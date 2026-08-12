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
// Ray Tracing Tutorial - 21 Clusters (CLAS)
//
// This sample demonstrates the VK_NV_cluster_acceleration_structure extension. A unit
// sphere is generated at several levels of detail; each level is split into small
// triangle patches (clusters). At startup we build ALL the clusters of ALL levels into
// CLAS once, plus one "cluster" bottom-level AS per level (each referencing that level's
// CLAS). Every frame we pick a level from the camera distance (or a manual override) and
// refit a single-instance top-level AS to point at that level's cluster BLAS. On a hit,
// the closest-hit shader reads the per-hit cluster ID and colorizes the surface.
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
#include <cassert>
#include <span>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/clusters.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

#include <nvvk/barriers.hpp>


//--------------------------------------------------------------------------------------------------
// Ray tracing a clustered sphere with cluster-based LOD
//
class RtClusters : public RtBase
{
  // Description of one cluster inside the concatenated vertex/index buffers
  struct ClusterInfo
  {
    uint32_t vertexByteOffset{};  // byte offset of the cluster's first vertex in m_vertexBuffer
    uint32_t indexByteOffset{};   // byte offset of the cluster's first index in m_indexBuffer (8-bit indices)
    uint32_t vertexCount{};       // number of local vertices
    uint32_t triangleCount{};     // number of triangles
  };

  // One level of detail: a contiguous range of clusters and the BLAS built from them
  struct Lod
  {
    uint32_t        clasOffset{};    // index of the level's first cluster in the global CLAS array
    uint32_t        clusterCount{};  // number of clusters at this level
    VkDeviceAddress blasAddress{};   // device address of the cluster BLAS for this level
  };

  // Sphere resolution per level (latitude x longitude subdivisions). Cluster patch is 4x4 cells,
  // so the number of clusters is (rings/4) * (sectors/4): 8, 32, 128, 512.
  static constexpr int        kPatch                = 4;
  static constexpr int        kNumLevels            = 4;
  static constexpr glm::ivec2 kLevelRes[kNumLevels] = {{8, 16}, {16, 32}, {32, 64}, {64, 128}};

public:
  RtClusters()           = default;
  ~RtClusters() override = default;

  // Set from main() after querying the device
  void setExtensionSupport(bool supported) { m_extensionSupported = supported; }
  void setInitialLod(int lod) { m_lodOverride = lod; }
  void setColorByCluster(int on) { m_colorByCluster = on; }

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;

