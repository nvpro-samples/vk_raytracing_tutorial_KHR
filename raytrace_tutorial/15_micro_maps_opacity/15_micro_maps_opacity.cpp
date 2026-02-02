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
// Ray Tracing Tutorial - 15 Micro-Maps Opacity
//
// This sample demonstrates the implementation of Opacity Micro-Maps in Vulkan
// for efficient ray tracing with selective AnyHit shader invocation.
//
//   This sample raytraces a plane made of 6x6 triangles with Micro-Mesh displacement
//   - The scene is created in
//           createScene()
//           - Micro - mesh creation uses the MicromapProcess class - Vulkan buffers holding the scene are created in createVkBuffers()
//           - Bottom and Top level acceleration structures are using the Vulkan buffers and scene description in
//           createBottomLevelAS()
//       and createTopLevelAS() - The raytracing pipeline,
//       composed of RayGen, Miss, ClosestHit shaders and the creation of the shader binding table,
//       is done increateRtxPipeline()
//           - Rendering is done in onRender()
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
#include "_autogen/micro_maps_opacity.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

#include "mm_process.hpp"


class Rt15MicroMapsOpacity : public RtBase
{
private:
  // Micro-maps specific settings
  struct MicroMapsSettings
  {
    bool     enableOpacity{true};
    int      subdivLevel{3};
    bool     showWireframe{true};
    float    radius{0.5f};
    bool     useAnyHit{true};
    uint16_t micromapFormat{VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT};
  } m_mmSettings;

  // Micro-maps resources
  std::unique_ptr<MicromapProcess> m_micromapProcess;
  VkPhysicalDeviceOpacityMicromapPropertiesEXT m_mmProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT};

  nvutils::PrimitiveMesh m_planeMesh;  // Plane mesh for micro-maps


