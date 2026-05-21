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
// Ray Tracing Tutorial - 06 Reflection
//
// This sample demonstrates how to implement reflective surfaces using ray tracing in Vulkan.
// It shows how to trace secondary rays to compute reflections, allowing objects to reflect
// their surroundings realistically. The tutorial builds on previous samples by adding
// recursive ray tracing for mirror-like effects and discusses performance considerations
// for handling multiple ray bounces.
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
#include "_autogen/rtreflection.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

// Reflection mode (must match shader define in rtreflection.slang)
// 0 = Iterative (RECOMMENDED): Loop-based, only needs maxPipelineRayRecursionDepth = 2
//     Can handle unlimited reflection depth! Not constrained by hardware recursion limits.
// 1 = Recursive: Hardware recursion, requires maxPipelineRayRecursionDepth = MAX_DEPTH + 2
//     Limited by GPU's maxRayRecursionDepth (typically 4-31, varies by vendor)
//     +2 accounts for: primary ray + shadow ray at deepest reflection level
#define USE_RECURSIVE_REFLECTION 0

#define MAX_DEPTH 10U  // Maximum reflection depth for UI slider


class RtReflection : public RtBase
{
public:
  RtReflection()           = default;
  ~RtReflection() override = default;

  // Override sampleUI to add reflection controls
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Reflection");
      {
        PE::begin();
        if(PE::SliderInt("Reflection Depth", &m_pushValues.depthMax, 1, MAX_DEPTH, "%d", ImGuiSliderFlags_AlwaysClamp,
                         "Maximum reflection depth"))
        {
          LOGI("Reflection depth set to %d\n", m_pushValues.depthMax);
        }
        PE::end();
      }
      ImGui::End();
    }
    RtBase::onUIRender();  // Call base class UI rendering
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Load a complex scene with multiple objects to demonstrate shadow casting
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    VkCommandBuffer cmd              = m_app->createTempCmdBuffer();
    m_sceneResource.sceneInfo.useSky = true;  // Use sky for lighting

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
        {.baseColorFactor = glm::vec4(.7f, .17f, .17f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.1f},  // Grey
        {.baseColorFactor = glm::vec4(0.8f, 0.8f, 1.0f, 1.0f), .metallicFactor = 0.99f, .roughnessFactor = 0.01f},  // Mirror
    };

    m_sceneResource.instances = {
        // Wuson
        {.transform = glm::mat4(1.f), .materialIndex = 0, .meshIndex = 0},
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)), glm::vec3(2.f)), .materialIndex = 1, .meshIndex = 1},  // Plane
        // Left mirror
        {.transform = glm::rotate(glm::translate(glm::mat4(1), glm::vec3(-1.5f, 0, 0)), glm::radians(-90.0f), glm::vec3(0, 0, 1)),
         .materialIndex = 2,
         .meshIndex     = 1},
        // Right mirror
        {.transform = glm::rotate(glm::translate(glm::mat4(1), glm::vec3(1.5f, 0, 0)), glm::radians(90.0f), glm::vec3(0, 0, 1)),
         .materialIndex = 2,
         .meshIndex     = 1},
    };

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set up camera
    m_cameraManip->setLookat({1.03534, 1.19964, -2.07709}, {-0.05626, 0.81966, -1.40429}, {0.00000, 1.00000, 0.00000});
  }


  // Override to customize ray tracing pipeline creation
  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtreflection.slang", rtreflection_slang);

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
    // CRITICAL: The pipeline depth must match the shader mode!
    // Iterative mode only needs depth=2 (primary + shadow) since reflections are handled in a loop.
    // Recursive mode needs depth=MAX_DEPTH+2 because each reflection also traces a shadow ray:
    //   - 1 level for primary ray from raygen
    //   - MAX_DEPTH levels for N reflections
    //   - +1 for shadow ray at the deepest reflection level
    //
    // Example: MAX_DEPTH=10 needs pipeline depth=12 (1 primary + 10 reflections + 1 final shadow)
#if USE_RECURSIVE_REFLECTION
    const uint32_t pipelineDepth = MAX_DEPTH + 2;  // Recursive: primary + reflections + shadow
#else
    const uint32_t pipelineDepth = 2;  // Iterative: only primary and shadow (RECOMMENDED)
#endif
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, pipelineDepth);
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
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

  // To enable ray tracing validation, set the NV_ALLOW_RAYTRACING_VALIDATION=1 environment variable
  // https://developer.nvidia.com/blog/ray-tracing-validation-at-the-driver-level/
  // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_NV_ray_tracing_validation.html
  VkPhysicalDeviceRayTracingValidationFeaturesNV validationFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME, &validationFeatures, false},  // For validation (optional)
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
  appInfo.name           = "Ray Tracing Tutorial - 06 Reflection";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtReflection>();
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