    // Viewport: the ray-traced (tonemapped) image
    if(ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(eImgTonemapped)), ImGui::GetContentRegionAvail());
    }
    ImGui::End();

    // Settings: only the controls relevant to this chapter. The sample uses flat, per-cluster
    // shading with a single directional light and carries no materials or normal buffers, so the
    // base class' Sky/lighting and metallic-roughness controls do not apply here and are omitted.
    if(ImGui::Begin("Settings"))
    {
      if(ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_cameraManip);

      ImGui::SeparatorText("Cluster Acceleration Structures");
      if(m_extensionSupported)
      {
        ImGui::Text("Camera distance: %.2f", m_cameraManip->getDistanceToCenter());
        ImGui::Text("Active level: %d  (%u clusters)", m_currentLevel, m_numClusters);

        const char* lodItems[1 + kNumLevels] = {"Auto", "Level 0", "Level 1", "Level 2", "Level 3"};
        int         comboIndex               = m_lodOverride + 1;  // -1 (auto) -> 0
        PE::begin();
        if(PE::Combo("LOD selection", &comboIndex, lodItems, 1 + kNumLevels))
          m_lodOverride = comboIndex - 1;
        bool colorByCluster = m_colorByCluster != 0;
        if(PE::Checkbox("Color by cluster", &colorByCluster))
          m_colorByCluster = colorByCluster ? 1 : 0;
        PE::DragFloat3("Light direction", &m_lightDir.x, 0.01F);
        PE::end();
      }
      else
      {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Extension VK_NV_cluster_acceleration_structure");
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "is NOT supported on this device!");
      }

      if(ImGui::CollapsingHeader("Tonemapper"))
        nvgui::tonemapperWidget(m_tonemapperData);
    }
    ImGui::End();
  }

  //--------------------------------------------------------------------------------------------------
  // Generate the procedural sphere (all LOD levels), upload geometry, create the (camera-only)
  // scene-info buffer. No glTF is used, but RtBase still needs the scene-info UBO for the camera.
  //
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Query acceleration-structure and cluster properties (alignments and per-cluster limits)
    m_asProp.pNext      = &m_clusterProp;
    m_clusterProp.pNext = nullptr;
    VkPhysicalDeviceProperties2 prop2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &m_asProp};
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);

    // Generate every LOD level of the sphere
    for(int level = 0; level < kNumLevels; level++)
    {
      Lod lod;
      lod.clasOffset   = uint32_t(m_clusters.size());
      lod.clusterCount = appendSphereLevel(kLevelRes[level].x, kLevelRes[level].y);
      m_lods.push_back(lod);
      LOGI("LOD %d: %u clusters\n", level, lod.clusterCount);
    }

    if(m_extensionSupported)
    {
      // Per-cluster hardware limits (8-bit indices => <= 256 vertices). A 4x4 patch stays well within
      // real-hardware limits, but enforce them at runtime (not only via debug asserts) so a device
      // reporting smaller limits disables the cluster path instead of building invalid CLAS.
      if(m_maxClusterVertices > m_clusterProp.maxVerticesPerCluster || m_maxClusterTriangles > m_clusterProp.maxTrianglesPerCluster)
      {
        LOGW("Clusters exceed device limits (vertices %u/%u, triangles %u/%u); disabling the cluster path.\n", m_maxClusterVertices,
             m_clusterProp.maxVerticesPerCluster, m_maxClusterTriangles, m_clusterProp.maxTrianglesPerCluster);
        m_extensionSupported = false;  // cascades: BLAS/TLAS/pipeline build steps skip, UI shows "not supported"
      }
    }

    // Upload the concatenated geometry
    const VkBufferUsageFlags2 asInputUsage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                             | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    NVVK_CHECK(m_allocator.createBuffer(m_vertexBuffer, std::span(m_vertices).size_bytes(), asInputUsage));
    NVVK_CHECK(m_allocator.createBuffer(m_indexBuffer, std::span(m_indices).size_bytes(), asInputUsage));
    NVVK_DBG_NAME(m_vertexBuffer.buffer);
    NVVK_DBG_NAME(m_indexBuffer.buffer);
    NVVK_CHECK(m_stagingUploader.appendBuffer(m_vertexBuffer, 0, std::span(m_vertices)));
    NVVK_CHECK(m_stagingUploader.appendBuffer(m_indexBuffer, 0, std::span(m_indices)));

    // Camera-only scene-info UBO (RtBase::updateSceneBuffer fills projInv/viewInv each frame)
    NVVK_CHECK(m_allocator.createBuffer(m_sceneResource.bSceneInfo, sizeof(shaderio::GltfSceneInfo),
                                        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT));
    NVVK_DBG_NAME(m_sceneResource.bSceneInfo.buffer);

    m_stagingUploader.cmdUploadAppended(cmd);
    m_app->submitAndWaitTempCmdBuffer(cmd);

    // Frame the sphere; the starting distance selects the initial LOD (see selectLod)
    m_cameraManip->setLookat({2.5F, 2.0F, 3.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
  }

  //--------------------------------------------------------------------------------------------------
  // Instead of a triangle BLAS, build CLAS (one per cluster) and one cluster BLAS per level.
  //
  void createBottomLevelAS() override
  {
    if(!m_extensionSupported)
      return;
    buildClusterAccelerationStructures();
  }

  //--------------------------------------------------------------------------------------------------
  // Build the single-instance TLAS for the starting level (with ALLOW_UPDATE so LOD switches refit).
  //
  void createTopLevelAS() override
  {
    if(!m_extensionSupported)
      return;
    setLevel(selectLod());
  }

  //--------------------------------------------------------------------------------------------------
  // Switch LOD (cheap TLAS refit) before the base class records the frame.
  //
  void onRender(VkCommandBuffer cmd) override
  {
    if(m_extensionSupported)
    {
      const int level = selectLod();
      if(level != m_currentLevel)
        setLevel(level);
    }
    RtBase::onRender(cmd);
  }

  //--------------------------------------------------------------------------------------------------
  // Ray tracing pipeline: rgen / rmiss / rchit + SBT. The cluster-ID built-in is only valid when the
  // pipeline is created with the cluster pNext.
  //
  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // clusters.slang declares the cluster SPIR-V capability, and the pipeline needs the cluster
    // pNext; both are invalid without the extension. Skip pipeline creation on unsupported devices
    // so RtBase::onAttach() does not fail - m_rtPipeline stays null and raytraceScene() will not trace.
    if(!m_extensionSupported)
      return;

    // Compile shader, and if that fails, use the pre-compiled shader
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("clusters.slang", clusters_slang);

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
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shaderGroups.push_back(group);
    group.generalShader = eMiss;
    shaderGroups.push_back(group);
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);

    // Pipeline layout: a single ray-tracing descriptor set (TLAS + output image) + push constant
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

    // Enable cluster acceleration structures for this pipeline (required for the cluster-ID built-in)
    VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV clusterPipeInfo{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
        .allowClusterAccelerationStructure = VK_TRUE,
    };

    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .pNext                        = &clusterPipeInfo,  // reached only when the extension is supported
        .stageCount                   = uint32_t(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = uint32_t(shaderGroups.size()),
        .pGroups                      = shaderGroups.data(),
        .maxPipelineRayRecursionDepth = 1,
        .layout                       = m_rtPipelineLayout,
    };
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  //--------------------------------------------------------------------------------------------------
  // Ray trace: bind our single-instance TLAS and output image, push the cluster parameters, trace.
  //
  void raytraceScene(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);

    if(!m_extensionSupported || m_tlas.accel == VK_NULL_HANDLE || m_rtPipeline == VK_NULL_HANDLE)
      return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);

    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_tlas);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage), m_gBuffers.getColorImageView(eImgRendered),
                 VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 0, write.size(), write.data());

    m_pushValues.sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;
    m_pushValues.lightDir         = m_lightDir;
    m_pushValues.colorByCluster   = m_colorByCluster;
    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &m_pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D&                  size    = m_app->getViewportSize();
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable, size.width, size.height, 1);

    // Barrier to make sure the image is ready for tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  //--------------------------------------------------------------------------------------------------
  // Destroy the resources created by this sample (the base class destroys the rest).
  //
  void sampleDestroy() override
  {
    m_allocator.destroyBuffer(m_vertexBuffer);
    m_allocator.destroyBuffer(m_indexBuffer);
    m_allocator.destroyBuffer(m_clasBuffer);
    m_allocator.destroyBuffer(m_clasAddressBuffer);
    m_allocator.destroyBuffer(m_clusterBlasBuffer);
    m_allocator.destroyBuffer(m_instancesBuffer);
    m_allocator.destroyBuffer(m_tlasScratch);
    m_allocator.destroyAcceleration(m_tlas);
  }

