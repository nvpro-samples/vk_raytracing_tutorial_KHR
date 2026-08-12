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
// Ray Tracing Tutorial - 22 Partitioned TLAS (PTLAS)
//
// Demonstrates VK_NV_partitioned_acceleration_structure. A grid of sphere instances is grouped into
// spatial partitions (plus a ground plane in the special "global" partition). The PTLAS is referenced
// by device address, turned into an acceleration structure in the shader, and traced like any other;
// the hit shader colors each instance by its partition (written into the instanceID).
//
// The point of a PTLAS is that a per-frame update only touches what changed - the rest of the
// structure is retained. To make that visible, two radii around the camera split the grid into three
// zones, each with its own operation, cost, and level of detail (LOD = zone):
//   * NEAR : every instance is re-specified individually (WRITE_INSTANCE), at the high-poly LOD - the
//            spheres ripple.
//   * MID  : each partition is shifted with a single record (WRITE_PARTITION_TRANSLATION), at the
//            medium LOD - the whole 16-instance tile rocks as one.
//   * FAR  : nothing is submitted - the instances are retained by the src == dst build (zero ops), at
//            the coarse LOD.
// When the camera moves and an instance crosses a MID<->FAR boundary, it is re-pointed at the new LOD
// BLAS with UPDATE_INSTANCE (a cheap geometry swap). All three core PTLAS ops are exercised. A regular
// TLAS has no equivalent: to move anything it must reprocess all of its instances. The production-scale,
// GPU-driven version of this idea is vk_forest_ray_tracing.
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

#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/partition_tlas.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

#include <nvvk/barriers.hpp>
#include <nvutils/primitives.hpp>


//--------------------------------------------------------------------------------------------------
// Ray tracing a partitioned grid of instances (PTLAS)
//
class RtPartitionTlas : public RtBase
{
  // Grid: kGridN x kGridN sphere instances, tiled into kTile x kTile partitions. The layout constants
  // are shared with the shader (shaderio.h) so it can classify each instance's zone the same way.
  static constexpr int kGridN        = PT_GRID_N;              // instances per axis (32)
  static constexpr int kTile         = PT_TILE;                // instances per partition-axis (4)
  static constexpr int kPartAxis     = PT_PART_AXIS;           // partitions per axis (8)
  static constexpr int kPartitions   = kPartAxis * kPartAxis;  // 64
  static constexpr int kNumSpheres   = kGridN * kGridN;        // 1024
  static constexpr int kNumInstances = kNumSpheres + 1;        // + ground plane

  static constexpr float kSpacing = PT_SPACING;
  static constexpr float kRadiusS = 0.42f;
  static constexpr float kSphereY = 0.9f;  // sphere centers float above the plane (y = 0)

  // Per-frame update zone, decided by a partition's distance to the camera.
  enum Zone
  {
    eFar  = 0,  // retained, no ops
    eMid  = 1,  // WRITE_PARTITION_TRANSLATION (whole partition moves), medium LOD
    eNear = 2,  // WRITE_INSTANCE (each instance moves), high LOD
  };

  // Mesh indices (order created in createScene / order of blasSet). Three sphere LODs (a BLAS each) let
  // a per-frame update swap an instance's geometry by distance: high-poly NEAR, coarse FAR.
  enum Mesh
  {
    eMeshLodHigh = 0,  // NEAR zone  - smooth sphere
    eMeshLodMed  = 1,  // MID zone   - medium sphere
    eMeshLodLow  = 2,  // FAR zone   - coarse, faceted sphere
    eMeshPlane   = 3
  };

  // Sphere BLAS to use for a zone (level of detail = update zone).
  uint32_t lodMesh(Zone z) const { return z == eNear ? eMeshLodHigh : (z == eMid ? eMeshLodMed : eMeshLodLow); }

  // Per-frame ring resources: everything the CPU writes each frame, plus the build scratch. One per
  // frame-in-flight so the CPU never overwrites data an in-flight frame's GPU work still reads.
  struct FrameRes
  {
    nvvk::Buffer scratch;     // build/update scratch
    nvvk::Buffer ops;         // array of indirect operation commands
    nvvk::Buffer opsCount;    // number of operation commands
    nvvk::Buffer writeInst;   // WRITE_INSTANCE records
    nvvk::Buffer updateInst;  // UPDATE_INSTANCE records (LOD / BLAS swaps)
    nvvk::Buffer partTrans;   // WRITE_PARTITION_TRANSLATION records
  };

public:
  RtPartitionTlas()           = default;
  ~RtPartitionTlas() override = default;

