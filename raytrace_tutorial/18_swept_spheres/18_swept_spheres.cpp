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
// Ray Tracing Tutorial - 18 Swept Spheres
//
// This sample demonstrates the use of VK_NV_ray_tracing_linear_swept_spheres extension
// to render grass using Linear Swept Spheres, standalone spheres, and multi-segment LSS chains.
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

#include <random>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtsweptspheres.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


class RtSweptSpheres : public RtBase
{
public:
  RtSweptSpheres()           = default;
  ~RtSweptSpheres() override = default;

  // Set extension support flag
  void setExtensionSupport(bool supported) { m_extensionSupported = supported; }

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Swept Spheres Extension");
      if(m_extensionSupported)
      {
        ImGui::Text("Grass Blades: %d", m_numGrassBlades);
        ImGui::Text("Standalone Spheres: %d", m_numSpheres);
        ImGui::Text("LSS Chains: %d segments", m_numChainSegments);

        ImGui::Separator();
        if(ImGui::Checkbox("Use Image Coloring", &m_useImageColoring))
        {
          // Flag will be used in raytraceScene
        }
        ImGui::SliderFloat("Color Intensity", &m_colorIntensity, 0.0f, 1.0f);
      }
      else
      {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Extension VK_NV_ray_tracing_linear_swept_spheres");
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "is NOT supported on this device!");
      }
    }
    ImGui::End();
    RtBase::onUIRender();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    m_sceneResource.sceneInfo.punctualLights[0].intensity = 500.0f;
    m_sceneResource.sceneInfo.punctualLights[0].position  = glm::vec3(5.0f, 15.0f, 5.0f);  // Position of the light

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Load the GLTF resources (ground plane)
    {
      tinygltf::Model planeModel = nvsamples::loadGltfResources(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));
      // Import and create the glTF data buffer
      nvsamples::importGltfData(m_sceneResource, planeModel, m_stagingUploader, false);
    }

    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.40f, 0.25f, 0.12f, 1.0f), .metallicFactor = 0.0f, .roughnessFactor = 0.9f},  // Ground plane (soil brown)
    };

    // Make instances of the meshes
    m_sceneResource.instances = {
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.01f, 0)), glm::vec3(1.3f)), .materialIndex = 0, .meshIndex = 0},
    };

    // Create grass field, spheres, and chains only if extension is supported
    if(m_extensionSupported)
    {
      m_grassFieldSize = glm::vec2(10.0f, 10.0f);
      createGrassField(100000, m_grassFieldSize);  // 100,000 grass blades in 10x10 field
      createStandaloneSpheres(10);                 // 10 spheres
      createLSSChains(20, 4);                      // 20 chains with 4 segments each

      // Load grass color texture
      loadGrassTexture();
    }

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set the camera
    m_cameraManip->setLookat({5.0f, 3.0f, 5.0f}, {0.00000, 0.00000, 0.00000}, {0.00000, 1.00000, 0.00000});
  }

  void createBottomLevelAS() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Create BLAS for triangle meshes
    std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(m_sceneResource.meshes.size());
    for(uint32_t p_idx = 0; p_idx < m_sceneResource.meshes.size(); p_idx++)
    {
      geoInfos[p_idx] = primitiveToGeometry(m_sceneResource.meshes[p_idx]);
    }

    // Geometry data structures (need to persist for BLAS building)
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV grassLSSData{};
    VkAccelerationStructureGeometrySpheresDataNV            spheresData{};
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV chainsLSSData{};

    if(m_extensionSupported)
    {
      // Add BLAS for grass LSS
      {
        nvvk::AccelerationStructureGeometryInfo blas = grassLSSToGeometry(grassLSSData);
        geoInfos.emplace_back(blas);
      }

      // Add BLAS for standalone spheres
      {
        nvvk::AccelerationStructureGeometryInfo blas = spheresToGeometry(spheresData);
        geoInfos.emplace_back(blas);
      }

      // Add BLAS for chains LSS
      {
        nvvk::AccelerationStructureGeometryInfo blas = chainsLSSToGeometry(chainsLSSData);
        geoInfos.emplace_back(blas);
      }
    }

    m_asBuilder.blasSubmitBuildAndWait(geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createTopLevelAS() override
  {
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size() + 3);  // +3 for grass, spheres, chains
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    // Add triangle instances (ground plane)
    for(const shaderio::GltfInstance& instance : m_sceneResource.instances)
    {
      VkAccelerationStructureInstanceKHR ray_inst{};
      ray_inst.transform                              = nvvk::toTransformMatrixKHR(instance.transform);
      ray_inst.instanceCustomIndex                    = instance.meshIndex;
      ray_inst.accelerationStructureReference         = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset = 0;  // Hit group 0 for triangles
      ray_inst.flags                                  = flags;
      ray_inst.mask                                   = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }

    if(m_extensionSupported)
    {
      // Add the BLAS containing grass LSS
      {
        VkAccelerationStructureInstanceKHR rayInst{};
        rayInst.transform           = nvvk::toTransformMatrixKHR(glm::mat4(1));
        rayInst.instanceCustomIndex = static_cast<uint32_t>(m_sceneResource.instances.size());
        rayInst.accelerationStructureReference =
            m_asBuilder.blasSet[static_cast<uint32_t>(m_sceneResource.meshes.size())].address;
        rayInst.instanceShaderBindingTableRecordOffset = 1;  // Hit group 1 for grass LSS
        rayInst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        rayInst.mask                                   = 0xFF;
        tlasInstances.emplace_back(rayInst);
      }

      // Add the BLAS containing standalone spheres
      {
        VkAccelerationStructureInstanceKHR rayInst{};
        rayInst.transform           = nvvk::toTransformMatrixKHR(glm::mat4(1));
        rayInst.instanceCustomIndex = static_cast<uint32_t>(m_sceneResource.instances.size() + 1);
        rayInst.accelerationStructureReference =
            m_asBuilder.blasSet[static_cast<uint32_t>(m_sceneResource.meshes.size() + 1)].address;
        rayInst.instanceShaderBindingTableRecordOffset = 2;  // Hit group 2 for spheres
        rayInst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        rayInst.mask                                   = 0xFF;
        tlasInstances.emplace_back(rayInst);
      }

      // Add the BLAS containing chains LSS
      {
        VkAccelerationStructureInstanceKHR rayInst{};
        rayInst.transform           = nvvk::toTransformMatrixKHR(glm::mat4(1));
        rayInst.instanceCustomIndex = static_cast<uint32_t>(m_sceneResource.instances.size() + 2);
        rayInst.accelerationStructureReference =
            m_asBuilder.blasSet[static_cast<uint32_t>(m_sceneResource.meshes.size() + 2)].address;
        rayInst.instanceShaderBindingTableRecordOffset = 3;  // Hit group 3 for chains LSS
        rayInst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        rayInst.mask                                   = 0xFF;
        tlasInstances.emplace_back(rayInst);
      }
    }

    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtsweptspheres.slang", rtsweptspheres_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eLssUnsupportedShaderGroupCount,  // If LSS is unsupported, then we create an SBT with fewer shaders.
      eClosestHitGrass = eLssUnsupportedShaderGroupCount,
      eClosestHitSpheres,
      eClosestHitChains,
      eShaderGroupCount
    };
    std::vector<VkPipelineShaderStageCreateInfo> stages(m_extensionSupported ? eShaderGroupCount : eLssUnsupportedShaderGroupCount);
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
    if(m_extensionSupported)
    {
      stages[eClosestHitGrass].pNext   = &shaderCode;
      stages[eClosestHitGrass].pName   = "rchitMainGrass";
      stages[eClosestHitGrass].stage   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
      stages[eClosestHitSpheres].pNext = &shaderCode;
      stages[eClosestHitSpheres].pName = "rchitMainSpheres";
      stages[eClosestHitSpheres].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
      stages[eClosestHitChains].pNext  = &shaderCode;
      stages[eClosestHitChains].pName  = "rchitMainChains";
      stages[eClosestHitChains].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    }

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

    // Closest hit shader for triangles
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);

    if(m_extensionSupported)
    {
      // Closest hit shader for grass LSS
      group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      group.closestHitShader = eClosestHitGrass;
      shaderGroups.push_back(group);

      // Closest hit shader for standalone spheres
      group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      group.closestHitShader = eClosestHitSpheres;
      shaderGroups.push_back(group);

      // Closest hit shader for chains LSS
      group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      group.closestHitShader = eClosestHitChains;
      shaderGroups.push_back(group);
    }

    // Pipeline flags for swept spheres extension
    VkPipelineCreateFlags2CreateInfoKHR pipelineFlags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR};
    if(m_extensionSupported)
    {
      pipelineFlags2.flags = VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV;
    }

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    if(m_extensionSupported)
    {
      rtPipelineInfo.pNext = &pipelineFlags2;
    }
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  void createRaytraceDescriptorLayout() override
  {
    // No additional bindings needed - LSS hit data is accessed via builtins
    RtBase::createRaytraceDescriptorLayout();
  }

  void raytraceScene(VkCommandBuffer cmd) override
  {
    // Set custom push constant fields
    m_pushValues.useImageColoring = m_useImageColoring ? 1 : 0;
    m_pushValues.colorIntensity   = m_colorIntensity;
    m_pushValues.grassFieldSize   = m_grassFieldSize;

    // Normal ray tracing
    RtBase::raytraceScene(cmd);
  }

  void sampleDestroy() override
  {
    // Destroy grass, spheres, and chains buffers
    m_allocator.destroyBuffer(m_grassLSSVertexBuffer);
    m_allocator.destroyBuffer(m_grassLSSRadiusBuffer);
    m_allocator.destroyBuffer(m_spheresCenterBuffer);
    m_allocator.destroyBuffer(m_spheresRadiusBuffer);
    m_allocator.destroyBuffer(m_chainsLSSVertexBuffer);
    m_allocator.destroyBuffer(m_chainsLSSRadiusBuffer);
    m_allocator.destroyBuffer(m_chainsLSSIndexBuffer);
  }