private:
  //--------------------------------------------------------------------------------------------------
  // Choose a level of detail: manual override if set, otherwise from the camera distance.
  //
  int selectLod() const
  {
    if(m_lodOverride >= 0)
      return std::min(m_lodOverride, kNumLevels - 1);

    const double d = m_cameraManip->getDistanceToCenter();
    if(d > 6.0)
      return 0;
    if(d > 4.0)
      return 1;
    if(d > 2.5)
      return 2;
    return 3;
  }

  //--------------------------------------------------------------------------------------------------
  // Append one sphere LOD level to the global vertex/index/cluster arrays. Returns the number of
  // clusters generated. Each 4x4-cell patch becomes one cluster with its own local vertices and
  // 8-bit indices; degenerate triangles at the poles are skipped.
  //
  uint32_t appendSphereLevel(int rings, int sectors)
  {
    const float radius = 1.0F;

    auto spherePos = [&](int i, int j) -> glm::vec3 {
      const float theta = glm::pi<float>() * float(i) / float(rings);           // 0..pi
      const float phi   = 2.0F * glm::pi<float>() * float(j) / float(sectors);  // 0..2pi
      return radius * glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
    };

    const uint32_t clustersBefore = uint32_t(m_clusters.size());

    for(int pi = 0; pi < rings; pi += kPatch)
    {
      for(int pj = 0; pj < sectors; pj += kPatch)
      {
        const int i0 = pi, i1 = std::min(pi + kPatch, rings);
        const int j0 = pj, j1 = std::min(pj + kPatch, sectors);
        const int nCols = (j1 - j0) + 1;

        std::vector<glm::vec3> localVerts;
        for(int i = i0; i <= i1; i++)
          for(int j = j0; j <= j1; j++)
            localVerts.push_back(spherePos(i, j));

        auto localIndex = [&](int i, int j) -> uint8_t { return uint8_t((i - i0) * nCols + (j - j0)); };

        std::vector<uint8_t> localIndices;
        auto                 addTriangle = [&](uint8_t a, uint8_t b, uint8_t c) {
          const float eps = 1e-6F;
          if(glm::distance(localVerts[a], localVerts[b]) < eps || glm::distance(localVerts[b], localVerts[c]) < eps
             || glm::distance(localVerts[a], localVerts[c]) < eps)
            return;
          localIndices.insert(localIndices.end(), {a, b, c});
        };

        for(int i = i0; i < i1; i++)
        {
          for(int j = j0; j < j1; j++)
          {
            const uint8_t a = localIndex(i, j);
            const uint8_t b = localIndex(i + 1, j);
            const uint8_t c = localIndex(i + 1, j + 1);
            const uint8_t d = localIndex(i, j + 1);
            addTriangle(a, b, c);
            addTriangle(a, c, d);
          }
        }

        ClusterInfo cluster{
            .vertexByteOffset = uint32_t(m_vertices.size() * sizeof(glm::vec3)),
            .indexByteOffset  = uint32_t(m_indices.size()),
            .vertexCount      = uint32_t(localVerts.size()),
            .triangleCount    = uint32_t(localIndices.size() / 3),
        };
        m_clusters.push_back(cluster);
        m_maxClusterVertices  = std::max(m_maxClusterVertices, cluster.vertexCount);
        m_maxClusterTriangles = std::max(m_maxClusterTriangles, cluster.triangleCount);

        m_vertices.insert(m_vertices.end(), localVerts.begin(), localVerts.end());
        m_indices.insert(m_indices.end(), localIndices.begin(), localIndices.end());
      }
    }

    return uint32_t(m_clusters.size()) - clustersBefore;
  }

  //--------------------------------------------------------------------------------------------------
  // Build one CLAS per cluster (all levels at once), then one cluster bottom-level AS per level.
  //
  // There are two acceleration structures below the TLAS: a CLAS is a BVH over the *triangles* of a
  // cluster (BUILD_TRIANGLE_CLUSTER); the cluster BLAS is a real BLAS whose *leaves are references to
  // CLAS* (BUILD_CLUSTERS_BOTTOM_LEVEL), i.e. its build input is a list of CLAS addresses, not
  // vertices. Traversal is TLAS -> cluster BLAS -> CLAS -> triangles. The CLAS are built once; a BLAS
  // just gathers pointers, so it is cheap to (re)assemble from any subset of the CLAS pool - which is
  // how per-cluster LOD works. See README.md ("The idea: one extra level of indirection").
  //
  // Both builds use IMPLICIT_DESTINATIONS: the driver sub-allocates the output from one blob and
  // writes the resulting device addresses into an array we provide. The per-CLAS addresses feed
  // straight into the per-level BLAS build, entirely on the GPU.
  //
  void buildClusterAccelerationStructures()
  {
    SCOPED_TIMER(__FUNCTION__);

    const uint32_t numClusters   = uint32_t(m_clusters.size());
    const uint32_t numLevels     = uint32_t(m_lods.size());
    uint32_t       maxLevelCount = 0;
    for(const Lod& lod : m_lods)
      maxLevelCount = std::max(maxLevelCount, lod.clusterCount);

    // ---------- CLAS (triangle-cluster) sizing ----------
    VkClusterAccelerationStructureTriangleClusterInputNV triangleInput{
        .sType                         = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV,
        .vertexFormat                  = VK_FORMAT_R32G32B32_SFLOAT,
        .maxGeometryIndexValue         = 0,
        .maxClusterUniqueGeometryCount = 1,
        .maxClusterTriangleCount       = m_maxClusterTriangles,
        .maxClusterVertexCount         = m_maxClusterVertices,
        .maxTotalTriangleCount         = uint32_t(m_indices.size() / 3),
        .maxTotalVertexCount           = uint32_t(m_vertices.size()),
        .minPositionTruncateBitCount   = 0,
    };
    VkClusterAccelerationStructureInputInfoNV clasInput{
        .sType                         = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV,
        .maxAccelerationStructureCount = numClusters,
        .flags                         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .opType                        = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV,
        .opMode                        = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV,
        .opInput                       = {.pTriangleClusters = &triangleInput},
    };
    VkAccelerationStructureBuildSizesInfoKHR clasSizes{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetClusterAccelerationStructureBuildSizesNV(m_app->getDevice(), &clasInput, &clasSizes);

    // ---------- Cluster BLAS sizing (one BLAS per level) ----------
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{
        .sType                = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV,
        .maxTotalClusterCount = numClusters,
        .maxClusterCountPerAccelerationStructure = maxLevelCount,
    };
    VkClusterAccelerationStructureInputInfoNV blasInputInfo{
        .sType                         = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV,
        .maxAccelerationStructureCount = numLevels,
        .flags                         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .opType                        = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV,
        .opMode                        = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV,
        .opInput                       = {.pClustersBottomLevel = &blasInput},
    };
    VkAccelerationStructureBuildSizesInfoKHR blasSizes{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetClusterAccelerationStructureBuildSizesNV(m_app->getDevice(), &blasInputInfo, &blasSizes);

    // ---------- Allocate output storage, scratch and argument buffers ----------
    const VkBufferUsageFlags2 storageUsage =
        VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

    nvvk::Buffer scratch;
    NVVK_CHECK(m_allocator.createBuffer(scratch, std::max(clasSizes.buildScratchSize, blasSizes.buildScratchSize),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                            | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_clusterProp.clusterScratchByteAlignment));

    NVVK_CHECK(m_allocator.createBuffer(m_clasBuffer, clasSizes.accelerationStructureSize, storageUsage,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_clusterProp.clusterByteAlignment));
    NVVK_CHECK(m_allocator.createBuffer(m_clusterBlasBuffer, blasSizes.accelerationStructureSize, storageUsage,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_clusterProp.clusterBottomLevelByteAlignment));
    NVVK_DBG_NAME(m_clasBuffer.buffer);
    NVVK_DBG_NAME(m_clusterBlasBuffer.buffer);

    NVVK_CHECK(m_allocator.createBuffer(m_clasAddressBuffer, numClusters * sizeof(uint64_t),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));
    NVVK_DBG_NAME(m_clasAddressBuffer.buffer);

    const VkBufferUsageFlags2 argUsage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                         | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    const VmaAllocationCreateFlags hostFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    nvvk::Buffer clasBuildInfos;  // per-cluster VkClusterAccelerationStructureBuildTriangleClusterInfoNV
    nvvk::Buffer clasSizesOut;    // per-CLAS sizes (output, unused)
    nvvk::Buffer blasBuildInfos;  // per-level VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV
    nvvk::Buffer blasAddressOut;  // per-level BLAS device addresses (output)
    nvvk::Buffer blasSizesOut;    // per-level BLAS sizes (output, unused)
    NVVK_CHECK(m_allocator.createBuffer(clasBuildInfos, numClusters * sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV),
                                        argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
    NVVK_CHECK(m_allocator.createBuffer(clasSizesOut, numClusters * sizeof(uint32_t),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_AUTO, hostFlags));
    NVVK_CHECK(m_allocator.createBuffer(blasBuildInfos, numLevels * sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV),
                                        argUsage, VMA_MEMORY_USAGE_AUTO, hostFlags));
    NVVK_CHECK(m_allocator.createBuffer(blasAddressOut, numLevels * sizeof(uint64_t),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_AUTO, hostFlags));
    NVVK_CHECK(m_allocator.createBuffer(blasSizesOut, numLevels * sizeof(uint32_t),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_AUTO, hostFlags));

    // ---------- Fill the per-cluster CLAS build infos ----------
    auto* buildInfos = reinterpret_cast<VkClusterAccelerationStructureBuildTriangleClusterInfoNV*>(clasBuildInfos.mapping);
    for(uint32_t i = 0; i < numClusters; i++)
    {
      const ClusterInfo& c = m_clusters[i];

      VkClusterAccelerationStructureBuildTriangleClusterInfoNV info{};
      info.clusterID                                       = i;  // <-- surfaced to the shader on a hit
      info.triangleCount                                   = c.triangleCount;
      info.vertexCount                                     = c.vertexCount;
      info.indexType                                       = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV;
      info.indexBufferStride                               = 1;
      info.vertexBufferStride                              = uint16_t(sizeof(glm::vec3));
      info.positionTruncateBitCount                        = 0;
      info.baseGeometryIndexAndGeometryFlags.geometryIndex = 0;
      info.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;
      info.indexBuffer                                     = m_indexBuffer.address + c.indexByteOffset;
      info.vertexBuffer                                    = m_vertexBuffer.address + c.vertexByteOffset;
      buildInfos[i]                                        = info;
    }

    // ---------- Fill the per-level cluster-BLAS build infos ----------
    auto* blasInfos = reinterpret_cast<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV*>(blasBuildInfos.mapping);
    for(uint32_t l = 0; l < numLevels; l++)
    {
      blasInfos[l].clusterReferencesCount  = m_lods[l].clusterCount;
      blasInfos[l].clusterReferencesStride = sizeof(uint64_t);
      blasInfos[l].clusterReferences       = m_clasAddressBuffer.address + m_lods[l].clasOffset * sizeof(uint64_t);
    }

    // Make the host-written build infos visible to the GPU (no-op on coherent memory)
    m_allocator.autoFlushBuffer(clasBuildInfos);
    m_allocator.autoFlushBuffer(blasBuildInfos);

    // ---------- Record both builds ----------
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    VkClusterAccelerationStructureCommandsInfoNV clasCmd{
        .sType             = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV,
        .input             = clasInput,
        .dstImplicitData   = m_clasBuffer.address,
        .scratchData       = scratch.address,
        .dstAddressesArray = {.deviceAddress = m_clasAddressBuffer.address,
                              .stride        = sizeof(uint64_t),
                              .size          = m_clasAddressBuffer.bufferSize},
        .dstSizesArray = {.deviceAddress = clasSizesOut.address, .stride = sizeof(uint32_t), .size = clasSizesOut.bufferSize},
        .srcInfosArray = {.deviceAddress = clasBuildInfos.address,
                          .stride        = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV),
                          .size          = clasBuildInfos.bufferSize},
    };
    vkCmdBuildClusterAccelerationStructureIndirectNV(cmd, &clasCmd);

    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    VkClusterAccelerationStructureCommandsInfoNV blasCmd{
        .sType             = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV,
        .input             = blasInputInfo,
        .dstImplicitData   = m_clusterBlasBuffer.address,
        .scratchData       = scratch.address,
        .dstAddressesArray = {.deviceAddress = blasAddressOut.address,
                              .stride        = sizeof(uint64_t),
                              .size          = blasAddressOut.bufferSize},
        .dstSizesArray = {.deviceAddress = blasSizesOut.address, .stride = sizeof(uint32_t), .size = blasSizesOut.bufferSize},
        .srcInfosArray = {.deviceAddress = blasBuildInfos.address,
                          .stride        = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV),
                          .size          = blasBuildInfos.bufferSize},
    };
    vkCmdBuildClusterAccelerationStructureIndirectNV(cmd, &blasCmd);

    m_app->submitAndWaitTempCmdBuffer(cmd);

    // Make the GPU-written BLAS addresses visible to the host (no-op on coherent memory)
    m_allocator.autoInvalidateBuffer(blasAddressOut);

    // Store each level's cluster-BLAS device address (referenced by the TLAS)
    const uint64_t* blasAddresses = reinterpret_cast<uint64_t*>(blasAddressOut.mapping);
    for(uint32_t l = 0; l < numLevels; l++)
      m_lods[l].blasAddress = blasAddresses[l];

    m_allocator.destroyBuffer(scratch);
    m_allocator.destroyBuffer(clasBuildInfos);
    m_allocator.destroyBuffer(clasSizesOut);
    m_allocator.destroyBuffer(blasBuildInfos);
    m_allocator.destroyBuffer(blasAddressOut);
    m_allocator.destroyBuffer(blasSizesOut);
  }

  //--------------------------------------------------------------------------------------------------
  // A single TLAS instance referencing the given cluster BLAS (identity transform).
  //
  VkAccelerationStructureInstanceKHR makeInstance(VkDeviceAddress blasAddress) const
  {
    return VkAccelerationStructureInstanceKHR{
        .transform                              = nvvk::toTransformMatrixKHR(glm::mat4(1)),
        .instanceCustomIndex                    = 0,
        .mask                                   = 0xFF,
        .instanceShaderBindingTableRecordOffset = 0,
        .flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference         = blasAddress,
    };
  }

  //--------------------------------------------------------------------------------------------------
  // Build the top-level AS once, with ALLOW_UPDATE so that later LOD switches only refit it. The
  // single instance lives in a persistent buffer that we rewrite in place on each switch.
  //
  void buildTlas(VkDeviceAddress blasAddress)
  {
    NVVK_CHECK(m_allocator.createBuffer(m_instancesBuffer, sizeof(VkAccelerationStructureInstanceKHR),
                                        VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT
                                            | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));
    NVVK_DBG_NAME(m_instancesBuffer.buffer);

    m_tlasData.addGeometry(m_tlasData.makeInstanceGeometry(1, m_instancesBuffer.address));
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo =
        m_tlasData.finalizeGeometry(m_app->getDevice(), VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                                            | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

    // Scratch must fit both the initial build and later updates
    NVVK_CHECK(m_allocator.createBuffer(m_tlasScratch, std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize),
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_asProp.minAccelerationStructureScratchOffsetAlignment));
    NVVK_CHECK(m_allocator.createAcceleration(m_tlas, m_tlasData.makeCreateInfo()));
    NVVK_DBG_NAME(m_tlas.accel);

    VkCommandBuffer                    cmd      = m_app->createTempCmdBuffer();
    VkAccelerationStructureInstanceKHR instance = makeInstance(blasAddress);
    vkCmdUpdateBuffer(cmd, m_instancesBuffer.buffer, 0, sizeof(instance), &instance);
    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);
    m_tlasData.cmdBuildAccelerationStructure(cmd, m_tlas.accel, m_tlasScratch.address);
    m_app->submitAndWaitTempCmdBuffer(cmd);
  }

  //--------------------------------------------------------------------------------------------------
  // Refit (update) the existing TLAS to reference a different level's cluster BLAS. Only the instance
  // data changes (still one instance), so an in-place update is enough - no destroy/rebuild.
  //
  void refitTlas(VkDeviceAddress blasAddress)
  {
    VkCommandBuffer                    cmd      = m_app->createTempCmdBuffer();
    VkAccelerationStructureInstanceKHR instance = makeInstance(blasAddress);
    vkCmdUpdateBuffer(cmd, m_instancesBuffer.buffer, 0, sizeof(instance), &instance);
    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);
    m_tlasData.cmdUpdateAccelerationStructure(cmd, m_tlas.accel, m_tlasScratch.address);
    m_app->submitAndWaitTempCmdBuffer(cmd);
  }

  //--------------------------------------------------------------------------------------------------
  // Point the TLAS at a given LOD level: build it the first time (ALLOW_UPDATE), refit afterwards.
  // Level changes are rare, so a wait + refit is fine (not production-appropriate as-is).
  //
  void setLevel(int level)
  {
    vkDeviceWaitIdle(m_app->getDevice());
    if(m_tlas.accel == VK_NULL_HANDLE)
      buildTlas(m_lods[level].blasAddress);
    else
      refitTlas(m_lods[level].blasAddress);
    m_currentLevel = level;
    m_numClusters  = m_lods[level].clusterCount;
  }

  //--------------------------------------------------------------------------------------------------
  // Sample state
  //--------------------------------------------------------------------------------------------------
  bool m_extensionSupported = false;

  // Geometry (all LOD levels concatenated)
  std::vector<glm::vec3>   m_vertices;
  std::vector<uint8_t>     m_indices;
  std::vector<ClusterInfo> m_clusters;
  std::vector<Lod>         m_lods;
  uint32_t                 m_maxClusterVertices{};
  uint32_t                 m_maxClusterTriangles{};
  nvvk::Buffer             m_vertexBuffer{};
  nvvk::Buffer             m_indexBuffer{};

  // Cluster acceleration structures
  nvvk::Buffer m_clasBuffer{};         // storage for all CLAS (all levels)
  nvvk::Buffer m_clasAddressBuffer{};  // per-CLAS device addresses
  nvvk::Buffer m_clusterBlasBuffer{};  // storage for the per-level cluster BLAS

  // Top-level AS (single instance, refit on LOD change)
  nvvk::AccelerationStructure m_tlas{};
  nvvk::AccelerationStructureBuildData m_tlasData{VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};  // kept alive for refits
  nvvk::Buffer m_instancesBuffer{};  // the single TLAS instance (rewritten on LOD change)
  nvvk::Buffer m_tlasScratch{};      // persistent TLAS build/update scratch

  // LOD state
  int      m_currentLevel{-1};  // level currently referenced by the TLAS
  int      m_lodOverride{-1};   // -1 = automatic (distance-based)
  uint32_t m_numClusters{};     // cluster count of the active level (UI)

  // Shading controls
  int       m_colorByCluster{1};
  glm::vec3 m_lightDir{glm::normalize(glm::vec3(0.5F, 1.0F, 0.7F))};

  // Properties (alignments and per-cluster limits)
  VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceClusterAccelerationStructurePropertiesNV m_clusterProp{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV};
};


//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  int                        initialLod     = -1;  // -1 = automatic
  int                        colorByCluster = 1;
  nvutils::ParameterParser   cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  reg.add({"lod", "Force a LOD level (0-3); -1 = automatic"}, &initialLod);
  reg.add({"colorByCluster", "Color the surface by cluster ID (1) or plain shading (0)"}, &colorByCluster);
  cli.add(reg);
  cli.parse(argc, argv);

  // Setting up the Vulkan context, instance and device extensions
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clusterFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME, &clusterFeature, false, 2},  // request spec version 2
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
  appInfo.name           = "Ray Tracing Tutorial - 21 Clusters";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtClusters>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Check if the cluster extension is supported and pass the flag / CLI options to the tutorial
  const bool extensionSupported = (clusterFeature.clusterAccelerationStructure == VK_TRUE);
  tutorial->setExtensionSupport(extensionSupported);
  tutorial->setInitialLod(initialLod);
  tutorial->setColorByCluster(colorByCluster);

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