  void setExtensionSupport(bool supported) { m_extensionSupported = supported; }
  void setInitialTime(float t) { m_time = t; }
  void setPlay(bool p) { m_play = p; }
  void setAzimuth(float deg) { m_azimuthDeg = deg; }
  void setOrbit(bool o) { m_orbit = o; }

  //-------------------------------------------------------------------------------
  // UI
  //-------------------------------------------------------------------------------
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;

    if(ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(eImgTonemapped)), ImGui::GetContentRegionAvail());
    }
    ImGui::End();

    if(ImGui::Begin("Settings"))
    {
      if(ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_cameraManip);

      ImGui::SeparatorText("Partitioned TLAS");
      if(m_extensionSupported)
      {
        ImGui::Text("%d instances in %d partitions (+1 global plane)", kNumSpheres, kPartitions);

        // Timeline: play, or pause and scrub / drive the controls by hand to watch the op counts.
        ImGui::Checkbox("Play", &m_play);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##time", &m_time, 0.0f, 12.0f, "time %.2fs");

        PE::begin();
        PE::SliderFloat("Near radius", &m_nearRadius, 0.0f, 60.0f, "%.0f", ImGuiSliderFlags_None,
                        "Inside this distance each instance is updated on its own (WRITE_INSTANCE)");
        PE::SliderFloat("Mid radius", &m_midRadius, 0.0f, 80.0f, "%.0f", ImGuiSliderFlags_None,
                        "Between the two radii a whole partition moves with one record (WRITE_PARTITION_TRANSLATION)");
        PE::SliderFloat("Instance bob", &m_instBob, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_None, "Per-instance amplitude in the NEAR zone");
        PE::SliderFloat("Partition bob", &m_partBob, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_None, "Per-partition amplitude in the MID zone");
        PE::SliderFloat("Bob speed", &m_bobSpeed, 0.0f, 6.0f);
        PE::DragFloat3("Light direction", &m_lightDir.x, 0.01f);
        PE::end();

        ImGui::SeparatorText("Build ops this frame");
        ImGui::BulletText("WRITE_INSTANCE : %d  (NEAR zone, per instance)", m_lastInstanceOps);
        ImGui::BulletText("UPDATE_INSTANCE : %d  (LOD swaps on zone crossings)", m_lastUpdateOps);
        ImGui::BulletText("WRITE_PARTITION_TRANSLATION : %d  (MID zone, per partition)", m_lastPartOps);
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Two radii carve the grid into three zones, each with its own operation and level of detail. NEAR the "
            "camera each sphere is re-specified so it can ripple on its own (high-poly); in the MID ring a whole "
            "partition of 16 instances moves with a single record (medium); FAR instances are retained and cost "
            "nothing (coarse). Orbit the camera: instances crossing a boundary swap LOD with UPDATE_INSTANCE. A "
            "TLAS has no middle ground - to move anything it reprocesses all %d instances every frame.",
            kNumInstances);
      }
      else
      {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Extension VK_NV_partitioned_acceleration_structure");
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "is NOT supported on this device!");
      }

      if(ImGui::CollapsingHeader("Tonemapper"))
        nvgui::tonemapperWidget(m_tonemapperData);
    }
    ImGui::End();
  }

  //-------------------------------------------------------------------------------
  // Scene: three sphere LODs + a ground plane, one BLAS each. RtBase::createBottomLevelAS builds one
  // BLAS per mesh; a per-frame update points each instance at the LOD BLAS for its zone.
  //-------------------------------------------------------------------------------
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Properties (scratch alignment + partition limit)
    m_asProp.pNext   = &m_partProp;
    m_partProp.pNext = nullptr;
    VkPhysicalDeviceProperties2 prop2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &m_asProp};
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);

    // Four BLAS in blasSet order: three sphere levels of detail (high / medium / coarse) then the plane.
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, nvutils::createSphereUv(kRadiusS, 48, 48));  // eMeshLodHigh
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, nvutils::createSphereUv(kRadiusS, 5, 5));  // eMeshLodMed
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, nvutils::createSphereUv(kRadiusS, 3, 2));  // eMeshLodLow
    const float planeHalf = 0.5f * kGridN * kSpacing + kSpacing;
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader,
                                       nvutils::createPlane(1, 2.0f * planeHalf, 2.0f * planeHalf));  // eMeshPlane

    // Record each instance's partition (grid spheres tiled kTile x kTile; the plane is global).
    m_instancePartition.resize(kNumInstances);
    for(int i = 0; i < kNumInstances; i++)
    {
      const bool isPlane     = (i == kNumSpheres);
      m_instancePartition[i] = isPlane ? -1 : (i / kGridN / kTile) * kPartAxis + (i % kGridN / kTile);
    }

    // Camera-only scene-info UBO (RtBase::updateSceneBuffer fills projInv/viewInv each frame). The
    // shader shades spheres from the object-space hit position, so no mesh/material buffers are needed.
    NVVK_CHECK(m_allocator.createBuffer(m_sceneResource.bSceneInfo, sizeof(shaderio::GltfSceneInfo),
                                        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT));
    NVVK_DBG_NAME(m_sceneResource.bSceneInfo.buffer);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    m_stagingUploader.cmdUploadAppended(cmd);  // upload the mesh geometry (BLAS input)
    m_app->submitAndWaitTempCmdBuffer(cmd);

    // Look at the grid (from the initial azimuth, so headless captures are deterministic)
    m_cameraManip->setClipPlanes({0.1f, 200.0f});
    applyCamera(m_azimuthDeg);
  }

  // Orbit the camera around the grid center by 'deg' degrees about the Y axis. Moving the camera moves
  // the update zones, which is what makes instances cross boundaries and swap LOD.
  void applyCamera(float deg)
  {
    const glm::vec3 target(0.0f, 1.0f, 0.0f), up(0.0f, 1.0f, 0.0f), baseEye(18.0f, 20.0f, 30.0f);
    const float     a = glm::radians(deg);
    const glm::vec3 r = baseEye - target;
    const glm::vec3 eye(r.x * std::cos(a) + r.z * std::sin(a), r.y, -r.x * std::sin(a) + r.z * std::cos(a));
    m_cameraManip->setLookat(target + eye, target, up);
  }

  // Skip RtBase's TLAS; build the PTLAS instead.
  void createTopLevelAS() override
  {
    if(!m_extensionSupported)
      return;
    if(uint32_t(kPartitions) > m_partProp.maxPartitionCount)
    {
      LOGW("partitionCount %d exceeds device maxPartitionCount %u; disabling PTLAS path.\n", kPartitions, m_partProp.maxPartitionCount);
      m_extensionSupported = false;
      return;
    }
    allocatePtlasBuffers();
    buildPtlas();  // from scratch (src == 0)
  }

  //-------------------------------------------------------------------------------
  // Per-frame: apply the cheap delta (partition translations + instance BLAS swaps)
  //-------------------------------------------------------------------------------
  void onRender(VkCommandBuffer cmd) override
  {
    if(m_extensionSupported)
    {
      if(m_play)
        m_time += ImGui::GetCurrentContext() ? ImGui::GetIO().DeltaTime : 0.016f;
      // Optional slow orbit (used for figure capture / to demonstrate LOD swaps): move the camera and
      // the zones move with it, so instances cross boundaries and swap LOD via UPDATE_INSTANCE.
      if(m_orbit)
        applyCamera(m_azimuthDeg + kOrbitDegPerSec * m_time);
      // Record the incremental PTLAS update on the frame command buffer (like the other per-frame
      // acceleration-structure samples). updatePtlas() submits only the ops that changed (none when
      // the scene is static); RtBase::onRender then records the trace, after the barrier below.
      updatePtlas(cmd);
    }
    RtBase::onRender(cmd);
  }

  //-------------------------------------------------------------------------------
  // Ray tracing pipeline (standard RT pipeline; PTLAS needs no special pNext)
  //-------------------------------------------------------------------------------
  void createRaytraceDescriptorLayout() override
  {
    SCOPED_TIMER(__FUNCTION__);
    // A PTLAS is not bound as a descriptor: it is referenced by device address (passed in the
    // push constant and converted to an acceleration structure in the shader). So the ray tracing
    // set only needs the output image.
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eOutImage,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});
    m_rtDescPack.init(m_rtBindings, m_app->getDevice(), 0, VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  void createRayTracingPipeline() override
  {
    destroyRayTracingPipeline();

    if(!m_extensionSupported)
      return;

    VkShaderModuleCreateInfo shaderCode = compileSlangShader("partition_tlas.slang", partition_tlas_slang);

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

    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader       = VK_SHADER_UNUSED_KHR;
    group.closestHitShader   = VK_SHADER_UNUSED_KHR;
    group.generalShader      = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shaderGroups.push_back(group);
    group.generalShader = eMiss;
    shaderGroups.push_back(group);
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);

    const VkPushConstantRange        pushRange{VK_SHADER_STAGE_ALL, 0, sizeof(shaderio::TutoPushConstant)};
    const VkDescriptorSetLayout      setLayout = m_rtDescPack.getLayout();
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushRange,
    };
    NVVK_CHECK(vkCreatePipelineLayout(m_app->getDevice(), &pipelineLayoutInfo, nullptr, &m_rtPipelineLayout));
    NVVK_DBG_NAME(m_rtPipelineLayout);

    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount                   = uint32_t(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = uint32_t(shaderGroups.size()),
        .pGroups                      = shaderGroups.data(),
        .maxPipelineRayRecursionDepth = 1,
        .layout                       = m_rtPipelineLayout,
    };
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    createShaderBindingTable(rtPipelineInfo);
  }

  void raytraceScene(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);
    if(!m_extensionSupported || m_rtPipeline == VK_NULL_HANDLE)
      return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);

    // Only the output image is a descriptor; the PTLAS is passed by device address (below).
    VkDescriptorImageInfo imgInfo{.imageView = m_gBuffers.getColorImageView(eImgRendered), .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writeImg{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = shaderio::BindingPoints::eOutImage,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &imgInfo,
    };
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 0, 1, &writeImg);

    const glm::vec3 eye           = m_cameraManip->getEye();
    m_pushValues.sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;
    m_pushValues.tlasAddress      = m_ptlasAddr;  // PTLAS bound by device address
    m_pushValues.lightDir         = m_lightDir;
    m_pushValues.cameraXZ         = glm::vec2(eye.x, eye.z);  // for per-zone shading in the shader
    m_pushValues.nearRadius       = m_nearRadius;
    m_pushValues.midRadius        = m_midRadius;
    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &m_pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D&                  size    = m_app->getViewportSize();
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable, size.width, size.height, 1);

    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  void sampleDestroy() override
  {
    m_allocator.destroyBuffer(m_ptlasBuffer);
    for(FrameRes& f : m_frames)
    {
      m_allocator.destroyBuffer(f.scratch);
      m_allocator.destroyBuffer(f.ops);
      m_allocator.destroyBuffer(f.opsCount);
      m_allocator.destroyBuffer(f.writeInst);
      m_allocator.destroyBuffer(f.updateInst);
      m_allocator.destroyBuffer(f.partTrans);
    }
  }

