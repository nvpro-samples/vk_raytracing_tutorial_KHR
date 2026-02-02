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
// Ray Tracing Tutorial - 09 Motion Blur
//
// This sample demonstrates the use of motion blur in ray tracing with three different types:
// - Matrix motion: transformation matrix interpolation (translation)
// - SRT motion: scale, rotation, translation interpolation
// - Vertex motion: vertex position interpolation between two meshes
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

#include <nvutils/primitives.hpp>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtmotionblur.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


class RtMotionBlur : public RtBase
{
  // Mesh indices for better code readability
  enum MeshIndices
  {
    eMeshPlane = 0,
    eMeshCube,
    eMeshModifiedCube,
    eMeshCount
  };

  // Instance indices for better code readability
  enum InstanceIndices
  {
    eInstancePlane = 0,  // Static plane
    eInstanceCube0,      // Green cube with matrix motion (translation)
    eInstanceCube1,      // Red cube with SRT motion (rotation)
    eInstanceCube2,      // Blue cube with vertex motion (morphing)
    eInstanceCount
  };

public:
  RtMotionBlur()           = default;
  ~RtMotionBlur() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Motion Blur");

      ImGui::Text("This sample demonstrates three types of motion blur:");
      ImGui::Separator();
      ImGui::BulletText("Matrix motion - Green cube translates");
      ImGui::BulletText("SRT motion - Red cube (back) rotates");
      ImGui::BulletText("Vertex motion - Blue cube (center) morphs");
      ImGui::Separator();
      ImGui::TextWrapped("Motion blur is interpolated between T0 and T1 based on ray time.");
      ImGui::Separator();

