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
// Ray Tracing Tutorial - 07 Multi Closest Hit
//
// This sample demonstrates the use of multiple closest hit shaders in ray tracing.
// Multiple closest hit shaders allow different objects to use different shaders,
// enabling per-instance material properties and effects.
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
#include <glm/gtc/color_space.hpp>  // For color space conversions

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtmulticlosesthit.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"
#include <nvgui/tooltip.hpp>

//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial - 07 Multi Closest Hit
//
// This sample demonstrates the use of multiple closest hit shaders in ray tracing.
// Multiple closest hit shaders allow different objects to use different shaders,
// enabling per-instance material properties and effects.
//
class RtMultiClosestHit : public RtBase
{

public:
  RtMultiClosestHit()           = default;
  ~RtMultiClosestHit() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Multi Closest Hit");
      glm::vec3 color0 = glm::convertLinearToSRGB(m_hitShaderRecord[0].color);
      ImGui::Text("Instance 0 Color");
      if(ImGui::ColorEdit3("##Color0", &color0.x))
      {
        m_hitShaderRecord[0].color = glm::convertSRGBToLinear(color0);
      }
      ImGui::BeginDisabled(true);  // Check in shader and un-comment to enable
      glm::vec3 color1 = glm::convertLinearToSRGB(m_hitShaderRecord[1].color);
      ImGui::Text("Instance 1 Color");
      if(ImGui::ColorEdit3("##Color1", &color1.x))
      {
        m_hitShaderRecord[1].color = glm::convertSRGBToLinear(color1);
      }
      nvgui::tooltip("Change code in shader (rchitMain3) before, then enable this");
      ImGui::EndDisabled();
      ImGui::Text("Press F5 or the button to recreate the pipeline");
      ImGui::Text("and see change applied");
      if(ImGui::Button("Recreate Pipeline"))
      {
        vkQueueWaitIdle(m_app->getQueue(0).queue);
        createRayTracingPipeline();  // Trigger shader recompilation
      }
    }
    ImGui::End();
    RtBase::onUIRender();
  }

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
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f},  // White
    };

    m_sceneResource.instances = {
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)), glm::vec3(2.f)), .materialIndex = 0, .meshIndex = 1},  // Plane
        {.transform = glm::translate(glm::mat4(1), glm::vec3(-1, 0, 0)), .materialIndex = 0, .meshIndex = 0},  // Wuson - left
        {.transform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0)), .materialIndex = 0, .meshIndex = 0},  // Wuson - right
    };

    // Configure which shader group each instance will use
    // This determines which closest hit shader is called for each instance
    m_shaderGroupIndices.resize(m_sceneResource.instances.size());
    m_shaderGroupIndices[0] = 0;  // Plane: uses HitGroup 0 (rchitMain - standard PBR)
    m_shaderGroupIndices[1] = 1;  // First wuson: uses HitGroup 1 (rchitMain2 - shader record data)
    m_shaderGroupIndices[2] = 2;  // Second wuson: uses HitGroup 2 (rchitMain3 - shader record data)


    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    m_cameraManip->setLookat({2.19703, 2.91932, -2.69778}, {-0.50040, 0.41911, -0.24307}, {0.00000, 1.00000, 0.00000});
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtmulticlosesthit.slang", rtmulticlosesthit_slang);

    // Creating all shaders
    // In this tutorial, we create multiple closest hit shaders to demonstrate
    // how different instances can use different shaders with different behaviors
    enum StageIndices
    {
      eRaygen,       // Ray generation shader (entry point for ray tracing)
      eMiss,         // Miss shader (when ray doesn't hit anything)
      eClosestHit,   // First closest hit shader (used by plane - standard PBR)
      eClosestHit2,  // Second closest hit shader (used by first wuson - uses shader record data)
      eClosestHit3,  // Third closest hit shader (used by second wuson - uses shader record data)
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for(auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    // Configure all shader stages
    stages[eRaygen].pNext     = &shaderCode;
    stages[eRaygen].pName     = "rgenMain";
    stages[eRaygen].stage     = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext       = &shaderCode;
    stages[eMiss].pName       = "rmissMain";
    stages[eMiss].stage       = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext = &shaderCode;
    stages[eClosestHit].pName = "rchitMain";
    stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    // Extra closest hit shaders for different instances
    stages[eClosestHit2].pNext = &shaderCode;
    stages[eClosestHit2].pName = "rchitMain2";
    stages[eClosestHit2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    //
    stages[eClosestHit3].pNext = &shaderCode;
    stages[eClosestHit3].pName = "rchitMain3";
    stages[eClosestHit3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;


    // Shader groups - these define how shaders are organized in the SBT
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader       = VK_SHADER_UNUSED_KHR;
    group.closestHitShader   = VK_SHADER_UNUSED_KHR;
    group.generalShader      = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    // Raygen group (Group 0 - RaygenGroup)
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shaderGroups.push_back(group);

    // Miss group (Group 1 - MissGroup 0)
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shaderGroups.push_back(group);

    // Hit groups - each instance can use a different closest hit shader
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);  // Group 2 - HitGroup 0 : Plane (uses rchitMain)

    // Extra closest hit groups for different instances
    group.closestHitShader = eClosestHit2;  // Group 3 - HitGroup 1: First wuson (uses rchitMain2)
    shaderGroups.push_back(group);
    group.closestHitShader = eClosestHit3;  // Group 4 - HitGroup 2: Second wuson (uses rchitMain3)
    shaderGroups.push_back(group);
    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);


    // Creating the SBT (Shader Binding Table)
    // The SBT contains shader handles and optional shader record data
    // Shader record data allows us to pass instance-specific data to shaders

    // Note: No shader record data is added for the first closest hit shader (Group 2)
    // This shader uses standard PBR lighting from material properties

    // Add shader record data for the second closest hit shader (Group 3)
    // This data will be accessible in rchitMain2 via the shader record buffer
    m_sbtGenerator.addData(nvvk::SBTGenerator::eHit, 1, m_hitShaderRecord[0]);

    // Add shader record data for the third closest hit shader (Group 4)
    // This data will be accessible in rchitMain3 via the shader record buffer
    m_sbtGenerator.addData(nvvk::SBTGenerator::eHit, 2, m_hitShaderRecord[1]);

    size_t bufferSize = m_sbtGenerator.calculateSBTBufferSize(m_rtPipeline, rtPipelineInfo);

    // Create SBT buffer using the size from above
    NVVK_CHECK(m_allocator.createBuffer(m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                        m_sbtGenerator.getBufferAlignment()));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate the SBT buffer with shader handles and data using the CPU-mapped memory pointer
    NVVK_CHECK(m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize, m_sbtBuffer.mapping));
  }

  // This function was overload because we are modifying the shader binding table (SBT) offset
  void createTopLevelAS() override
  {
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size());
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};  // Makes the instance visible to all rays (double sided)
    for(size_t i = 0; i < m_sceneResource.instances.size(); i++)
    {
      const shaderio::GltfInstance&      instance = m_sceneResource.instances[i];
      VkAccelerationStructureInstanceKHR tlasInst{};
      tlasInst.transform                      = nvvk::toTransformMatrixKHR(instance.transform);
      tlasInst.instanceCustomIndex            = instance.meshIndex;
      tlasInst.accelerationStructureReference = m_asBuilder.blasSet[instance.meshIndex].address;

      tlasInst.instanceShaderBindingTableRecordOffset = m_shaderGroupIndices[i];  // <-- here we set the shader group index for each instance
      tlasInst.flags = flags;
      tlasInst.mask  = 0xFF;
      tlasInstances.emplace_back(tlasInst);
    }
    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }


protected:
  // Shader group indices for each instance - determines which closest hit shader to use
  std::vector<int> m_shaderGroupIndices;

  // Shader record data structure - data passed to shaders via SBT
  struct HitRecordBuffer
  {
    glm::vec3 color;  // Color data that will be accessible in the shader
  };

  // Initialize shader record data for different instances
  // This data is passed to the closest hit shaders via the SBT
  std::vector<HitRecordBuffer> m_hitShaderRecord = {
      {.color = glm::vec3(0.8f, 1.0f, 0.6f)},  // Green color for first wuson
      {.color = glm::vec3(0.6f, 0.8f, 1.0f)}   // Cyan color for second wuson
  };
};

//-------------------------------------------------------------------------------
// Main
//-------------------------------------------------------------------------------
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
  appInfo.name           = "Ray Tracing Tutorial - 07 Multi Closest Hit";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtMultiClosestHit>();
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