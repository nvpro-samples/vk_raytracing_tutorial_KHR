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
// Ray Tracing Tutorial - 08 Intersection
//
// This sample demonstrates the use of intersection shaders to render implicit primitives.
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
#include "_autogen/rtintersection.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


class RtIntersection : public RtBase
{
public:
  RtIntersection()           = default;
  ~RtIntersection() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Intersection Shader");
      ImGui::Text("Rendering 2,000,000 implicit objects");
      ImGui::Text("Alternating spheres and cubes");
    }
    ImGui::End();
    RtBase::onUIRender();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // m_sceneInfo.useSky = true;
    m_sceneResource.sceneInfo.punctualLights[0].intensity = 1000.0f;
    m_sceneResource.sceneInfo.punctualLights[0].position  = glm::vec3(10.0f, 35.0f, 8.0f);  // Position of the light

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Load the GLTF resources
    {
      tinygltf::Model planeModel = nvsamples::loadGltfResources(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));
      // Import and create the glTF data buffer
      nvsamples::importGltfData(m_sceneResource, planeModel, m_stagingUploader, false);
    }


    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f},  // White
    };

    // Make instances of the meshes
    m_sceneResource.instances = {
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.9f, 0)), glm::vec3(5.f)), .materialIndex = 0, .meshIndex = 0},  // Plane
    };


    // Create implicit objects (spheres)
    createSpheres(2000000);

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources


    // Set the camera
    m_cameraManip->setLookat({21.70726, 29.00918, 27.13919}, {0.00000, 1.00000, 0.00000}, {0.00000, 1.00000, 0.00000});
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

    // Add BLAS for implicit objects (spheres/cubes)
    {
      nvvk::AccelerationStructureGeometryInfo blas = sphereToVkGeometryKHR();
      geoInfos.emplace_back(blas);
    }

    m_asBuilder.blasSubmitBuildAndWait(geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createTopLevelAS() override
  {
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size() + 1);  // +1 for implicit objects
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    // Add triangle instances
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

    // Add the BLAS containing all implicit objects
    {
      VkAccelerationStructureInstanceKHR rayInst{};
      rayInst.transform           = nvvk::toTransformMatrixKHR(glm::mat4(1));
      rayInst.instanceCustomIndex = static_cast<uint32_t>(m_sceneResource.instances.size());
      rayInst.accelerationStructureReference = m_asBuilder.blasSet[static_cast<uint32_t>(m_sceneResource.meshes.size())].address;
      rayInst.instanceShaderBindingTableRecordOffset = 1;  // Use hit group 1 for implicit objects
      rayInst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      rayInst.mask                                   = 0xFF;
      tlasInstances.emplace_back(rayInst);
    }

    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtintersection.slang", rtintersection_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eClosestHit2,
      eIntersection,
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for(auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    stages[eRaygen].pNext       = &shaderCode;
    stages[eRaygen].pName       = "rgenMain";
    stages[eRaygen].stage       = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext         = &shaderCode;
    stages[eMiss].pName         = "rmissMain";
    stages[eMiss].stage         = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext   = &shaderCode;
    stages[eClosestHit].pName   = "rchitMain";
    stages[eClosestHit].stage   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[eClosestHit2].pNext  = &shaderCode;
    stages[eClosestHit2].pName  = "rchitMain2";
    stages[eClosestHit2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[eIntersection].pNext = &shaderCode;
    stages[eIntersection].pName = "rintMain";
    stages[eIntersection].stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

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

    // Closest hit shader + Intersection for implicit objects
    group.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    group.closestHitShader   = eClosestHit2;
    group.intersectionShader = eIntersection;
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  void createRaytraceDescriptorLayout() override
  {
    // Adding access to the implicit objects
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eImplicit,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});

    RtBase::createRaytraceDescriptorLayout();
  }


  void raytraceScene(VkCommandBuffer cmd) override
  {
    // Push descriptor sets for ray tracing including implicit objects
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eImplicit), m_spheresBuffer.buffer, 0, VK_WHOLE_SIZE);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1, write.size(), write.data());

    // Normal ray tracing
    RtBase::raytraceScene(cmd);
  }

  void sampleDestroy() override
  {
    // Destroy implicit object buffers
    m_allocator.destroyBuffer(m_spheresBuffer);
    m_allocator.destroyBuffer(m_spheresAabbBuffer);
    m_allocator.destroyBuffer(m_spheresMatColorBuffer);
    m_allocator.destroyBuffer(m_spheresMatIndexBuffer);
  }