      // Motion blur settings
      ImGui::Text("Motion Blur Settings:");
      ImGui::SliderInt("Samples", &m_numSamples, 1, 100, "%d", ImGuiSliderFlags_Logarithmic);
      ImGui::TextWrapped("More samples = smoother motion blur but slower rendering");
    }
    ImGui::End();
    RtBase::onUIRender();
  }


  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Initialize acceleration structure properties for motion blur
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_asProperties;
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Create a plane
    nvutils::PrimitiveMesh plane = nvutils::createPlane();
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, plane);

    // Create a cube
    nvutils::PrimitiveMesh cube = nvutils::createCube();
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, cube);

    // Add a modified Cube: one vertex is moved, this will be used to have motion between Cube and the Modified Cube
    nvutils::PrimitiveMesh modifiedCube = nvutils::createCube();
    modifiedCube.vertices[6].pos *= 2;   // Modifying the +x,+y,+z position (appearing 3 times)
    modifiedCube.vertices[11].pos *= 2;  // Modifying the +x,+y,+z position
    modifiedCube.vertices[22].pos *= 2;  // Modifying the +x,+y,+z position
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, modifiedCube);

    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},  // White
        {.baseColorFactor = glm::vec4(0.1f, 1.0f, 0.1f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},  // Green
        {.baseColorFactor = glm::vec4(0.8f, 0.0f, 0.1f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},  // Red
        {.baseColorFactor = glm::vec4(0.1f, 0.0f, 0.8f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f}};  // Blue


    // Make instances of the meshes
    m_sceneResource.instances = {
        {
            .transform     = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(10.0f)),
            .materialIndex = 0,
            .meshIndex     = eMeshPlane,
        },
        {
            .transform     = glm::translate(glm::mat4(1), glm::vec3(2.0f, 0.0f, 2.0f)),
            .materialIndex = 1,
            .meshIndex     = eMeshCube,
        },
        {
            .transform     = glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, 2.0f)),
            .materialIndex = 2,
            .meshIndex     = eMeshCube,
        },
        {
            .transform     = glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, 0.0f)),
            .materialIndex = 3,
            .meshIndex     = eMeshModifiedCube,
        },
    };


    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set the camera & light
    m_cameraManip->setLookat({3.91698, 2.65970, -0.42755}, {0.71716, 0.03205, 1.36345}, {0.00000, 1.00000, 0.00000});
    m_sceneResource.sceneInfo.punctualLights[0].position  = glm::vec3(4, 5, -1);
    m_sceneResource.sceneInfo.punctualLights[0].intensity = 100;
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtmotionblur.slang", rtmotionblur_slang);

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

    // Create the ray tracing pipeline with motion blur support
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    rtPipelineInfo.flags = VK_PIPELINE_CREATE_RAY_TRACING_ALLOW_MOTION_BIT_NV;  // Enable motion blur
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  //-------------------------------------------------------------------------------
  // Motion Blur specific overrides
  //-------------------------------------------------------------------------------

  void createBottomLevelAS() override
  {
    SCOPED_TIMER(__FUNCTION__);

    std::vector<nvvk::AccelerationStructureBuildData> blasBuildData;

    // BLAS - Storing each mesh in a geometry
    blasBuildData.resize(eMeshCount);
    m_asBuilder.blasSet.resize(eMeshCount);

    VkDeviceSize maxScratchSize{0};

    // Adding Plane mesh
    {
      blasBuildData[eMeshPlane].asType = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      blasBuildData[eMeshPlane].addGeometry(RtBase::primitiveToGeometry(m_sceneResource.meshes[eMeshPlane]));
      auto sizeInfo  = blasBuildData[eMeshPlane].finalizeGeometry(m_app->getDevice(),
                                                                  VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
      maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);
    }
    // Adding Cube
    {
      blasBuildData[eMeshCube].asType = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      blasBuildData[eMeshCube].addGeometry(RtBase::primitiveToGeometry(m_sceneResource.meshes[eMeshCube]));
      auto sizeInfo  = blasBuildData[eMeshCube].finalizeGeometry(m_app->getDevice(),
                                                                 VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
      maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);
    }

    // Motion-enabled BLAS for modified cube (vertex motion)
    VkAccelerationStructureGeometryMotionTrianglesDataNV motionTriangles{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_MOTION_TRIANGLES_DATA_NV};
    motionTriangles.vertexData.deviceAddress = VkDeviceAddress(m_sceneResource.meshes[eMeshCube].gltfBuffer)
                                               + m_sceneResource.meshes[eMeshCube].triMesh.positions.offset;  // Original cube vertices for T0

    {  // Motion blur BLAS for modified cube
      nvvk::AccelerationStructureGeometryInfo geo = primitiveToGeometry(m_sceneResource.meshes[eMeshModifiedCube]);  // T1 vertices (modified cube)
      geo.geometry.geometry.triangles.pNext = &motionTriangles;  // T0 vertices (original cube)

      blasBuildData[eMeshModifiedCube].asType = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      blasBuildData[eMeshModifiedCube].addGeometry(geo);
      auto sizeInfo = blasBuildData[eMeshModifiedCube].finalizeGeometry(m_app->getDevice(), VK_BUILD_ACCELERATION_STRUCTURE_MOTION_BIT_NV);
      maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);
    }

    // Create scratch buffer
    nvvk::Buffer scratchBuffer;
    NVVK_CHECK(m_allocator.createBuffer(scratchBuffer, maxScratchSize,
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_asProperties.minAccelerationStructureScratchOffsetAlignment));
    NVVK_DBG_NAME(scratchBuffer.buffer);

    // Create the acceleration structures
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    {
      for(size_t p_idx = 0; p_idx < blasBuildData.size(); p_idx++)
      {
        NVVK_CHECK(m_allocator.createAcceleration(m_asBuilder.blasSet[p_idx], blasBuildData[p_idx].makeCreateInfo()));
        NVVK_DBG_NAME(m_asBuilder.blasSet[p_idx].accel);
        blasBuildData[p_idx].cmdBuildAccelerationStructure(cmd, m_asBuilder.blasSet[p_idx].accel, scratchBuffer.address);

        // Add barrier between BLAS builds to prevent WRITE_AFTER_WRITE hazard on scratch buffer
        nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                                           VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
      }
    }
    m_app->submitAndWaitTempCmdBuffer(cmd);

    m_allocator.destroyBuffer(scratchBuffer);
  }

  void createTopLevelAS() override
  {
    // VkAccelerationStructureMotionInstanceNV must have a stride of 160 bytes
    struct VkAccelerationStructureMotionInstanceNVPad : VkAccelerationStructureMotionInstanceNV
    {
      uint64_t _pad{0};
    };
    static_assert(sizeof(VkAccelerationStructureMotionInstanceNVPad) == 160);

    std::vector<VkAccelerationStructureMotionInstanceNVPad> motionInstances;
    motionInstances.reserve(m_sceneResource.instances.size());

    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    // Plane - static instance
    {
      VkAccelerationStructureInstanceKHR staticInst{};
      staticInst.transform           = nvvk::toTransformMatrixKHR(m_sceneResource.instances[eInstancePlane].transform);
      staticInst.instanceCustomIndex = m_sceneResource.instances[eInstancePlane].meshIndex;
      staticInst.accelerationStructureReference =
          m_asBuilder.blasSet[m_sceneResource.instances[eInstancePlane].meshIndex].address;
      staticInst.instanceShaderBindingTableRecordOffset = 0;
      staticInst.flags                                  = flags;
      staticInst.mask                                   = 0xFF;

      VkAccelerationStructureMotionInstanceNVPad motionInst;
      motionInst.type                = VK_ACCELERATION_STRUCTURE_MOTION_INSTANCE_TYPE_STATIC_NV;
      motionInst.data.staticInstance = staticInst;
      motionInstances.emplace_back(motionInst);
    }

    // Cube-0 (green) - Matrix transformation motion (translation)
    {
      glm::mat4 matT0 = m_sceneResource.instances[eInstanceCube0].transform;                 // Original position
      glm::mat4 matT1 = glm::translate(glm::mat4(1), glm::vec3(0.30f, 0.0f, 0.0f)) * matT0;  // Translated position

      VkAccelerationStructureMatrixMotionInstanceNV matrixData{};
      matrixData.transformT0         = nvvk::toTransformMatrixKHR(matT0);
      matrixData.transformT1         = nvvk::toTransformMatrixKHR(matT1);
      matrixData.instanceCustomIndex = m_sceneResource.instances[eInstanceCube0].meshIndex;
      matrixData.accelerationStructureReference =
          m_asBuilder.blasSet[m_sceneResource.instances[eInstanceCube0].meshIndex].address;
      matrixData.instanceShaderBindingTableRecordOffset = 0;
      matrixData.flags                                  = flags;
      matrixData.mask                                   = 0xFF;

      VkAccelerationStructureMotionInstanceNVPad motionInst;
      motionInst.type                      = VK_ACCELERATION_STRUCTURE_MOTION_INSTANCE_TYPE_MATRIX_MOTION_NV;
      motionInst.data.matrixMotionInstance = matrixData;
      motionInstances.emplace_back(motionInst);
    }

    // Cube-1 (red) - SRT transformation motion (rotation)
    {
      glm::quat rot0 = {1, 0, 0, 0};                                                          // No rotation
      glm::quat rot1 = glm::quat(glm::vec3(glm::radians(10.0f), glm::radians(30.0f), 0.0f));  // Rotation

      VkSRTDataNV srtT0{};
      srtT0.sx = srtT0.sy = srtT0.sz = 1.0f;
      srtT0.tx                       = m_sceneResource.instances[eInstanceCube1].transform[3][0];
      srtT0.ty                       = m_sceneResource.instances[eInstanceCube1].transform[3][1];
      srtT0.tz                       = m_sceneResource.instances[eInstanceCube1].transform[3][2];
      srtT0.qx                       = rot0.x;
      srtT0.qy                       = rot0.y;
      srtT0.qz                       = rot0.z;
      srtT0.qw                       = rot0.w;

      VkSRTDataNV srtT1 = srtT0;
      srtT1.qx          = rot1.x;
      srtT1.qy          = rot1.y;
      srtT1.qz          = rot1.z;
      srtT1.qw          = rot1.w;

      VkAccelerationStructureSRTMotionInstanceNV srtData{};
      srtData.transformT0         = srtT0;
      srtData.transformT1         = srtT1;
      srtData.instanceCustomIndex = m_sceneResource.instances[eInstanceCube1].meshIndex;
      srtData.accelerationStructureReference = m_asBuilder.blasSet[m_sceneResource.instances[eInstanceCube1].meshIndex].address;
      srtData.instanceShaderBindingTableRecordOffset = 0;
      srtData.flags                                  = flags;
      srtData.mask                                   = 0xFF;

      VkAccelerationStructureMotionInstanceNVPad motionInst;
      motionInst.type                   = VK_ACCELERATION_STRUCTURE_MOTION_INSTANCE_TYPE_SRT_MOTION_NV;
      motionInst.data.srtMotionInstance = srtData;
      motionInstances.emplace_back(motionInst);
    }

    // Cube-2 (blue) - Vertex motion (using the modified cube mesh) - static instance but uses motion-enabled BLAS
    {
      VkAccelerationStructureInstanceKHR staticInst{};
      staticInst.transform           = nvvk::toTransformMatrixKHR(m_sceneResource.instances[eInstanceCube2].transform);
      staticInst.instanceCustomIndex = m_sceneResource.instances[eInstanceCube2].meshIndex;
      staticInst.accelerationStructureReference =
          m_asBuilder.blasSet[m_sceneResource.instances[eInstanceCube2].meshIndex].address;
      staticInst.instanceShaderBindingTableRecordOffset = 0;
      staticInst.flags                                  = flags;
      staticInst.mask                                   = 0xFF;

      VkAccelerationStructureMotionInstanceNVPad motionInst;
      motionInst.type                = VK_ACCELERATION_STRUCTURE_MOTION_INSTANCE_TYPE_STATIC_NV;
      motionInst.data.staticInstance = staticInst;
      motionInstances.emplace_back(motionInst);
    }

    // Build TLAS with motion blur support
    VkCommandBuffer       cmd = m_app->createTempCmdBuffer();
    nvvk::StagingUploader uploader;
    uploader.init(&m_allocator);

    // Create the instances buffer
    nvvk::Buffer instancesBuffer;
    NVVK_CHECK(m_allocator.createBuffer(instancesBuffer, std::span(motionInstances).size_bytes(),
                                        VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                            | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));
    NVVK_CHECK(uploader.appendBuffer(instancesBuffer, 0, std::span(motionInstances)));
    NVVK_DBG_NAME(instancesBuffer.buffer);
    uploader.cmdUploadAppended(cmd);
    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);

    // Build TLAS
    nvvk::AccelerationStructureBuildData    tlasBuildData{VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
    nvvk::AccelerationStructureGeometryInfo geometryInfo =
        tlasBuildData.makeInstanceGeometry(motionInstances.size(), instancesBuffer.address);
    tlasBuildData.addGeometry(geometryInfo);

    // Get the size of the TLAS
    auto sizeInfo = tlasBuildData.finalizeGeometry(m_app->getDevice(), VK_BUILD_ACCELERATION_STRUCTURE_MOTION_BIT_NV);

    // Create the scratch buffer
    nvvk::Buffer scratchBuffer;
    NVVK_CHECK(m_allocator.createBuffer(scratchBuffer, sizeInfo.buildScratchSize,
                                        VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_asProperties.minAccelerationStructureScratchOffsetAlignment));

    // Create the TLAS with motion blur support
    VkAccelerationStructureCreateInfoKHR createInfo = tlasBuildData.makeCreateInfo();
#ifdef VK_NV_ray_tracing_motion_blur
    VkAccelerationStructureMotionInfoNV motionInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MOTION_INFO_NV};
    motionInfo.maxInstances = uint32_t(motionInstances.size());
    createInfo.createFlags  = VK_ACCELERATION_STRUCTURE_CREATE_MOTION_BIT_NV;
    createInfo.pNext        = &motionInfo;
