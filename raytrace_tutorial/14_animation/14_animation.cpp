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
// Ray Tracing Tutorial - 14 Animation
//
// This sample demonstrates animation in ray tracing using two methods:
// 1. Instance animation - updating transformation matrices in the TLAS
// 2. Geometry animation - updating vertex positions using compute shaders
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
#include "_autogen/animation.slang.h"
#include "_autogen/vertex_animation.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


class Rt14Animation : public RtBase
{
  enum MeshIndex
  {
    eWuson = 0,  // Wuson model
    ePlane,      // Ground plane
    eSphere,     // Sphere for geometry animation
    eMeshCount
  };


public:
  Rt14Animation()           = default;
  ~Rt14Animation() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void sampleDestroy() override
  {
    vkDestroyPipeline(m_app->getDevice(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_app->getDevice(), m_pipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
  }

  void onUIRender() override
  {
    RtBase::onUIRender();

    if(ImGui::Begin("Settings"))
    {
      ImGui::Text("Animation Controls");
      ImGui::Separator();
      ImGui::Checkbox("Enable Instance Animation", &m_enableInstanceAnimation);
      ImGui::Checkbox("Enable Geometry Animation", &m_enableGeometryAnimation);
      ImGui::SliderFloat("Animation Speed", &m_animationSpeed, 0.1f, 2.0f);
    }
    ImGui::End();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Load the GLTF resources
    {
      tinygltf::Model wusonModel =
          nvsamples::loadGltfResources(nvutils::findFile("wuson.glb", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      nvsamples::importGltfData(m_sceneResource, wusonModel, m_stagingUploader, false);
    }

    // Create other model for geometry animation
    {
      nvutils::PrimitiveMesh sphere = nvutils::createSphereUv(1.0, 200, 200);
      nvutils::PrimitiveMesh plane  = nvutils::createPlane(1, 20, 20);
      nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, plane);   // Must be #1 (ePlane)
      nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, sphere);  // Must be #2 (eSphere)
    }

    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},
        {.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f},
        {.baseColorFactor = glm::vec4(1.0f, 0.5f, 0.8f, 1.0f), .metallicFactor = 0.3f, .roughnessFactor = 0.7f},
    };


    // Make instances of the meshes
    shaderio::GltfInstance instance{};
    // Create multiple Wuson instances for instance animation
    for(int i = 0; i < m_numWusonModels; i++)
    {
      instance.materialIndex = 0;
      instance.transform     = glm::mat4(1);
      instance.meshIndex     = eWuson;  // Wuson model
      m_sceneResource.instances.push_back(instance);
    }

    {
      // Ground plane (static)
      instance.materialIndex = 1;
      instance.transform     = glm::scale(glm::mat4(1), glm::vec3(10.0f, 1.0f, 10.0f));
      instance.meshIndex     = ePlane;
      m_sceneResource.instances.push_back(instance);
    }

    // Add sphere for geometry animation
    {
      instance.materialIndex = 2;
      instance.transform     = glm::translate(glm::mat4(1), glm::vec3(0.0f, 2.0f, 0.0f));
      instance.meshIndex     = eSphere;  // Sphere model
      m_sceneResource.instances.push_back(instance);
    }

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    //// Create the instance and scene buffers (GPU buffers)
    //// Create the scene information buffer (UBO) with the mesh data
    //NVVK_CHECK(m_allocator.createBuffer(m_bInstances, std::span(m_instances).size_bytes(),
    //                                    VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
    //                                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE));
    //NVVK_CHECK(m_stagingUploader.appendBuffer(m_bInstances, 0, std::span(m_instances)));
    //NVVK_DBG_NAME(m_bInstances.buffer);

    //// Create a buffer (UBO) to store the scene information (set each frame)
    //NVVK_CHECK(m_allocator.createBuffer(m_bSceneInfo, sizeof(shaderio::GltfSceneInfo),
    //                                    VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
    //                                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE));
    //NVVK_DBG_NAME(m_bSceneInfo.buffer);

    // Upload the scene information to the GPU
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    m_stagingUploader.cmdUploadAppended(cmd);
    m_app->submitAndWaitTempCmdBuffer(cmd);  // Submit the command buffer to upload the resources

    // Set the camera
    m_cameraManip->setLookat({3.0f, 5.0f, -12.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("animation.slang", animation_slang);

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


    // Create the compute pipeline
    createComputePipeline();
  }


  void createComputePipeline()
  {
    // Destroy the compute pipeline
    vkDestroyPipeline(m_app->getDevice(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_app->getDevice(), m_pipelineLayout, nullptr);

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("vertex_animation.slang", vertex_animation_slang);

    // Push constant
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset     = 0,
        .size       = sizeof(shaderio::VertexAnimationPushConstant),
    };

    // Pipeline layout
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange,
    };
    NVVK_CHECK(vkCreatePipelineLayout(m_app->getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout));
    NVVK_DBG_NAME(m_pipelineLayout);

    // Compute Pipeline
    VkComputePipelineCreateInfo compInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    compInfo.stage                       = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    compInfo.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
    compInfo.stage.pName                 = "main";
    compInfo.stage.pNext                 = &shaderCode;
    compInfo.layout                      = m_pipelineLayout;

    NVVK_CHECK(vkCreateComputePipelines(m_app->getDevice(), nullptr, 1, &compInfo, nullptr, &m_pipeline));
    NVVK_DBG_NAME(m_pipeline);
  }

  void onRender(VkCommandBuffer cmd) override
  {
    // Update animation time
    static auto startTime   = std::chrono::high_resolution_clock::now();
    auto        currentTime = std::chrono::high_resolution_clock::now();
    float       deltaTime   = std::chrono::duration<float>(currentTime - startTime).count();
    m_animationTime         = deltaTime * m_animationSpeed;

    // Perform animations
    if(m_enableInstanceAnimation)
    {
      animateInstances(cmd);
    }
    if(m_enableGeometryAnimation)
    {
      animateGeometry(cmd);
    }

    RtBase::onRender(cmd);
  }

private:
  // Animation methods
  void animateInstances(VkCommandBuffer cmd)
  {
    const int32_t nbWuson     = m_numWusonModels;
    const float   deltaAngle  = 6.28318530718f / static_cast<float>(nbWuson);
    const float   wusonLength = 3.f;
    const float   radius      = wusonLength / (2.f * sin(deltaAngle / 2.0f));
    const float   offset      = m_animationTime * 0.5f;

    for(int i = 0; i < nbWuson; i++)
    {
      int   wusonIdx  = i;  // Skip ground plane
      auto& transform = m_sceneResource.instances[wusonIdx].transform;
      transform       = glm::rotate(glm::mat4(1), i * deltaAngle + offset, glm::vec3(0.f, 1.f, 0.f))
                  * glm::translate(glm::mat4(1), glm::vec3(radius, 0.f, 0.f));

      m_tlasInstances[i].transform = nvvk::toTransformMatrixKHR(transform);  // Update the TLAS instance transform
    }


    // Update the instance buffer
    m_stagingUploader.appendBuffer(m_asBuilder.tlasInstancesBuffer, 0, std::span(m_tlasInstances));
    m_stagingUploader.cmdUploadAppended(cmd);

    // Make sure the copy of the instance buffer are copied before triggering the acceleration structure build
    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);

    if(m_asBuilder.tlasScratchBuffer.buffer == VK_NULL_HANDLE)
    {
      NVVK_CHECK(m_allocator.createBuffer(m_asBuilder.tlasScratchBuffer, m_asBuilder.tlasBuildData.sizeInfo.buildScratchSize,
                                          VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT));
      NVVK_DBG_NAME(m_asBuilder.tlasScratchBuffer.buffer);
    }

    // Building or updating the top-level acceleration structure
    m_asBuilder.tlasBuildData.cmdUpdateAccelerationStructure(cmd, m_asBuilder.tlas.accel, m_asBuilder.tlasScratchBuffer.address);

    // Barrier to make the acceleration structure available to the ray tracing pipeline
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
  }