private:
  //-------------------------------------------------------------------------------
  // World transform of grid instance i (translation only)
  //-------------------------------------------------------------------------------
  glm::mat4 instanceTransform(int i) const
  {
    if(i == kNumSpheres)
      return glm::mat4(1);  // ground plane at the origin (y = 0)
    const int   ix = i % kGridN;
    const int   iz = i / kGridN;
    const float x  = (float(ix) - 0.5f * (kGridN - 1)) * kSpacing;
    const float z  = (float(iz) - 0.5f * (kGridN - 1)) * kSpacing;
    return glm::translate(glm::mat4(1), glm::vec3(x, kSphereY, z));
  }

  // World-space XZ center of partition p (used to test it against the camera streaming radius).
  glm::vec2 partitionCenterXZ(int p) const
  {
    const int   px = p % kPartAxis;
    const int   pz = p / kPartAxis;
    const float x  = ((px * kTile + 0.5f * (kTile - 1)) - 0.5f * (kGridN - 1)) * kSpacing;
    const float z  = ((pz * kTile + 0.5f * (kTile - 1)) - 0.5f * (kGridN - 1)) * kSpacing;
    return glm::vec2(x, z);
  }

  //-------------------------------------------------------------------------------
  // Size and allocate every PTLAS buffer
  //-------------------------------------------------------------------------------
  void allocatePtlasBuffers()
  {
    m_input = VkPartitionedAccelerationStructureInstancesInputNV{
        .sType                             = VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCES_INPUT_NV,
        .pNext                             = &m_inputFlags,
        .flags                             = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .instanceCount                     = uint32_t(kNumInstances),
        .maxInstancePerPartitionCount      = uint32_t(kTile * kTile),
        .partitionCount                    = uint32_t(kPartitions),
        .maxInstanceInGlobalPartitionCount = 1,
    };
    m_inputFlags = VkPartitionedAccelerationStructureFlagsNV{
        .sType                      = VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_FLAGS_NV,
        .enablePartitionTranslation = VK_TRUE,
    };

    VkAccelerationStructureBuildSizesInfoKHR sizes{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetPartitionedAccelerationStructuresBuildSizesNV(m_app->getDevice(), &m_input, &sizes);

    // The PTLAS is both the destination and (on src == dst updates) a build input, so it needs the
    // read-only build-input usage in addition to acceleration-structure storage.
    const VkBufferUsageFlags2 asStorage = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                          | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                          | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
    const VkBufferUsageFlags2 argUsage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                         | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    const VmaAllocationCreateFlags hostFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    NVVK_CHECK(m_allocator.createBuffer(m_ptlasBuffer, sizes.accelerationStructureSize, asStorage));
    m_ptlasAddr = m_ptlasBuffer.address;
    NVVK_DBG_NAME(m_ptlasBuffer.buffer);

    // The update is recorded on the frame command buffer, so the scratch and the host-mapped
    // argument buffers are ring-buffered per frame-in-flight: the CPU never rewrites a buffer that
    // an in-flight frame's build is still reading.
    const VkBufferUsageFlags2 scratchUsage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                             | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    m_frames.resize(m_app->getFrameCycleSize());
    for(FrameRes& f : m_frames)
    {
      NVVK_CHECK(m_allocator.createBuffer(f.scratch, std::max(sizes.buildScratchSize, sizes.updateScratchSize), scratchUsage,
                                          VMA_MEMORY_USAGE_AUTO, {}, m_asProp.minAccelerationStructureScratchOffsetAlignment));
      NVVK_CHECK(m_allocator.createBuffer(f.ops, 3 * sizeof(VkBuildPartitionedAccelerationStructureIndirectCommandNV),
                                          argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
      NVVK_CHECK(m_allocator.createBuffer(f.opsCount, sizeof(uint32_t), argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
      NVVK_CHECK(m_allocator.createBuffer(f.writeInst, kNumInstances * sizeof(VkPartitionedAccelerationStructureWriteInstanceDataNV),
                                          argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
      NVVK_CHECK(m_allocator.createBuffer(f.updateInst, kNumInstances * sizeof(VkPartitionedAccelerationStructureUpdateInstanceDataNV),
                                          argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
      NVVK_CHECK(m_allocator.createBuffer(f.partTrans, kPartitions * sizeof(VkPartitionedAccelerationStructureWritePartitionTranslationDataNV),
                                          argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
    }
  }

  // First grid-instance index of partition p (its 16 instances are contiguous per row, kTile per row).
  int partitionInstance(int p, int local) const
  {
    const int px = p % kPartAxis, pz = p / kPartAxis;
    const int ix = px * kTile + (local % kTile), iz = pz * kTile + (local / kTile);
    return iz * kGridN + ix;
  }

  // One WRITE_INSTANCE record for instance i (a sphere at its grid cell, or the ground plane), pointing
  // at the given LOD BLAS (meshIndex), with an extra vertical offset used to animate the NEAR zone.
  VkPartitionedAccelerationStructureWriteInstanceDataNV makeInstance(int i, float yOffset, uint32_t meshIndex) const
  {
    const bool     isPlane = (i == kNumSpheres);
    const uint32_t partition =
        isPlane ? VK_PARTITIONED_ACCELERATION_STRUCTURE_PARTITION_INDEX_GLOBAL_NV : uint32_t(m_instancePartition[i]);

    VkPartitionedAccelerationStructureWriteInstanceDataNV w{};
    w.transform    = nvvk::toTransformMatrixKHR(glm::translate(instanceTransform(i), glm::vec3(0, yOffset, 0)));
    w.instanceID   = isPlane ? PT_GROUND_ID : uint32_t(m_instancePartition[i]);
    w.instanceMask = 0xFF;
    w.instanceContributionToHitGroupIndex = 0;
    w.instanceFlags         = VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_TRIANGLE_FACING_CULL_DISABLE_BIT_NV;
    w.instanceIndex         = uint32_t(i);
    w.partitionIndex        = partition;
    w.accelerationStructure = m_asBuilder.blasSet[meshIndex].address;  // BLAS = LOD for this instance's zone
    return w;
  }

  //-------------------------------------------------------------------------------
  // Fill one frame's argument buffers with the *delta* to apply this frame, and return the op count.
  // The two camera radii split the grid into three zones (see zoneOf), each with its own operation and
  // its own level of detail (LOD = zone):
  //   * NEAR -> WRITE_INSTANCE for every instance: each sphere ripples on its own, at the HIGH LOD.
  //   * MID  -> WRITE_PARTITION_TRANSLATION per partition: the whole 16-instance tile rocks as one, at
  //             the MEDIUM LOD.
  //   * FAR  -> nothing: the instances are retained by the src == dst build (zero ops), at the LOW LOD.
  // When a partition crosses a MID<->FAR boundary, its instances are re-pointed at the new LOD BLAS with
  // UPDATE_INSTANCE (a cheap BLAS swap, no transform). NEAR crossings are handled by the WRITE_INSTANCE
  // path above, which also carries the BLAS. Everything is delta-tracked, so a static frame emits zero.
  //-------------------------------------------------------------------------------
  uint32_t fillBuildOps(FrameRes& fr)
  {
    auto*    ops     = reinterpret_cast<VkBuildPartitionedAccelerationStructureIndirectCommandNV*>(fr.ops.mapping);
    uint32_t opCount = 0;

    const glm::vec3 eye   = m_cameraManip->getEye();
    const glm::vec2 camXZ = glm::vec2(eye.x, eye.z);

    // ---- WRITE_INSTANCE: the NEAR zone (+ the whole scene on the first build) ----
    auto*    wi      = reinterpret_cast<VkPartitionedAccelerationStructureWriteInstanceDataNV*>(fr.writeInst.mapping);
    uint32_t numInst = 0;
    for(int i = 0; i < kNumInstances; i++)
    {
      const bool isPlane = (i == kNumSpheres);
      const int  p       = isPlane ? -1 : m_instancePartition[i];
      const Zone zone    = isPlane ? eFar : zoneOf(p, camXZ);
      const bool isNear  = (zone == eNear);

      if(m_initialBuilt)
      {
        // After the first build, re-specify only the NEAR instances - and partitions that just left the
        // NEAR zone, once, to settle their spheres back to rest (and to the new LOD). Rest is retained.
        if(isPlane || (!isNear && m_wasNear[p] == 0))
          continue;
      }

      float yOff = 0.0f;
      if(isNear)
      {
        const float phase = 0.5f * float((i % kGridN) + (i / kGridN));  // per-instance -> a travelling ripple
        yOff              = m_instBob * std::sin(m_time * m_bobSpeed + phase);
      }
      const uint32_t mesh = isPlane ? uint32_t(eMeshPlane) : lodMesh(zone);
      wi[numInst++]       = makeInstance(i, yOff, mesh);
    }
    if(numInst > 0)
    {
      m_allocator.autoFlushBuffer(fr.writeInst);
      ops[opCount].opType   = VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_INSTANCE_NV;
      ops[opCount].argCount = numInst;
      ops[opCount].argData  = {.startAddress  = fr.writeInst.address,
                               .strideInBytes = sizeof(VkPartitionedAccelerationStructureWriteInstanceDataNV)};
      opCount++;
    }
    m_lastInstanceOps = int(numInst);

    // ---- UPDATE_INSTANCE: LOD swaps for partitions that crossed a MID<->FAR boundary ----
    // (NEAR crossings already went through WRITE_INSTANCE above, which sets the BLAS too.)
    auto*    ui = reinterpret_cast<VkPartitionedAccelerationStructureUpdateInstanceDataNV*>(fr.updateInst.mapping);
    uint32_t numUpdate = 0;
    if(m_initialBuilt)
    {
      for(int p = 0; p < kPartitions; p++)
      {
        const Zone zone           = zoneOf(p, camXZ);
        const bool touchedByWrite = (zone == eNear) || (m_wasNear[p] != 0);
        if(touchedByWrite || zone == m_prevZone[p])
          continue;  // handled elsewhere, or LOD unchanged -> no op
        const VkDeviceAddress blas = m_asBuilder.blasSet[lodMesh(zone)].address;
        for(int local = 0; local < kTile * kTile; local++)
        {
          ui[numUpdate].instanceIndex                       = uint32_t(partitionInstance(p, local));
          ui[numUpdate].instanceContributionToHitGroupIndex = 0;
          ui[numUpdate].accelerationStructure               = blas;
          numUpdate++;
        }
      }
    }
    if(numUpdate > 0)
    {
      m_allocator.autoFlushBuffer(fr.updateInst);
      ops[opCount].opType   = VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_UPDATE_INSTANCE_NV;
      ops[opCount].argCount = numUpdate;
      ops[opCount].argData  = {.startAddress  = fr.updateInst.address,
                               .strideInBytes = sizeof(VkPartitionedAccelerationStructureUpdateInstanceDataNV)};
      opCount++;
    }
    m_lastUpdateOps = int(numUpdate);

    for(int p = 0; p < kPartitions; p++)
    {
      const Zone zone = zoneOf(p, camXZ);
      m_wasNear[p]    = (zone == eNear) ? 1 : 0;
      m_prevZone[p]   = zone;
    }

    // ---- WRITE_PARTITION_TRANSLATION: the MID zone (one record moves a whole partition) ----
    auto* pt = reinterpret_cast<VkPartitionedAccelerationStructureWritePartitionTranslationDataNV*>(fr.partTrans.mapping);
    uint32_t numMoved = 0;
    for(int p = 0; p < kPartitions; p++)
    {
      // MID partitions rock as a rigid tile; NEAR/FAR partitions target 0 (NEAR moves its instances
      // individually instead, FAR does not move). The first build emits no translations at all (it is a
      // pure WRITE_INSTANCE); afterwards a record is emitted only when the offset actually changes, so
      // entering/leaving the MID ring costs exactly one settle op and a static frame emits nothing.
      const float phase = 0.7f * float((p % kPartAxis) + (p / kPartAxis));
      const float y     = (zoneOf(p, camXZ) == eMid) ? (m_partBob * std::sin(m_time * m_bobSpeed + phase)) : 0.0f;
      if(!m_initialBuilt || std::fabs(y - m_prevPartY[p]) < 1e-4f)
        continue;
      m_prevPartY[p]                       = y;
      pt[numMoved].partitionIndex          = uint32_t(p);
      pt[numMoved].partitionTranslation[0] = 0.0f;
      pt[numMoved].partitionTranslation[1] = y;
      pt[numMoved].partitionTranslation[2] = 0.0f;
      numMoved++;
    }
    if(numMoved > 0)
    {
      m_allocator.autoFlushBuffer(fr.partTrans);
      ops[opCount].opType   = VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_PARTITION_TRANSLATION_NV;
      ops[opCount].argCount = numMoved;
      ops[opCount].argData = {.startAddress = fr.partTrans.address,
                              .strideInBytes = sizeof(VkPartitionedAccelerationStructureWritePartitionTranslationDataNV)};
      opCount++;
    }
    m_lastPartOps = int(numMoved);

    *reinterpret_cast<uint32_t*>(fr.opsCount.mapping) = opCount;
    m_allocator.autoFlushBuffer(fr.ops);
    m_allocator.autoFlushBuffer(fr.opsCount);
    m_initialBuilt = true;
    return opCount;
  }

  //-------------------------------------------------------------------------------
  // Initial build (src == 0), recorded once on a temporary command buffer during onAttach.
  //-------------------------------------------------------------------------------
  void buildPtlas()
  {
    SCOPED_TIMER(__FUNCTION__);
    m_prevPartY.assign(kPartitions, 0.0f);
    m_wasNear.assign(kPartitions, 0);
    m_prevZone.assign(kPartitions, eFar);
    FrameRes& fr = m_frames[0];
    fillBuildOps(fr);
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    recordBuild(cmd, fr, /*update=*/false);
    m_app->submitAndWaitTempCmdBuffer(cmd);
  }

  //-------------------------------------------------------------------------------
  // Per-frame incremental update (src == dst): applies only the partitions that moved. If nothing
  // moved, no build is recorded at all.
  //-------------------------------------------------------------------------------
  void updatePtlas(VkCommandBuffer cmd)
  {
    FrameRes& fr = m_frames[m_app->getFrameCycleIndex()];
    if(fillBuildOps(fr) == 0)
      return;
    recordBuild(cmd, fr, /*update=*/true);
  }

  //-------------------------------------------------------------------------------
  // Record the PTLAS build into cmd (no submit). update==true reuses the structure as its own
  // source (src == dst), applying the delta ops in place; the barrier makes the result visible to
  // the trace recorded later on the same command buffer.
  //-------------------------------------------------------------------------------
  void recordBuild(VkCommandBuffer cmd, const FrameRes& fr, bool update)
  {
    VkBuildPartitionedAccelerationStructureInfoNV buildInfo{
        .sType                        = VK_STRUCTURE_TYPE_BUILD_PARTITIONED_ACCELERATION_STRUCTURE_INFO_NV,
        .input                        = m_input,
        .srcAccelerationStructureData = update ? m_ptlasAddr : 0,
        .dstAccelerationStructureData = m_ptlasAddr,
        .scratchData                  = fr.scratch.address,
        .srcInfos                     = fr.ops.address,
        .srcInfosCount                = fr.opsCount.address,
    };

    // The cluster/partitioned build touches several stages (indirect op-count read, transfer/host
    // argument reads, acceleration-structure build) and its result is consumed by the ray tracing
    // shaders. Wrap it in a full barrier before and after, matching vk_forest_ray_tracing - narrower
    // barriers miss a dependency and let the trace read a partially-built structure (device lost).
    fullMemoryBarrier(cmd);
    vkCmdBuildPartitionedAccelerationStructuresNV(cmd, &buildInfo);
    fullMemoryBarrier(cmd);
  }

  // Full pipeline barrier over all commands (shader/transfer/indirect/acceleration-structure access).
  static void fullMemoryBarrier(VkCommandBuffer cmd)
  {
    const VkAccessFlags access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT
                                 | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                                 | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    VkMemoryBarrier     mb{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = access, .dstAccessMask = access};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);
  }

  //-------------------------------------------------------------------------------
  // State
  //-------------------------------------------------------------------------------
  bool m_extensionSupported = false;

  // Timeline (play, or scrub the "Time" slider when paused)
  bool  m_play = true;
  float m_time = 0.0f;

  // Camera framing / optional orbit (deterministic figure capture; also demonstrates LOD swaps)
  static constexpr float kOrbitDegPerSec = 12.0f;
  float                  m_azimuthDeg    = 0.0f;
  bool                   m_orbit         = false;

  // Two radii around the camera carve the grid into three zones (a level-of-detail idea):
  //   * inside  m_nearRadius            -> NEAR: each instance is re-specified every frame (WRITE_INSTANCE),
  //                                        so its sphere can move individually (a fine per-instance ripple);
  //   * between m_nearRadius and m_midRadius -> MID: the whole partition is shifted with ONE record
  //                                        (WRITE_PARTITION_TRANSLATION), moving all its instances together;
  //   * beyond  m_midRadius             -> FAR: nothing is submitted; the instances are retained as-is.
  float m_nearRadius = 22.0f;  // NEAR zone: per-instance updates
  float m_midRadius  = 41.0f;  // MID zone: per-partition translation
  float m_instBob    = 1.2f;   // per-instance bob amplitude (NEAR)
  float m_partBob    = 0.9f;   // per-partition bob amplitude (MID)
  float m_bobSpeed   = 2.0f;

  Zone zoneOf(int p, const glm::vec2& camXZ) const
  {
    const float d = glm::distance(partitionCenterXZ(p), camXZ);
    return d < m_nearRadius ? eNear : (d < m_midRadius ? eMid : eFar);
  }

  std::vector<int>     m_instancePartition;        // per-instance partition (grid spheres)
  std::vector<float>   m_prevPartY;                // last translation applied to each partition (delta tracking)
  std::vector<uint8_t> m_wasNear;                  // whether each partition was in the NEAR zone last frame
  std::vector<int>     m_prevZone;                 // each partition's zone last frame (LOD-swap delta tracking)
  bool                 m_initialBuilt    = false;  // instances are written on the first build, then retained
  int                  m_lastInstanceOps = 0;      // WRITE_INSTANCE records submitted last frame
  int                  m_lastUpdateOps   = 0;      // UPDATE_INSTANCE records submitted last frame (LOD swaps)
  int                  m_lastPartOps     = 0;      // WRITE_PARTITION_TRANSLATION records submitted last frame

  glm::vec3 m_lightDir{glm::normalize(glm::vec3(0.4f, 1.0f, 0.5f))};

  // PTLAS resources
  VkPartitionedAccelerationStructureInstancesInputNV m_input{};
  VkPartitionedAccelerationStructureFlagsNV          m_inputFlags{};
  nvvk::Buffer                                       m_ptlasBuffer{};  // PTLAS storage (dst / src)
  std::vector<FrameRes>                              m_frames;         // ring, one per frame-in-flight
  VkDeviceAddress                                    m_ptlasAddr = 0;  // PTLAS device address (passed to the shader)

  VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV m_partProp{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV};
};


//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  float                      startTime = 0.0f;
  bool                       play      = true;
  float                      azimuth   = 0.0f;
  bool                       orbit     = false;
  nvutils::ParameterParser   cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  reg.add({"frames", "Number of frames to render in headless mode"}, &appInfo.headlessFrameCount);
  reg.add({"time", "Timeline position in seconds (for deterministic headless capture)"}, &startTime);
  reg.add({"play", "Advance the timeline automatically"}, &play);
  reg.add({"azimuth", "Initial camera azimuth in degrees (deterministic capture)"}, &azimuth);
  reg.add({"orbit", "Slowly orbit the camera (demonstrates LOD swaps via UPDATE_INSTANCE)"}, &orbit);
  cli.add(reg);
  cli.parse(argc, argv);

  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV partitionedFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},
              {VK_NV_PARTITIONED_ACCELERATION_STRUCTURE_EXTENSION_NAME, &partitionedFeature, false},  // optional
          },
  };
  if(!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  auto vkContext = RtBase::createVulkanContext(vkSetup);
  if(!vkContext)
    return 1;

  appInfo.name           = "Ray Tracing Tutorial - 22 Partitioned TLAS";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  nvapp::Application application;
  application.init(appInfo);

  auto tutorial    = std::make_shared<RtPartitionTlas>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  const bool extensionSupported = (partitionedFeature.partitionedAccelerationStructure == VK_TRUE);
  tutorial->setExtensionSupport(extensionSupported);
  tutorial->setInitialTime(startTime);
  tutorial->setPlay(play);
  tutorial->setAzimuth(azimuth);
  tutorial->setOrbit(orbit);

  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();
  application.deinit();
  vkContext->deinit();

  return 0;
}
