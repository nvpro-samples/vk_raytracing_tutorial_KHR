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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */


//
// Ray Tracing Tutorial - Callable Shaders
//
// This sample demonstrates the use of callable shaders to implement a modular material system with procedural textures.
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
#include "_autogen/callable.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


class RtCallableShader : public RtBase
{

public:
  RtCallableShader()           = default;
  ~RtCallableShader() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------
  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Callable Shader");
      namespace PE = nvgui::PropertyEditor;


      // Object material assignment
      ImGui::Text("Object Materials:");
      if(PE::begin())
      {
        bool materialsChanged = false;
        for(int i = 0; i < m_sceneResource.instances.size(); i++)
        {
          std::string label = "Object " + std::to_string(i);
          if(PE::Combo(label.c_str(), &m_objectMaterials[i], "Diffuse\0Plastic\0Glass\0Constant\0"))
          {
            materialsChanged = true;
          }
        }
        PE::end();

        // Update TLAS when materials change
        if(materialsChanged)
        {
          vkDeviceWaitIdle(m_app->getDevice());
          createTopLevelAS();
        }
      }

      ImGui::Separator();

      // Texture type selection (global override)
      ImGui::Text("Texture Type:");
      if(PE::begin())
      {
        PE::RadioButton("None", &m_textureType, shaderio::TextureType::eNone);
        PE::RadioButton("Noise", &m_textureType, shaderio::TextureType::eNoise);
        PE::RadioButton("Checker", &m_textureType, shaderio::TextureType::eChecker);
        PE::RadioButton("Voronoi", &m_textureType, shaderio::TextureType::eVoronoi);
        // Animation controls
        PE::SliderFloat("Animation Speed", &m_animationSpeed, 0.0f, 2.0f, "%.2f");
        PE::end();
      }
    }
    ImGui::End();
    RtBase::onUIRender();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    m_sceneResource.sceneInfo.useSky = true;  // Use sky for the scene

    // Load the GLTF resources
    {
      tinygltf::Model shaderBallModel =
          nvsamples::loadGltfResources(nvutils::findFile("shader_ball.gltf", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      nvsamples::importGltfData(m_sceneResource, shaderBallModel, m_stagingUploader);  // Import the GLTF resources
    }

    // Meshes
    nvutils::PrimitiveMesh sphere = nvutils::createSphereUv(1.0);
    nvutils::PrimitiveMesh plane  = nvutils::createPlane(1, 20, 20);
    nvutils::PrimitiveMesh cube   = nvutils::createCube(1.0);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, sphere);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, plane);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, cube);

    // Create materials
    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.8f, 0.6f, 0.2f, 1.0f), .metallicFactor = 1.0f, .roughnessFactor = 0.1f},
        {.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), .metallicFactor = 0.0f, .roughnessFactor = 0.8f},
        {.baseColorFactor = glm::vec4(0.2f, 0.8f, 1.0f, 0.8f), .metallicFactor = 0.0f, .roughnessFactor = 0.0f},
        {.baseColorFactor = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), .metallicFactor = 0.0f, .roughnessFactor = 1.0f},
    };

    // Make instances of the meshes
    m_sceneResource.instances = {
        {.transform     = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(1.0f)),
         .materialIndex = 0,
         .meshIndex     = 0},  // Teapot
        {.transform = glm::translate(glm::mat4(1), glm::vec3(-2.5f, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(1.f)),
         .materialIndex = 2,
         .meshIndex     = 1},  // Sphere
        {.transform     = glm::translate(glm::mat4(1), glm::vec3(0, -2, 0)) * glm::scale(glm::mat4(1), glm::vec3(1.0f)),
         .materialIndex = 1,
         .meshIndex     = 2},  // Plane
        {.transform = glm::translate(glm::mat4(1), glm::vec3(2.5f, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(2.3f)),
         .materialIndex = 3,
         .meshIndex     = 3},  // Cube
    };

    // Initial material assignment (callable) for the objects
    m_objectMaterials = {
        shaderio::MaterialType::ePlastic,
        shaderio::MaterialType::eDiffuse,
        shaderio::MaterialType::eGlass,
        shaderio::MaterialType::eConstant,
    };

    // Create buffers for the scene data (GPU buffers)
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the resources
    m_app->submitAndWaitTempCmdBuffer(cmd);    // Submit the command buffer to upload the resources

    // Set the camera
    m_cameraManip->setLookat({-5.56733, 2.23931, 6.02600}, {0.00000, 0.00000, 0.00000}, {0.00000, 1.00000, 0.00000});
  }

  void createRayTracingPipeline() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("callable.slang", callable_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      // Material callable shaders
      eMaterialDiffuse,
      eMaterialPlastic,
      eMaterialGlass,
      eMaterialConstant,
      // Texture callable shaders
      eTextureNoise,
      eTextureChecker,
      eTextureVoronoi,
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

    // Material callable shaders
    stages[eMaterialDiffuse].pNext = &shaderCode;
    stages[eMaterialDiffuse].pName = "material_diffuse_main";
    stages[eMaterialDiffuse].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    stages[eMaterialPlastic].pNext = &shaderCode;
    stages[eMaterialPlastic].pName = "materialPlasticMain";
    stages[eMaterialPlastic].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    stages[eMaterialGlass].pNext = &shaderCode;
    stages[eMaterialGlass].pName = "materialGlassMain";
    stages[eMaterialGlass].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    stages[eMaterialConstant].pNext = &shaderCode;
    stages[eMaterialConstant].pName = "materialConstantMain";
    stages[eMaterialConstant].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    // Texture callable shaders
    stages[eTextureNoise].pNext = &shaderCode;
    stages[eTextureNoise].pName = "textureNoiseMain";
    stages[eTextureNoise].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    stages[eTextureChecker].pNext = &shaderCode;
    stages[eTextureChecker].pName = "textureCheckerMain";
    stages[eTextureChecker].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    stages[eTextureVoronoi].pNext = &shaderCode;
    stages[eTextureVoronoi].pName = "textureVoronoiMain";
    stages[eTextureVoronoi].stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

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

    // Material callable shaders
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.generalShader    = eMaterialDiffuse;
    shaderGroups.push_back(group);

    group.generalShader = eMaterialPlastic;
    shaderGroups.push_back(group);

    group.generalShader = eMaterialGlass;
    shaderGroups.push_back(group);

    group.generalShader = eMaterialConstant;
    shaderGroups.push_back(group);

    // Texture callable shaders
    group.generalShader = eTextureNoise;
    shaderGroups.push_back(group);

    group.generalShader = eTextureChecker;
    shaderGroups.push_back(group);

    group.generalShader = eTextureVoronoi;
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 6);
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

  // Override TLAS creation to set material type in instanceCustomIndex
  void createTopLevelAS() override
  {
    SCOPED_TIMER(__FUNCTION__);

    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size());
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    for(size_t i = 0; i < m_sceneResource.instances.size(); i++)
    {
      const shaderio::GltfInstance&      instance = m_sceneResource.instances[i];
      VkAccelerationStructureInstanceKHR ray_inst{};
      ray_inst.transform                      = nvvk::toTransformMatrixKHR(instance.transform);
      ray_inst.instanceCustomIndex            = m_objectMaterials[i];  // Use material type instead of mesh index
      ray_inst.accelerationStructureReference = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset = 0;
      ray_inst.flags                                  = flags;
      ray_inst.mask                                   = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }

    // Use appropriate build flags: allow updates and prefer fast trace
    VkBuildAccelerationStructureFlagsKHR buildFlags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    if(m_asBuilder.tlas.accel == VK_NULL_HANDLE)
    {
      // If TLAS is not created yet, create it
      m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, buildFlags);
    }
    else
    {
      // If TLAS already exists, update it
      m_asBuilder.tlasSubmitUpdateAndWait(tlasInstances);
    }
  }

  // Override the ray trace scene method to include our custom push constants
  void raytraceScene(VkCommandBuffer cmd) override
  {
    // Update time for animation
    m_time += m_animationSpeed * 0.016f;  // Assuming 60 FPS

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

    // Push descriptor sets for ray tracing
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_asBuilder.tlas);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage), m_gBuffers.getColorImageView(eImgRendered),
                 VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1, write.size(), write.data());

    // Push constant information with our custom data
    m_pushValues.sceneInfoAddress          = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;
    m_pushValues.metallicRoughnessOverride = m_metallicRoughnessOverride;
    m_pushValues.time                      = m_time;
    m_pushValues.textureType               = m_textureType;

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

    // Barrier to make sure the image is ready for Tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

private:
  // Texture and animation settings
  int              m_textureType    = shaderio::TextureType::eNone;
  float            m_animationSpeed = 1.0f;
  float            m_time           = 0.0f;
  std::vector<int> m_objectMaterials;
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
  appInfo.name           = "Ray Tracing Tutorial - Callable Shaders";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtCallableShader>();
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