public:
  Rt15MicroMapsOpacity()           = default;
  ~Rt15MicroMapsOpacity() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    RtBase::onUIRender();

    if(ImGui::Begin("Settings"))
    {
      bool settingsChanged = false;

      namespace PE = nvgui::PropertyEditor;
      if(PE::begin())
      {

        settingsChanged |= PE::Checkbox("Enable Opacity", &m_mmSettings.enableOpacity);
        settingsChanged |= PE::SliderInt("Subdivision Level", &m_mmSettings.subdivLevel, 1, 5);
        PE::Checkbox("Show Wireframe", &m_mmSettings.showWireframe);
        settingsChanged |= PE::SliderFloat("Radius", &m_mmSettings.radius, 0.1f, 2.0f);
        PE::Checkbox("Use AnyHit Shader", &m_mmSettings.useAnyHit);

        settingsChanged |= PE::entry("Micro-map format", [&] {
          return ImGui::RadioButton("2-States", (int*)&m_mmSettings.micromapFormat, VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT);
        });
        settingsChanged |= PE::entry("", [&] {
          return ImGui::RadioButton("4-States", (int*)&m_mmSettings.micromapFormat, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT);
        });

        PE::end();
      }
      if(settingsChanged)
      {

        vkDeviceWaitIdle(m_app->getDevice());

        if(m_micromapProcess)
        {
          VkCommandBuffer cmd = m_app->createTempCmdBuffer();
          m_micromapProcess->createMicromapData(cmd, m_stagingUploader, m_planeMesh, m_mmSettings.subdivLevel,
                                                m_mmSettings.radius, m_mmSettings.micromapFormat);
          m_app->submitAndWaitTempCmdBuffer(cmd);               // Wait for the micromap data to be ready
          m_micromapProcess->cleanBuildData();                  // Clean the micromap data
          assert(m_stagingUploader.isAppendedEmpty() == true);  // Ensure no pending uploads
        }
        // Recreate the acceleration structures with the new settings
        m_asBuilder.deinitAccelerationStructures();  // Destroy the acceleration structures builder
        createBottomLevelAS();
        createTopLevelAS();
      }
    }
    ImGui::End();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Requesting ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_mmProperties;
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);


    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Create a simple plane with 6x6 triangles for micro-maps demonstration
    m_planeMesh = nvutils::createPlane(3, 1.0F, 1.0F);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, m_planeMesh);

    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},
    };

    m_sceneResource.instances = {
        {.transform = glm::mat4(1), .materialIndex = 0, .meshIndex = 0},  // Plane
    };

    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);


    // #MICROMAP - Micromap Opacity Process
    {
      if(m_micromapProcess == nullptr)
      {
        m_micromapProcess = std::make_unique<MicromapProcess>(&m_allocator);
      }

      m_micromapProcess->createMicromapData(cmd, m_stagingUploader, m_planeMesh, m_mmSettings.subdivLevel,
                                            m_mmSettings.radius, m_mmSettings.micromapFormat);
    }

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources

    m_app->submitAndWaitTempCmdBuffer(cmd);

    m_micromapProcess->cleanBuildData();  // Clean the micromap data

    // Set the camera
    m_cameraManip->setLookat({-0.28558, 0.60154, 0.88699}, {0.00000, 0.00000, 0.00000}, {0.00000, 1.00000, 0.00000});
  }


  void createBottomLevelAS() override
  {
    std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(m_sceneResource.meshes.size());

    assert(m_sceneResource.meshes.size() == 1);  // The micromap is created for only one mesh
    std::vector<VkAccelerationStructureTrianglesOpacityMicromapEXT> geometryOpacity;  // hold data until BLAS is created
    geometryOpacity.reserve(m_sceneResource.meshes.size());

    for(uint32_t p_idx = 0; p_idx < m_sceneResource.meshes.size(); p_idx++)
    {
      geoInfos[p_idx] = primitiveToGeometry(m_sceneResource.meshes[p_idx]);

      // #MICROMAP
      VkAccelerationStructureTrianglesOpacityMicromapEXT opacityGeometryMicromap = {
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT};

      if(m_mmSettings.enableOpacity)
      {
        const VkDeviceAddress indexT_address = m_micromapProcess->indexBuffer().address;

        opacityGeometryMicromap.indexType                 = VK_INDEX_TYPE_UINT32;
        opacityGeometryMicromap.indexBuffer.deviceAddress = indexT_address;
        opacityGeometryMicromap.indexStride               = sizeof(int32_t);
        opacityGeometryMicromap.baseTriangle              = 0;
        opacityGeometryMicromap.micromap                  = m_micromapProcess->micromap();

        // Adding micromap
        geometryOpacity.emplace_back(opacityGeometryMicromap);
        geoInfos[p_idx].geometry.geometry.triangles.pNext = &geometryOpacity.back();
      }
    }
    m_asBuilder.blasSubmitBuildAndWait(geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createTopLevelAS() override
  {
    // #MICROMAP
    const VkBuildAccelerationStructureFlagsKHR buildFlags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
        | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;


    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size());
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};  // Makes the instance visible to all rays (double sided)
    for(const shaderio::GltfInstance& instance : m_sceneResource.instances)
    {
      VkAccelerationStructureInstanceKHR ray_inst{};
      ray_inst.transform                              = nvvk::toTransformMatrixKHR(instance.transform);
      ray_inst.instanceCustomIndex                    = instance.meshIndex;
      ray_inst.accelerationStructureReference         = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset = 0;
      ray_inst.flags                                  = flags;
      ray_inst.mask                                   = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }
    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, buildFlags);
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("micro_maps_opacity.slang", micro_maps_opacity_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eAnyHit,
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
    stages[eAnyHit].pName     = "rahitMain";
    stages[eAnyHit].stage     = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;  // #MICROMAP

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

    // Closest hit shader
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    group.anyHitShader     = eAnyHit;  // Add any-hit shader for opacity testing
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    rtPipelineInfo.flags = VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;  // #MICROMAP
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }


  void raytraceScene(VkCommandBuffer cmd) override
  {

    m_pushValues.maxDepth = 2;
    m_pushValues.numBaseTriangles =
        m_mmSettings.showWireframe ? (m_mmSettings.enableOpacity ? 1 << m_mmSettings.subdivLevel : 1) : 0;
    m_pushValues.radius    = m_mmSettings.radius;
    m_pushValues.useAnyHit = m_mmSettings.useAnyHit;

    RtBase::raytraceScene(cmd);  // Call the base class method to handle ray tracing
  }


private:
  void sampleDestroy() override { m_micromapProcess.reset(); }
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
  VkPhysicalDeviceOpacityMicromapFeaturesEXT opacityMicromapFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};  // #MICROMAP

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},      // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},   // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                   // Required by ray tracing pipeline
              {VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, &opacityMicromapFeature},  // For opacity micromaps
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

  // #MICROMAP
  if(opacityMicromapFeature.micromap == VK_FALSE)
  {
    LOGE("ERROR: Micro-Mesh not supported");
    exit(1);
  }


  // Setting up the application
  appInfo.name           = "Ray Tracing Tutorial - 15 Micro-Maps Opacity";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<Rt15MicroMapsOpacity>();
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
