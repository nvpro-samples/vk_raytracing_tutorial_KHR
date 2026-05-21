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
// Ray Tracing Tutorial - 11 Shader Execution Reorder
//
// This sample demonstrates the use of Shader Execution Reorder (SER) to improve
// ray tracing performance by reducing divergence in shader execution. SER allows
// fine-grained control over the scheduling of ray tracing operations by intelligently
// reordering shader invocations based on their execution characteristics.
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

#include <fmt/format.h>
#include "shaders/shaderio.h"

// Elements to monitor the GPU and profiling
#include <nvapp/elem_profiler.hpp>
#include <nvgpu_monitor/elem_gpu_monitor.hpp>
#include <nvutils/profiler.hpp>
#include <nvvk/profiler_vk.hpp>


// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/shader_execution_reorder.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

nvutils::ProfilerManager g_profilerManager;  // #PROFILER

class RtShadingExecutionReorder : public RtBase
{

public:
  RtShadingExecutionReorder()
  {
    m_slangCapabilities = {"spvShaderInvocationReorderNV", "spvShaderInvocationReorderEXT", "spvShaderClockKHR"};
  }
  ~RtShadingExecutionReorder() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onAttach(nvapp::Application* app) override
  {
    // Requesting ray tracing properties and reorder properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_reorderProperties;
    vkGetPhysicalDeviceProperties2(app->getPhysicalDevice(), &prop2);

    RtBase::onAttach(app);

    // ===== Profiling & Performance =====
    {
      SCOPED_TIMER("Profiler");
      m_profilerTimeline = g_profilerManager.createTimeline({.name = "Primary Timeline"});
      m_profilerGpuTimer.init(m_profilerTimeline, m_app->getDevice(), m_app->getPhysicalDevice(),
                              m_app->getQueue(0).familyIndex, false);
    }
  }


