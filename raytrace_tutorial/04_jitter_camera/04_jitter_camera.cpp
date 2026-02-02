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
// Ray Tracing Tutorial - 04 Jitter Camera
//
// This sample demonstrates how to use jittering to improve the quality of the ray tracing.
// It shows how to use a random number generator to jitter the camera position and direction,
// allowing for more accurate rendering of the scene.
//

// #define USE_NSIGHT_AFTERMATH

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
  {                                                                                                                    \
    printf((format), __VA_ARGS__);                                                                                     \
    printf("\n");                                                                                                      \
  }

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtjittercamera.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

class RtJitterCamera : public RtBase
{
public:
  RtJitterCamera()           = default;
  ~RtJitterCamera() override = default;

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Jitter");
      if(ImGui::SliderInt("Max Frames", &m_maxFrames, 1, 100))
        resetFrame();
      ImGui::TextDisabled("Frame: %d", m_pushValues.frame);
    }
    ImGui::End();

    if(RtBase::renderUI())
      resetFrame();  // Reset the frame if any UI parameter was modified
  }

  // Override to customize scene creation
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
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f}};  // White


    // Make instances of the meshes
    m_sceneResource.instances = {
        {.transform = glm::mat4(1.f), .materialIndex = 0, .meshIndex = 0},  // Wuson
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)), glm::vec3(2.f)), .materialIndex = 1, .meshIndex = 1},  // Plane
    };

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    m_cameraManip->setLookat({0.40429, 1.09030, -1.63537}, {-1.02078, 0.90831, 0.15507}, {0.00000, 1.00000, 0.00000});
  }

  // Override to customize ray tracing pipeline creation
  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtjittercamera.slang", rtjittercamera_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
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

  // Override raytraceScene to add frame management
  void raytraceScene(VkCommandBuffer cmd) override
  {
    updateFrame();
    if(m_pushValues.frame >= m_maxFrames)
      return;

    RtBase::raytraceScene(cmd);
  }

  // Frame management functions
  void resetFrame() { m_pushValues.frame = -1; }

  void updateFrame()
  {
    static glm::mat4 refCamMatrix;
    static float     refFov{m_cameraManip->getFov()};

    const auto& m   = m_cameraManip->getViewMatrix();
    const auto  fov = m_cameraManip->getFov();

    if(refCamMatrix != m || refFov != fov)
    {
      resetFrame();
      refCamMatrix = m;
      refFov       = fov;
    }
    m_pushValues.frame = std::min(++m_pushValues.frame, m_maxFrames);  // Increment frame count, but limit it to maxFrames
  }

  // Override onResize to reset frame
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) override
  {
    resetFrame();
    RtBase::onResize(cmd, size);
  }

private:
  int m_maxFrames{100};  // Maximum number of frames for accumulation
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
  appInfo.name           = "Ray Tracing Tutorial - 04 Jitter Camera";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtJitterCamera>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();  // Element displaying a menu, File->Exit ...
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