private:
  // Implicit object buffers
  std::vector<shaderio::Sphere> m_spheres;
  nvvk::Buffer                  m_spheresBuffer;
  nvvk::Buffer                  m_spheresAabbBuffer;
  nvvk::Buffer                  m_spheresMatColorBuffer;
  nvvk::Buffer                  m_spheresMatIndexBuffer;

  //--------------------------------------------------------------------------------------------------
  // Creating all spheres
  //
  void createSpheres(uint32_t nbSpheres)
  {
    std::random_device                    rd{};
    std::mt19937                          gen{rd()};
    std::normal_distribution<float>       xzd{0.f, 5.f};
    std::normal_distribution<float>       yd{6.f, 3.f};
    std::uniform_real_distribution<float> radd{.05f, .2f};

    // All spheres
    m_spheres.resize(nbSpheres);
    for(uint32_t i = 0; i < nbSpheres; i++)
    {
      shaderio::Sphere s;
      s.center     = glm::vec3(xzd(gen), yd(gen), xzd(gen));
      s.radius     = radd(gen);
      m_spheres[i] = std::move(s);
    }

    // Axis aligned bounding box of each sphere
    std::vector<shaderio::Aabb> aabbs;
    aabbs.reserve(nbSpheres);
    for(const auto& s : m_spheres)
    {
      shaderio::Aabb aabb;
      aabb.minimum = s.center - glm::vec3(s.radius);
      aabb.maximum = s.center + glm::vec3(s.radius);
      aabbs.emplace_back(aabb);
    }

    // Creating two materials
    shaderio::GltfMetallicRoughness mat{};
    mat.baseColorFactor = glm::vec4(0, 1, 1, 1);
    std::vector<shaderio::GltfMetallicRoughness> materials;
    std::vector<int>                             matIdx(nbSpheres);
    materials.emplace_back(mat);
    mat.baseColorFactor = glm::vec4(1, 1, 0, 1);
    materials.emplace_back(mat);

    // Assign a material to each sphere/box
    for(size_t i = 0; i < m_spheres.size(); i++)
    {
      matIdx[i] = i % 2;
    }

    // Creating all buffers
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();
      m_allocator.createBuffer(m_spheresBuffer, std::span(m_spheres).size_bytes(), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresBuffer, 0, std::span(m_spheres)));
      NVVK_DBG_NAME(m_spheresBuffer.buffer);

      m_allocator.createBuffer(m_spheresAabbBuffer, std::span(aabbs).size_bytes(),
                               VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                   | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresAabbBuffer, 0, std::span(aabbs)));
      NVVK_DBG_NAME(m_spheresAabbBuffer.buffer);

      m_allocator.createBuffer(m_spheresMatIndexBuffer, std::span(matIdx).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresMatIndexBuffer, 0, std::span(matIdx)));
      NVVK_DBG_NAME(m_spheresMatIndexBuffer.buffer);

      m_allocator.createBuffer(m_spheresMatColorBuffer, std::span(materials).size_bytes(),
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT);
      NVVK_CHECK(m_stagingUploader.appendBuffer(m_spheresMatColorBuffer, 0, std::span(materials)));
      NVVK_DBG_NAME(m_spheresMatColorBuffer.buffer);
      m_stagingUploader.cmdUploadAppended(cmd);
      m_app->submitAndWaitTempCmdBuffer(cmd);
    }

    // Adding an extra instance to which the implicit objects are attached
    shaderio::GltfInstance objDesc{};
    objDesc.materialIndex = 0;  // Use first material as default
    m_sceneResource.instances.emplace_back(objDesc);
  }

  //--------------------------------------------------------------------------------------------------
  // Returning the ray tracing geometry used for the BLAS, containing all spheres
  //
  nvvk::AccelerationStructureGeometryInfo sphereToVkGeometryKHR() const
  {
    // Setting up the build info of the acceleration for AABB
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType                      = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.flags                             = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.aabbs.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    geometry.geometry.aabbs.stride             = sizeof(shaderio::Aabb);
    geometry.geometry.aabbs.data.deviceAddress = m_spheresAabbBuffer.address;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{.primitiveCount = uint32_t(m_spheres.size())};  // Nb aabb

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
  appInfo.name           = "Ray Tracing Tutorial - 08 Intersection";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtIntersection>();
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