private:
  // Extension support flag
  bool m_extensionSupported = false;

  // Primitive counts
  int m_numGrassBlades   = 0;
  int m_numSpheres       = 0;
  int m_numChainSegments = 0;

  // UI controls
  bool  m_useImageColoring = false;
  float m_colorIntensity   = 0.5f;

  // Scene parameters
  glm::vec2 m_grassFieldSize = glm::vec2(10.0f, 10.0f);

  // Grass LSS buffers
  std::vector<glm::vec3> m_grassLSSVertices;
  std::vector<float>     m_grassLSSRadii;
  nvvk::Buffer           m_grassLSSVertexBuffer;
  nvvk::Buffer           m_grassLSSRadiusBuffer;

  // Standalone spheres buffers
  std::vector<glm::vec3> m_spheresCenters;
  std::vector<float>     m_spheresRadii;
  nvvk::Buffer           m_spheresCenterBuffer;
  nvvk::Buffer           m_spheresRadiusBuffer;

  // Chains LSS buffers
  std::vector<glm::vec3> m_chainsLSSVertices;
  std::vector<float>     m_chainsLSSRadii;
  std::vector<uint32_t>  m_chainsLSSIndices;
  nvvk::Buffer           m_chainsLSSVertexBuffer;
  nvvk::Buffer           m_chainsLSSRadiusBuffer;
  nvvk::Buffer           m_chainsLSSIndexBuffer;


  //--------------------------------------------------------------------------------------------------
  // Creating grass field with LSS
  //
  void createGrassField(uint32_t grassBlades, glm::vec2 fieldSize)
  {
    std::random_device                    rd{};
    std::mt19937                          gen{rd()};
    std::uniform_real_distribution<float> xPosDist{-fieldSize.x * 0.5f, fieldSize.x * 0.5f};
    std::uniform_real_distribution<float> zPosDist{-fieldSize.y * 0.5f, fieldSize.y * 0.5f};
    std::uniform_real_distribution<float> heightDist{0.2f, 0.5f};
    std::uniform_real_distribution<float> tiltDist{-0.02f, 0.02f};
    std::uniform_real_distribution<float> radiusDist{0.003f, 0.008f};

    m_numGrassBlades = grassBlades;
    m_grassLSSVertices.reserve(m_numGrassBlades * 2);
    m_grassLSSRadii.reserve(m_numGrassBlades * 2);

    for(uint32_t i = 0; i < grassBlades; i++)
    {
      float xPos   = xPosDist(gen);
      float zPos   = zPosDist(gen);
      float height = heightDist(gen);
      float radius = radiusDist(gen);

      // Root at ground
      glm::vec3 root(xPos, 0.0f, zPos);
      // Tip with slight tilt
      glm::vec3 tip(xPos + tiltDist(gen), height, zPos + tiltDist(gen));

      m_grassLSSVertices.push_back(root);
      m_grassLSSVertices.push_back(tip);
      m_grassLSSRadii.push_back(radius);         // Thicker at base
      m_grassLSSRadii.push_back(radius * 0.4f);  // Thinner at tip
    }

    // Creating buffers
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();

      m_allocator.createBuffer(m_grassLSSVertexBuffer, std::span(m_grassLSSVertices).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_grassLSSVertexBuffer, 0, std::span(m_grassLSSVertices)));
      NVVK_DBG_NAME(m_grassLSSVertexBuffer.buffer);

      m_allocator.createBuffer(m_grassLSSRadiusBuffer, std::span(m_grassLSSRadii).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_grassLSSRadiusBuffer, 0, std::span(m_grassLSSRadii)));
      NVVK_DBG_NAME(m_grassLSSRadiusBuffer.buffer);

      m_stagingUploader.cmdUploadAppended(cmd);
      m_app->submitAndWaitTempCmdBuffer(cmd);
    }
  }

  //--------------------------------------------------------------------------------------------------
  // Creating standalone spheres
  //
  void createStandaloneSpheres(uint32_t numSpheres)
  {
    std::random_device                    rd{};
    std::mt19937                          gen{rd()};
    std::uniform_real_distribution<float> posDist{-4.0f, 4.0f};
    std::uniform_real_distribution<float> radiusDist{0.15f, 0.45f};

    m_numSpheres = numSpheres;
    m_spheresCenters.reserve(numSpheres);
    m_spheresRadii.reserve(numSpheres);

    for(uint32_t i = 0; i < numSpheres; i++)
    {
      glm::vec3 center = glm::vec3(posDist(gen), radiusDist(gen) + 0.1f, posDist(gen));
      float     radius = radiusDist(gen);
      m_spheresCenters.push_back(center);
      m_spheresRadii.push_back(radius);
    }

    // Creating buffers
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();

      m_allocator.createBuffer(m_spheresCenterBuffer, std::span(m_spheresCenters).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresCenterBuffer, 0, std::span(m_spheresCenters)));
      NVVK_DBG_NAME(m_spheresCenterBuffer.buffer);

      m_allocator.createBuffer(m_spheresRadiusBuffer, std::span(m_spheresRadii).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresRadiusBuffer, 0, std::span(m_spheresRadii)));
      NVVK_DBG_NAME(m_spheresRadiusBuffer.buffer);

      m_stagingUploader.cmdUploadAppended(cmd);
      m_app->submitAndWaitTempCmdBuffer(cmd);
    }
  }

  //--------------------------------------------------------------------------------------------------
  // Creating multi-segment LSS chains
  //
  void createLSSChains(uint32_t numChains, uint32_t segmentsPerChain)
  {
    std::random_device                    rd{};
    std::mt19937                          gen{rd()};
    std::uniform_real_distribution<float> posDist{-4.0f, 4.0f};
    std::uniform_real_distribution<float> angleDist{0.0f, 6.28318f};
    std::uniform_real_distribution<float> radiusDist{0.1f, 0.4f};

    m_numChainSegments = numChains * segmentsPerChain;
    // Each chain has (segmentsPerChain + 1) vertices (no duplication between segments)
    m_chainsLSSVertices.reserve(numChains * (segmentsPerChain + 1));
    m_chainsLSSRadii.reserve(numChains * (segmentsPerChain + 1));
    // Each chain needs segmentsPerChain indices for successive indexing
    m_chainsLSSIndices.reserve(m_numChainSegments);

    for(uint32_t i = 0; i < numChains; i++)
    {
      // Start position
      glm::vec3 pos(posDist(gen), 0.0f, posDist(gen));
      float     angle  = angleDist(gen);
      float     radius = radiusDist(gen);

      // Store the starting vertex index for this chain
      uint32_t chainStartVertex = static_cast<uint32_t>(m_chainsLSSVertices.size());

      // Add the first vertex of the chain
      m_chainsLSSVertices.push_back(pos);
      m_chainsLSSRadii.push_back(radius);

      // Generate the remaining vertices and indices
      for(uint32_t seg = 0; seg < segmentsPerChain; seg++)
      {
        float     segHeight = 0.5f;
        glm::vec3 segEnd    = pos + glm::vec3(cos(angle) * 0.1f, segHeight, sin(angle) * 0.1f);
        float     endRadius = radius * 0.8f;

        // Add the end vertex
        m_chainsLSSVertices.push_back(segEnd);
        m_chainsLSSRadii.push_back(endRadius);

        // Add index for this segment (points to the first vertex of the pair)
        // In successive mode, index k means vertices (k, k+1) form the LSS primitive
        m_chainsLSSIndices.push_back(chainStartVertex + seg);

        // Update position and parameters for next segment
        pos    = segEnd;
        radius = endRadius;
        angle += (gen() % 100 - 50) * 0.02f;  // Vary the angle slightly
      }
    }

    // Creating buffers
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();

      m_allocator.createBuffer(m_chainsLSSVertexBuffer, std::span(m_chainsLSSVertices).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_chainsLSSVertexBuffer, 0, std::span(m_chainsLSSVertices)));
      NVVK_DBG_NAME(m_chainsLSSVertexBuffer.buffer);

      m_allocator.createBuffer(m_chainsLSSRadiusBuffer, std::span(m_chainsLSSRadii).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_chainsLSSRadiusBuffer, 0, std::span(m_chainsLSSRadii)));
      NVVK_DBG_NAME(m_chainsLSSRadiusBuffer.buffer);

      m_allocator.createBuffer(m_chainsLSSIndexBuffer, std::span(m_chainsLSSIndices).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_chainsLSSIndexBuffer, 0, std::span(m_chainsLSSIndices)));
      NVVK_DBG_NAME(m_chainsLSSIndexBuffer.buffer);

      m_stagingUploader.cmdUploadAppended(cmd);
      m_app->submitAndWaitTempCmdBuffer(cmd);
    }
  }

  //--------------------------------------------------------------------------------------------------
  // Load grass color texture
  //
  void loadGrassTexture()
  {
    // Textures
    std::filesystem::path imageFilename = nvutils::findFile("nvidia.png", nvsamples::getResourcesDirs());
    VkCommandBuffer       cmd           = m_app->createTempCmdBuffer();
    nvvk::Image texture = nvsamples::loadAndCreateImage(cmd, m_stagingUploader, m_app->getDevice(), imageFilename);  // Load the image from the file and create a texture from it
    NVVK_DBG_NAME(texture.image);
    m_app->submitAndWaitTempCmdBuffer(cmd);
    m_samplerPool.acquireSampler(texture.descriptor.sampler);
    m_textures.emplace_back(texture);  // Store the texture in the vector of textures
  }

  //--------------------------------------------------------------------------------------------------
  // Returning the ray tracing geometry used for the BLAS, containing grass LSS
  //
  nvvk::AccelerationStructureGeometryInfo grassLSSToGeometry(VkAccelerationStructureGeometryLinearSweptSpheresDataNV& grassLSSData)
  {
    grassLSSData.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV;
    grassLSSData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    grassLSSData.vertexData   = {.deviceAddress = m_grassLSSVertexBuffer.address};
    grassLSSData.vertexStride = sizeof(glm::vec3);
    grassLSSData.radiusFormat = VK_FORMAT_R32_SFLOAT;
    grassLSSData.radiusData   = {.deviceAddress = m_grassLSSRadiusBuffer.address};
    grassLSSData.radiusStride = sizeof(float);
    grassLSSData.indexType    = VK_INDEX_TYPE_NONE_KHR;
    grassLSSData.indexData    = {};
    grassLSSData.indexStride  = 0;
    grassLSSData.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_LIST_NV;
    grassLSSData.endCapsMode  = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.pNext        = &grassLSSData;
    geometry.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
    geometry.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{.primitiveCount = uint32_t(m_numGrassBlades)};

    return {geometry, rangeInfo};
  }

  //--------------------------------------------------------------------------------------------------
  // Returning the ray tracing geometry used for the BLAS, containing standalone spheres
  //
  nvvk::AccelerationStructureGeometryInfo spheresToGeometry(VkAccelerationStructureGeometrySpheresDataNV& spheresData)
  {
    spheresData.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_SPHERES_DATA_NV;
    spheresData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    spheresData.vertexData   = {.deviceAddress = m_spheresCenterBuffer.address};
    spheresData.vertexStride = sizeof(glm::vec3);
    spheresData.radiusFormat = VK_FORMAT_R32_SFLOAT;
    spheresData.radiusData   = {.deviceAddress = m_spheresRadiusBuffer.address};
    spheresData.radiusStride = sizeof(float);
    spheresData.indexType    = VK_INDEX_TYPE_NONE_KHR;
    spheresData.indexData    = {};
    spheresData.indexStride  = 0;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.pNext        = &spheresData;
    geometry.geometryType = VK_GEOMETRY_TYPE_SPHERES_NV;
    geometry.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{.primitiveCount = uint32_t(m_numSpheres)};

    return {geometry, rangeInfo};
  }

  //--------------------------------------------------------------------------------------------------
  // Returning the ray tracing geometry used for the BLAS, containing chains LSS
  //
  nvvk::AccelerationStructureGeometryInfo chainsLSSToGeometry(VkAccelerationStructureGeometryLinearSweptSpheresDataNV& chainsLSSData)
  {
    chainsLSSData.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV;
    chainsLSSData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    chainsLSSData.vertexData   = {.deviceAddress = m_chainsLSSVertexBuffer.address};
    chainsLSSData.vertexStride = sizeof(glm::vec3);
    chainsLSSData.radiusFormat = VK_FORMAT_R32_SFLOAT;
    chainsLSSData.radiusData   = {.deviceAddress = m_chainsLSSRadiusBuffer.address};
    chainsLSSData.radiusStride = sizeof(float);
    chainsLSSData.indexType    = VK_INDEX_TYPE_UINT32;
    chainsLSSData.indexData    = {.deviceAddress = m_chainsLSSIndexBuffer.address};
    chainsLSSData.indexStride  = sizeof(uint32_t);
    chainsLSSData.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
    chainsLSSData.endCapsMode  = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.pNext        = &chainsLSSData;
    geometry.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
    geometry.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{.primitiveCount = uint32_t(m_numChainSegments)};

    return {geometry, rangeInfo};
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
  VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV linearSweptSpheresFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME, &linearSweptSpheresFeature, false},  // Optional extension
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
  appInfo.name           = "Ray Tracing Tutorial - 18 Swept Spheres";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtSweptSpheres>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Check if extension is supported and set the flag in tutorial
  bool extensionSupported =
      (linearSweptSpheresFeature.linearSweptSpheres == VK_TRUE && linearSweptSpheresFeature.spheres == VK_TRUE);
  tutorial->setExtensionSupport(extensionSupported);

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