#endif

    NVVK_CHECK(m_allocator.createAcceleration(m_asBuilder.tlas, createInfo));
    NVVK_DBG_NAME(m_asBuilder.tlas.accel);
    tlasBuildData.cmdBuildAccelerationStructure(cmd, m_asBuilder.tlas.accel, scratchBuffer.address);


    m_app->submitAndWaitTempCmdBuffer(cmd);
    uploader.deinit();

    m_allocator.destroyBuffer(scratchBuffer);
    m_allocator.destroyBuffer(instancesBuffer);
  }

  void raytraceScene(VkCommandBuffer cmd) override
  {
    m_pushValues.numSamples = m_numSamples;
    RtBase::raytraceScene(cmd);  // Call the base class method to set up the command buffer
    return;

    // Ray trace
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{.sType      = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
                                                          .stageFlags = VK_SHADER_STAGE_ALL,
                                                          .layout     = m_rtPipelineLayout,
                                                          .firstSet   = 0,
                                                          .descriptorSetCount = 1,
                                                          .pDescriptorSets    = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // Push descriptor sets for ray tracing (use motion blur TLAS)
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_asBuilder.tlas);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage), m_gBuffers.getColorImageView(eImgRendered),
                 VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1, write.size(), write.data());

    // Push constant information
    m_pushValues.sceneInfoAddress          = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;
    m_pushValues.metallicRoughnessOverride = m_metallicRoughnessOverride;
    m_pushValues.numSamples                = m_numSamples;

    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &m_pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Ray trace
    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D&                  size    = m_app->getViewportSize();
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable, size.width, size.height, 1);
  }

private:
  VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

  // Motion blur settings
  int m_numSamples = 10;  // Number of samples for motion blur accumulation
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
  VkPhysicalDeviceRayTracingMotionBlurFeaturesNV rtMotionBlurFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MOTION_BLUR_FEATURES_NV};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_NV_RAY_TRACING_MOTION_BLUR_EXTENSION_NAME, &rtMotionBlurFeatures},  // Required for motion blur
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
  appInfo.name           = "Ray Tracing Tutorial - 09 Motion Blur";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtMotionBlur>();
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