  void animateGeometry(VkCommandBuffer cmd)
  {
    // Call the compute shader to animate the geometry
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    shaderio::VertexAnimationPushConstant pushConstant{};
    pushConstant.time = m_animationTime;
    pushConstant.meshBuffer =
        (shaderio::GltfMesh*)(m_sceneResource.bMeshes.address + (eSphere * sizeof(shaderio::GltfMesh)));  // Sphere mesh for geometry animation (offset #2)

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(shaderio::VertexAnimationPushConstant), &pushConstant);
    // There are 64 threads per group, so we need to dispatch the compute shader with the number of vertex
    uint32_t vertexCount = m_sceneResource.meshes[eSphere].triMesh.positions.count;
    uint32_t groupCount  = (vertexCount + 63) / 64;
    vkCmdDispatch(cmd, groupCount, 1, 1);


    // Barrier to make the geometry available to the ray tracing pipeline
    nvvk::accelerationStructureBarrier(cmd, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    // The Bottom level acceleration needs to be updated
    m_asBuilder.blasBuildData[eSphere].cmdUpdateAccelerationStructure(cmd, m_asBuilder.blasSet[eSphere].accel,
                                                                      m_asBuilder.blasScratchBuffer.address);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
  }

  void createBottomLevelAS() override
  {
    std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(m_sceneResource.meshes.size());
    for(uint32_t p_idx = 0; p_idx < m_sceneResource.meshes.size(); p_idx++)
    {
      geoInfos[p_idx] = primitiveToGeometry(m_sceneResource.meshes[p_idx]);
    }
    m_asBuilder.blasSubmitBuildAndWait(geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |  // <<--- For animation
                                                     VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createTopLevelAS() override
  {
    m_tlasInstances.reserve(m_sceneResource.instances.size());
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
      m_tlasInstances.emplace_back(ray_inst);
    }
    m_asBuilder.tlasSubmitBuildAndWait(m_tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |  // <<--- For animation
                                                            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

private:
  std::vector<VkAccelerationStructureInstanceKHR> m_tlasInstances;

  // Animation state
  float m_animationTime           = 0.0f;
  float m_animationSpeed          = 1.0f;
  bool  m_enableInstanceAnimation = true;
  bool  m_enableGeometryAnimation = false;
  int   m_numWusonModels          = 10;

  // Compute pipeline
  VkPipelineLayout m_pipelineLayout;
  VkPipeline       m_pipeline;
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
  appInfo.name           = "Ray Tracing Tutorial - 14 Animation";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<Rt14Animation>();
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