  void onUIRender() override
  {
    bool modified = false;
    namespace PE  = nvgui::PropertyEditor;
    // Display the rendering GBuffer in the ImGui window ("Viewport")
    // Either the heatmap or the final image
    if(ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(m_showHeatmap ? 2 : eImgTonemapped)), ImGui::GetContentRegionAvail());
    }
    ImGui::End();
    // Setting panel
    if(ImGui::Begin("Settings"))
    {
      if(ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_cameraManip);
      if(ImGui::CollapsingHeader("Sky"))
        modified |= nvgui::skySimpleParametersUI(m_sceneResource.sceneInfo.skySimpleParam);
      if(ImGui::CollapsingHeader("Tonemapper"))
        nvgui::tonemapperWidget(m_tonemapperData);
      if(ImGui::Checkbox("Use SER", &m_enableSER))
      {
        recreatePipeline();  // Recreate the pipeline if SER usage is toggled
      }
      modified |= ImGui::SliderInt("Sample per Frame", &m_pushValues.maxSamples, 1, 16);
      modified |= ImGui::SliderInt("Max Depth", &m_pushValues.maxDepth, 1, 20);
      modified |= ImGui::SliderInt("Max Frames", &m_maxFrames, 1, 100000);
      modified |= ImGui::SliderFloat2("Metallic/Roughness", glm::value_ptr(m_metallicRoughnessOverride), 0.f, 1.f);

      if(PE::begin())
      {
        PE::entry("SER Mode", fmt::format("{}", m_reorderProperties.rayTracingInvocationReorderReorderingHint
                                                        == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_NV ?
                                                    "Active" :
                                                    "Not Available"));

        if(PE::entry("Heatmap", [&] {
             static const ImVec4 highlightColor = ImVec4(118.f / 255.f, 185.f / 255.f, 0.f, 1.f);
             ImVec4 selectedColor = m_showHeatmap ? highlightColor : ImGui::GetStyleColorVec4(ImGuiCol_Button);
             ImVec4 hoveredColor  = ImVec4(selectedColor.x * 1.2f, selectedColor.y * 1.2f, selectedColor.z * 1.2f, 1.f);
             ImGui::PushStyleColor(ImGuiCol_Button, selectedColor);
             ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
             ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));
             bool result = ImGui::ImageButton("##but", (ImTextureID)m_gBuffers.getDescriptorSet(2),
                                              ImVec2(100 * m_gBuffers.getAspectRatio(), 100));
             ImGui::PopStyleColor(2);
             ImGui::PopStyleVar();
             return result;
           }))
        {
          m_showHeatmap = !m_showHeatmap;
        }
        PE::end();
      }
    }
    ImGui::End();

    if(modified)
      resetFrame();  // Reset frame count if max frames is changed
  }

  void onRender(VkCommandBuffer cmd) override
  {
    m_profilerTimeline->frameAdvance();  // #PROFILER
    auto sectionID = m_profilerGpuTimer.cmdFrameSection(cmd, __FUNCTION__);
    RtBase::onRender(cmd);
  }


  // Destroy the resources created for this sample
  void sampleDestroy() override
  {
    m_allocator.destroyBuffer(m_bHeatStats);
    m_bHeatStats = {};
    // #PROFILER
    m_profilerGpuTimer.deinit();
    g_profilerManager.destroyTimeline(m_profilerTimeline);
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    m_metallicRoughnessOverride = {0.5f, 0.1f};  // Default metallic and roughness override

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    constexpr int   numObj       = 20;
    constexpr float objSize      = 1.0F;
    constexpr float objSpacing   = 2.0f;
    constexpr int   numMaterials = 128;

    srand(12312412);

    // Meshes
    nvutils::PrimitiveMesh sphere = nvutils::createSphereUv(objSize);
    nvutils::PrimitiveMesh plane  = nvutils::createPlane(1, 100, 100);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, sphere);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, plane);

    // Color from an integer
    auto autoColor = [](int i) {
      const glm::vec3 freq = glm::vec3(1.33333F, 2.33333F, 3.33333F) * static_cast<float>(i);
      return glm::vec4(static_cast<glm::vec3>(sin(freq) * 0.5F + 0.5F), 1);
    };

    // For the hollow box, we will skip the center instances
    auto inRange = [](int a, int b, int v0 = 10 - 3, int v1 = 10 + 2) {
      return (a >= v0 && a <= v1) && (b >= v0 && b <= v1);
    };

    m_sceneResource.materials.reserve(numMaterials + 1);
    m_sceneResource.materials.push_back({.baseColorFactor = {1.f, 1.f, 1.f, 1.f}, .metallicFactor = 0.0f, .roughnessFactor = 0.9f});
    for(int i = 0; i < numMaterials; i++)
    {
      m_sceneResource.materials.push_back({.baseColorFactor = autoColor(i), .metallicFactor = 0.0f, .roughnessFactor = 0.9f});
    }

    // Create the instances of the spheres (hollow box)
    m_sceneResource.instances.reserve(static_cast<size_t>(numObj * numObj) * numObj);
    bool skip{false};
    int  count{0};
    for(int k = 0; k < numObj; k++)
    {
      for(int j = 0; j < numObj; j++)
      {
        for(int i = 0; i < numObj; i++)
        {
          bool center = inRange(i, j);
          center |= inRange(i, k);
          center |= inRange(k, j);
          if(!skip && !center)
          {
            auto& n     = m_sceneResource.instances.emplace_back();
            n.meshIndex = 0;  // The sphere mesh index
            // One of the numMaterials colors

            n.materialIndex       = count % numMaterials + 1;
            glm::vec3 translation = {-(static_cast<float>(numObj) / 2.F) + static_cast<float>(i),
                                     -(static_cast<float>(numObj) / 2.F) + static_cast<float>(j),
                                     -(static_cast<float>(numObj) / 2.F) + static_cast<float>(k)};
            translation *= objSpacing;
            n.transform = glm::translate(glm::mat4(1), translation);
            count++;
          }
          skip = !skip;
        }
        skip = !skip;
      }
      skip = !skip;
    }
    m_sceneResource.instances.shrink_to_fit();

    // Add the plane
    {
      shaderio::GltfInstance instance{};
      instance.materialIndex = 0;
      instance.transform     = glm::translate(glm::mat4(1), glm::vec3(0, -25, 0));
      instance.meshIndex     = 1;
      m_sceneResource.instances.push_back(instance);
    }


    // Create the buffer for the heatmap statistics
    NVVK_CHECK(m_allocator.createBuffer(m_bHeatStats, sizeof(uint32_t) * 2, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT));
    NVVK_DBG_NAME(m_bHeatStats.buffer);


    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set the camera for good viewing of the scene
    m_cameraManip->setLookat({0.0F, 2.0F, static_cast<float>(numObj) * objSpacing * 1.5f}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("shader_execution_reorder.slang", shader_execution_reorder_slang);

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

    // Create specialization constant for SER enable/disable
    VkSpecializationMapEntry specializationEntry{};
    specializationEntry.constantID = 0;  // SER enable constant [vk::constant_id(0)]
    specializationEntry.offset     = 0;
    specializationEntry.size       = sizeof(uint32_t);

    uint32_t             serEnable = m_enableSER ? 1 : 0;
    VkSpecializationInfo specializationInfo{};
    specializationInfo.mapEntryCount = 1;
    specializationInfo.pMapEntries   = &specializationEntry;
    specializationInfo.dataSize      = sizeof(uint32_t);
    specializationInfo.pData         = &serEnable;

    // Apply specialization to raygen shader
    stages[eRaygen].pSpecializationInfo = &specializationInfo;

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }


  // Override to add more GBuffers (Heatmap)
  void createGBuffers(VkSampler linearSampler) override
  {  // Create G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator      = &m_allocator,
        .colorFormats   = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_SFLOAT},
        .depthFormat    = nvvk::findDepthFormat(m_app->getPhysicalDevice()),
        .imageSampler   = linearSampler,
        .descriptorPool = m_app->getTextureDescriptorPool(),
    };
    m_gBuffers.init(gBufferInit);
  }

  // Here,on top of the actual descriptor set (1), we are adding two extra descriptor: heatmap and heatstat
  void createRaytraceDescriptorLayout() override
  {
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eHeatmap,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eHeatStats,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});

    RtBase::createRaytraceDescriptorLayout();
  }


  // Like in 04_jitter_camera.cpp, we override the ray trace scene method to manage frames

  // Override raytraceScene to add frame management
  void raytraceScene(VkCommandBuffer cmd) override
  {
    updateFrame();
    if(m_pushValues.frame >= m_maxFrames)
      return;

    // Push descriptor sets for the heatmap and heat statistics
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eHeatStats), m_bHeatStats.buffer, 0, VK_WHOLE_SIZE);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eHeatmap), m_gBuffers.getColorImageView(2), VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1, write.size(), write.data());

    // Reset maximum for current frame
    vkCmdFillBuffer(cmd, m_bHeatStats.buffer, (uint32_t(m_pushValues.frame) & 1) * sizeof(uint32_t), sizeof(uint32_t), 1);

    // Add memory barrier to ensure buffer fill completes before ray tracing reads
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);

    // Normal ray tracing
    RtBase::raytraceScene(cmd);
  }

  // Frame management functions
  void resetFrame() { m_pushValues.frame = -1; }

  void updateFrame()
  {
    static glm::dmat4 refCamMatrix;
    static double     refFov{m_cameraManip->getFov()};

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

  //--------------------------------------------------------------------------------------------------
  // Create the top level acceleration structures, referencing all BLAS
  // !!! The change is simply the instanceCustomIndex to use the material index !!!
  void createTopLevelAS() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Prepare instance data for TLAS
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size());
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    for(const shaderio::GltfInstance& instance : m_sceneResource.instances)
    {
      VkAccelerationStructureInstanceKHR ray_inst{};
      ray_inst.transform           = nvvk::toTransformMatrixKHR(instance.transform);  // Position of the instance
      ray_inst.instanceCustomIndex = instance.materialIndex;                          // gl_InstanceCustomIndexEXT
      ray_inst.accelerationStructureReference         = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset = 0;  // We will use the same hit group for all objects
      ray_inst.flags                                  = flags;
      ray_inst.mask                                   = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }

    // Build the top-level acceleration structure
    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }


