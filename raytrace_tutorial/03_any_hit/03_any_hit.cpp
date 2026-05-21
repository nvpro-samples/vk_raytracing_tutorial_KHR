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
// Ray Tracing Tutorial - 03 Any Hit
//
// This sample demonstrates the use of any-hit shaders in ray tracing.
// Any-hit shaders are called for every intersection during ray traversal,
// allowing for transparency, alpha testing, and other effects.
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
#include "_autogen/rtanyhit.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial - 03 Any Hit
//
// This sample demonstrates the use of any-hit shaders in ray tracing.
// Any-hit shaders are called for every intersection during ray traversal,
// allowing for transparency, alpha testing, and other effects.
//
class RtAnyHit : public RtBase
{

public:
  RtAnyHit()           = default;
  ~RtAnyHit() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("AnyHit");
      ImGui::SliderFloat("Radius", &m_pushValues.radius, 0.01f, 5.0f);

      ImGui::SeparatorText("Transparency");
      ImGui::SliderFloat("Opacity", &m_pushValues.opacity, 0.0f, 1.0f);
      const char* transparencyModes[] = {"Cutout Only", "Stochastic", "Accumulative"};
      ImGui::Combo("Mode", &m_pushValues.transparencyMode, transparencyModes, 3);

      // Add helpful tooltips
      if(ImGui::IsItemHovered())
      {
        ImGui::BeginTooltip();
        ImGui::Text("Cutout Only: Binary transparency (original behavior)");
        ImGui::Text("Stochastic: Random sampling based on opacity");
        ImGui::Text("Accumulative: Proper handling of mixed transparent/opaque surfaces");
        ImGui::EndTooltip();
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
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 0.99f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},  // Greenish, mostly opaque
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f}};  // White


    // Make instances of the meshes
    // Create multiple overlapping planes to demonstrate transparency
    m_sceneResource.instances = {
        {.transform = glm::mat4(1.f), .materialIndex = 0, .meshIndex = 0},  // Wuson
        // Original plane at ground level
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)), glm::vec3(2.f)), .materialIndex = 1, .meshIndex = 1},
        // Additional planes for transparency demonstration (uncomment to test)
        //{.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0.5, 0)), glm::vec3(1.8f)), .materialIndex = 1, .meshIndex = 1},
        //{.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 1.0, 0)), glm::vec3(1.6f)), .materialIndex = 1, .meshIndex = 1},
    };

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    m_cameraManip->setLookat({2.39388, 1.49850, -1.67974}, {-0.40258, 0.55174, 0.00354}, {0.00000, 1.00000, 0.00000});
  }


  //--------------------------------------------------------------------------------------------------
  // Converting a PrimitiveMesh as input for BLAS
  //
  nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& gltfMesh) override
  {
    nvvk::AccelerationStructureGeometryInfo result = {};

    const shaderio::TriangleMesh triMesh       = gltfMesh.triMesh;
    const auto                   triangleCount = static_cast<uint32_t>(triMesh.indices.count / 3U);

    // Describe buffer as array of VertexObj.
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,  // vec3 vertex position data
        .vertexData   = {.deviceAddress = VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.positions.offset},
        .vertexStride = triMesh.positions.byteStride,
        .maxVertex    = triMesh.positions.count - 1,
        .indexType    = VkIndexType(gltfMesh.indexType),  // Index type (VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32)
        .indexData    = {.deviceAddress = VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.indices.offset},
    };

    // Identify the above data as containing opaque triangles.
    result.geometry = VkAccelerationStructureGeometryKHR{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = {.triangles = triangles},
        .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,  // Removed VK_GEOMETRY_OPAQUE_BIT_KHR so the Anyhit is called
    };

    result.rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = triangleCount};

    return result;
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Use pre-compiled shaders by default
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtanyhit.slang", rtanyhit_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eAnyHit,  // <---- Any-hit shader
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
    stages[eAnyHit].pNext     = &shaderCode;
    stages[eAnyHit].pName     = "anyhitMain";  // <---- Any-hit shader
    stages[eAnyHit].stage     = VK_SHADER_STAGE_ANY_HIT_BIT_NV;

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
    group.anyHitShader     = eAnyHit;  // <---- Any-hit shader
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  void onRender(VkCommandBuffer cmd) override
  {
    m_pushValues.frameNumber++;
    RtBase::onRender(cmd);
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
  appInfo.name           = "Ray Tracing Tutorial - 03 Any Hit";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtAnyHit>();
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