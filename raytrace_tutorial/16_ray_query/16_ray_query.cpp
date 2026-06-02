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
// Ray Tracing Tutorial - 16 Ray Query
//
// This sample demonstrates the use of ray queries in compute shaders.
// Ray queries allow you to perform ray tracing operations directly within compute shaders
// without needing a full ray tracing pipeline. This is useful for techniques like
// screen-space reflections, ambient occlusion, or other effects that need to trace rays
// from within compute shaders.
//


// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH  // (not always on, as it slows down the application)


#define TINYGLTF_IMPLEMENTATION         // Implementation of the GLTF loader library
#define STB_IMAGE_IMPLEMENTATION        // Implementation of the image loading library
#define STB_IMAGE_WRITE_IMPLEMENTATION  // Implementation of the image writing library
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1  // Use dynamic Vulkan functions for VMA (Vulkan Memory Allocator)
#define VMA_IMPLEMENTATION              // Implementation of the Vulkan Memory Allocator
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
  {                                                                                                                    \
    printf((format), __VA_ARGS__);                                                                                     \
    printf("\n");                                                                                                      \
  }

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"  // from nvpro_core2
#include "_autogen/tonemapper.slang.h"  //   "    "
#include "_autogen/ray_query.slang.h"   // Local shader

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial - 16 Ray Query
//
// This class demonstrates the use of ray queries in compute shaders.
// Unlike traditional ray tracing pipelines, ray queries allow you to perform
// ray tracing operations directly within compute shaders, providing more flexibility
// for implementing various rendering techniques.
//
class Rt16RayQuery : public RtBase
{

public:
  Rt16RayQuery() { m_slangCapabilities = {"spvRayQueryKHR"}; }
  ~Rt16RayQuery() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  //---------------------------------------------------------------------------------------------------------------
  // Rendering all UI elements, this includes the image of the GBuffer, the camera controls, and the sky parameters.
  // - Called every frame
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;
    bool changed = false;
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("16 Ray Query");
      if(PE::begin())
      {
        PE::DragInt("Max Frames", (int*)&m_maxFrames, 1, 1, 1000);
        ImGui::TextDisabled("Max Frames: %d", m_pushValues.frame);
        changed = PE::SliderInt("Max Depth", (int*)&m_pushValues.maxDepth, 1, 10);
        PE::end();
      }
    }
    ImGui::End();
    changed |= RtBase::renderUI();
    if(changed)
    {
      resetFrame();
    }
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Create primitive meshes (cube and sphere) and load a GLTF model
  // - Create instances for them, assign materials and transformations
  // - Set up lighting and camera for the ray query demonstration
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Create primitives
    nvutils::PrimitiveMesh cube   = nvutils::createCube();
    nvutils::PrimitiveMesh sphere = nvutils::createSphereUv(10, 10);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, cube);
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, sphere);
    m_sceneResource.instances = {{
                                     .transform     = glm::scale(glm::mat4(1), glm::vec3(10.0f, 0.01f, 10.0f)),
                                     .materialIndex = 1,
                                     .meshIndex     = 0,
                                 },
                                 {
                                     .transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(1.5f, 0.5f, 0.0f)),
                                                             glm::vec3(0.1f, 0.1f, 0.1f)),
                                     .materialIndex = 2,
                                     .meshIndex     = 1,
                                 }};

    // Load the GLTF resources
    {
      tinygltf::Model meetMat =
          nvsamples::loadGltfResources(nvutils::findFile("meet_mat.glb", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      const bool importAllInstances = true;
      nvsamples::importGltfData(m_sceneResource, meetMat, m_stagingUploader, importAllInstances);  // Import the GLTF resources
    }

    m_sceneResource.materials = {
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},
        {.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.05f},
        {.baseColorFactor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f), .metallicFactor = 0.05f, .roughnessFactor = 0.95f},
    };

    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    m_stagingUploader.cmdUploadAppended(cmd);
    m_app->submitAndWaitTempCmdBuffer(cmd);  // Submit the command buffer to upload the resources

    // Set the camera
    m_cameraManip->setCamera(
        {{-1.8179423, 1.0988777, 2.4955902}, {0.15707326, 0.9963042, 0.089965165}, {0, 1, 0}, {60}, {0.01, 100}});
    m_sceneResource.sceneInfo.useSky = true;

    m_sceneResource.sceneInfo.punctualLights[0].position  = {2.0f, 4.0f, 4.0f};
    m_sceneResource.sceneInfo.punctualLights[0].direction = {.7f, 1.0f, 1.0f};
    m_sceneResource.sceneInfo.punctualLights[0].color     = {1.0f, 1.0f, 1.0f};
    m_sceneResource.sceneInfo.punctualLights[0].intensity = 50.0f;
    m_sceneResource.sceneInfo.punctualLights[0].type      = shaderio::GltfLightType::eSpot;
    m_sceneResource.sceneInfo.backgroundColor             = {0.03f, 0.03f, 0.03f};
  }

  //--------------------------------------------------------------------------------------------------
  // Create compute pipeline for ray query
  // Unlike traditional ray tracing pipelines, ray queries use compute shaders
  // This creates a compute pipeline that will execute the ray query shader
  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("ray_query.slang", ray_query_slang);

    // Push constant: we want to be able to update constants used by the shaders
    const VkPushConstantRange pushConstant{VK_SHADER_STAGE_ALL, 0, sizeof(shaderio::TutoPushConstant)};

    // Descriptor sets: one specific to ray tracing, and one shared with the rasterization pipeline
    std::array<VkDescriptorSetLayout, 2> layouts = {{m_descPack.getLayout(), m_rtDescPack.getLayout()}};
    VkPipelineLayoutCreateInfo           pipelineLayoutCreateInfo{
                  .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                  .setLayoutCount         = uint32_t(layouts.size()),
                  .pSetLayouts            = layouts.data(),
                  .pushConstantRangeCount = 1,
                  .pPushConstantRanges    = &pushConstant,
    };
    vkCreatePipelineLayout(m_app->getDevice(), &pipelineLayoutCreateInfo, nullptr, &m_rtPipelineLayout);
    NVVK_DBG_NAME(m_rtPipelineLayout);

    VkPipelineShaderStageCreateInfo shaderStage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = &shaderCode,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .pName = "main",
    };

    VkComputePipelineCreateInfo cpCreateInfo{
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = shaderStage,
        .layout = m_rtPipelineLayout,
    };

    NVVK_CHECK(vkCreateComputePipelines(m_app->getDevice(), {}, 1, &cpCreateInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);
  }


  //---------------------------------------------------------------------------------------------------------------
  // Ray query rendering method
  // This method executes the compute shader that performs ray queries
  // Unlike traditional ray tracing, this uses a compute pipeline with ray queries
  void raytraceScene(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    updateFrame();
    if(m_pushValues.frame >= m_maxFrames)
      return;

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
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipelineLayout, 1, write.size(), write.data());

    // Push constant information, see usage later
    m_pushValues.sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;  // Pass the address of the scene information buffer to the shader
    m_pushValues.metallicRoughnessOverride = m_metallicRoughnessOverride;  // Override the metallic and roughness values

    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &m_pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);


    // Execute the compute shader with ray queries
    const VkExtent2D& size = m_app->getViewportSize();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipeline);
    vkCmdDispatch(cmd, (size.width + (WORKGROUP_SIZE - 1)) / WORKGROUP_SIZE,
                  (size.height + (WORKGROUP_SIZE - 1)) / WORKGROUP_SIZE, 1);

    // Making sure the rendered image is ready to be used by tonemapper
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  //---------------------------------------------------------------------------------------------------------------
  // Frame management functions for progressive rendering
  // These functions handle frame accumulation for progressive ray query rendering
  //---------------------------------------------------------------------------------------------------------------

  // Reset the frame counter to restart progressive rendering
  void resetFrame() { m_pushValues.frame = -1; }

  // Update the frame counter and reset if camera changes
  // This enables progressive rendering where each frame accumulates more samples
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

  //---------------------------------------------------------------------------------------------------------------
  // When the viewport is resized, reset the frame counter for progressive rendering
  // - Called when the Window viewport is resized
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) override
  {
    resetFrame();
    RtBase::onResize(cmd, size);
  }

private:
  uint32_t m_maxFrames = 200;  // Maximum number of frames for accumulation
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

  // Add ray tracing features
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDeviceRayQueryFeaturesKHR rayqueryFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // Build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // Use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_KHR_RAY_QUERY_EXTENSION_NAME, &rayqueryFeature},  // Enable ray queries in compute shaders
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
  appInfo.name           = "Ray Tracing Tutorial - 16 Ray Query";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial   = std::make_shared<Rt16RayQuery>();          // Our tutorial element
  auto elemCamera = std::make_shared<nvapp::ElementCamera>();  // Element to control the camera movement
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();  // Element displaying the window title with application name and size
  auto windowMenu = std::make_shared<nvapp::ElementDefaultMenu>();  // Element displaying a menu, File->Exit ...
  auto camManip   = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Adding all elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();     // Start the application, loop until the window is closed
  application.deinit();  // Closing application
  vkContext->deinit();   // De-initialize the Vulkan context

  return 0;
}