private:
  bool         m_enableSER   = true;   // SER enabled by default
  int          m_maxFrames   = 10000;  // Maximum number of frames for accumulation
  bool         m_showHeatmap = false;  // Show heatmap in UI
  nvvk::Buffer m_bHeatStats;

  // Profiler
  nvutils::ProfilerTimeline* m_profilerTimeline{};  // Timeline profiler
  nvvk::ProfilerGpuTimer     m_profilerGpuTimer{};  // GPU profiler


  VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV m_reorderProperties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV};

  void recreatePipeline()
  {
    vkDeviceWaitIdle(m_app->getDevice());
    createRayTracingPipeline();
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

  // Add SER-specific features
  VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV serFeatureNV{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV};

  // Add shader clock feature for heatmap timing (getRealtimeClock in shader)
  VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
  shaderClockFeature.shaderDeviceClock = VK_TRUE;

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},         // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},      // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                      // Required by ray tracing pipeline
              {VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, &serFeatureNV},  // For SER support (required)
              {VK_KHR_SHADER_CLOCK_EXTENSION_NAME, &shaderClockFeature},             // For shader clock (heatmap)
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

  // Check if SER is supported
  auto& logger = nvutils::Logger::getInstance();
  if(serFeatureNV.rayTracingInvocationReorder == VK_FALSE)
  {
    logger.log(nvutils::Logger::eERROR, "SER(NV) support: Not Available\n");
    return 1;
  }
  logger.log(nvutils::Logger::eINFO, "SER(NV) support: Available\n");


  // Setting up the application
  appInfo.name           = "Ray Tracing Tutorial - 11 Shader Execution Reorder";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();
  appInfo.vSync          = false;  // To show the speed gain

  // Setting up the layout of the application
  appInfo.dockSetup = [](ImGuiID viewportID) {
    ImGuiID leftPane = ImGui::DockBuilderSplitNode(viewportID, ImGuiDir_Left, 0.25F, nullptr, &viewportID);
    ImGuiID downPane = ImGui::DockBuilderSplitNode(leftPane, ImGuiDir_Down, 0.35F, nullptr, &leftPane);
    ImGui::DockBuilderDockWindow("Settings", leftPane);
    ImGui::DockBuilderDockWindow("NVML Monitor", downPane);
    ImGui::DockBuilderDockWindow("Profiler", downPane);
  };

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial       = std::make_shared<RtShadingExecutionReorder>();
  auto elemCamera     = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle    = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu     = std::make_shared<nvapp::ElementDefaultMenu>();
  auto elemGpuMonitor = std::make_shared<nvgpu_monitor::ElementGpuMonitor>();
  auto elemProfiler   = std::make_shared<nvapp::ElementProfiler>(&g_profilerManager);

  auto camManip = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Add elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(elemGpuMonitor);
  application.addElement(elemProfiler);
  application.addElement(tutorial);

  application.run();
  application.deinit();
  vkContext->deinit();

  return 0;
}