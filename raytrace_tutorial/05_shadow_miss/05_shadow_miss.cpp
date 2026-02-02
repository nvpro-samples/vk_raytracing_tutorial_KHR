/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

//
// Ray Tracing Tutorial - 05 Shadow Miss
//
// This sample demonstrates the use of a dedicated shadow miss shader for better performance.
// Instead of using RAY_FLAG_SKIP_CLOSEST_HIT_SHADER with the regular miss shader,
// we create a separate miss shader specifically for shadow rays with a minimal payload.
//

// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH  // (not always on, as it slows down the application)

#define TINYGLTF_IMPLEMENTATION         // Implementation of the GLTF loader library
#define STB_IMAGE_IMPLEMENTATION        // Implementation of the image loading library
#define STB_IMAGE_WRITE_IMPLEMENTATION  // Implementation of the image writing library
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1  // Use dynamic Vulkan functions for VMA (Vulkan Memory Allocator)
#define VMA_IMPLEMENTATION              // Implementation of the Vulkan Memory Allocator
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
  {                                                                                                                    \
    printf((format), __VA_ARGS__);                                                                                     \
    printf("\n");                                                                                                      \
  }

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtshadowmiss.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

//---------------------------------------------------------------------------------------------------------------
// Ray Tracing Tutorial - Shadow Miss Shader
//
// This sample demonstrates the use of a dedicated shadow miss shader for better performance.
// Instead of using RAY_FLAG_SKIP_CLOSEST_HIT_SHADER with the regular miss shader,
// we create a separate miss shader specifically for shadow rays with a minimal payload.
//
class RtShadowMiss : public RtBase
{
public:
  RtShadowMiss()           = default;
  ~RtShadowMiss() override = default;

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Load a complex scene with multiple objects to demonstrate shadow casting
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Load the GLTF resources
    {
      tinygltf::Model wusonModel =
          nvsamples::loadGltfResources(nvutils::findFile("wuson.glb", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      tinygltf::Model planeModel =
          nvsamples::loadGltfResources(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      // Import and create the glTF data buffer
      nvsamples::importGltfData(m_sceneResource, wusonModel, m_stagingUploader, false);
      nvsamples::importGltfData(m_sceneResource, planeModel, m_stagingUploader, false);
    }


    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},  // Bronze
        {.baseColorFactor = glm::vec4(1.0f, 0.6f, 0.8f, 1.0f), .metallicFactor = 0.8f, .roughnessFactor = 0.2f},  // Pink
        {.baseColorFactor = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f), .metallicFactor = 0.2f, .roughnessFactor = 0.8f},  // Light Blue
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f}};  // White


    // Make instances of the meshes
    m_sceneResource.instances = {
        // Wuson
        {.transform = glm::mat4(1.f), .materialIndex = 0, .meshIndex = 0},
        // Wuson
        {.transform = glm::translate(glm::mat4(1), glm::vec3(1.5f, 0.0f, 0.0f))
                      * glm::rotate(glm::mat4(1), glm::radians(45.0f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.8f)),
         .materialIndex = 1,
         .meshIndex     = 0},
        // Wuson
        {.transform = glm::translate(glm::mat4(1), glm::vec3(-1.5f, 0.0f, 0.0f))
                      * glm::rotate(glm::mat4(1), glm::radians(-30.0f), glm::vec3(0, 1, 0))
                      * glm::scale(glm::mat4(1), glm::vec3(1.2f)),
         .materialIndex = 2,
         .meshIndex     = 0},
        // Plane
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)), glm::vec3(2.f)), .materialIndex = 3, .meshIndex = 1},
    };
    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set up camera
    m_cameraManip->setLookat({1.29575, 1.44139, -3.10521}, {-0.40258, 0.55174, 0.00354}, {0.00000, 1.00000, 0.00000});
  }

  // Override to destroy local created resources
  void sampleDestroy() override {};

  // Override to customize ray tracing pipeline creation
  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtshadowmiss.slang", rtshadowmiss_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eMissShadow,  // <---- Dedicated shadow miss shader
      eClosestHit,
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for(auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    stages[eRaygen].pNext     = &shaderCode;
    stages[eRaygen].pName     = "rgenMain";
    stages[eRaygen].stage     = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext       = &shaderCode;
    stages[eMiss].pName       = "rmissMain";
    stages[eMiss].stage       = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eMissShadow].pNext = &shaderCode;
    stages[eMissShadow].pName = "rmissShadowMain";  // <---- Shadow miss shader
    stages[eMissShadow].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext = &shaderCode;
    stages[eClosestHit].pName = "rchitMain";
    stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Shader groups
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader       = VK_SHADER_UNUSED_KHR;
    group.closestHitShader   = VK_SHADER_UNUSED_KHR;
    group.generalShader      = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    // Raygen
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shaderGroups.push_back(group);

    // Miss
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shaderGroups.push_back(group);

    // Shadow Miss
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMissShadow;  // <---- Shadow miss shader group
    shaderGroups.push_back(group);

    // closest hit shader
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }
};

//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  nvutils::ParameterParser   cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Setting up the Vulkan context, instance and device extensions
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
          },
  };

  if(!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // Create Vulkan context using the new method
  auto vkContext = RtBase::createVulkanContext(vkSetup);
  if(!vkContext)
  {
    return 1;
  }

  // Setting up the application
  appInfo.name           = "Ray Tracing Tutorial - 05 Shadow Miss";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtShadowMiss>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Add elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();
  application.deinit();
  vkContext->deinit();

  return 0;
}