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
// Ray Tracing Tutorial - Base Class
//
// This is the base class for all ray tracing tutorial samples.
// It provides common functionality and interfaces for both rasterization and ray tracing pipelines.
// Derived classes should implement specific features and rendering logic for each tutorial step.
//

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include <nvaftermath/aftermath.hpp>         // Nsight Aftermath for crash tracking and shader debugging
#include <nvapp/application.hpp>             // Application framework
#include <nvapp/elem_camera.hpp>             // Camera manipulator
#include <nvapp/elem_default_title.hpp>      // Default title element
#include <nvapp/elem_default_menu.hpp>       // Default menu element
#include <nvgui/camera.hpp>                  // Camera widget
#include <nvgui/sky.hpp>                     // Sky widget
#include <nvgui/tonemapper.hpp>              // Tonemapper widget
#include <nvshaders_host/sky.hpp>            // Sky shader
#include <nvshaders_host/tonemapper.hpp>     // Tonemapper shader
#include <nvslang/slang.hpp>                 // Slang compiler
#include <nvutils/camera_manipulator.hpp>    // Camera manipulator
#include <nvutils/logger.hpp>                // Logger for debug messages
#include <nvutils/timers.hpp>                // Timers for profiling
#include <nvvk/context.hpp>                  // Vulkan context management
#include <nvvk/default_structs.hpp>          // Default Vulkan structures
#include <nvvk/descriptors.hpp>              // Descriptor set management
#include <nvvk/formats.hpp>                  // Finding Vulkan formats utilities
#include <nvvk/gbuffers.hpp>                 // GBuffer management
#include <nvvk/graphics_pipeline.hpp>        // Graphics pipeline management
#include <nvvk/sampler_pool.hpp>             // Sampler pool management
#include <nvvk/validation_settings.hpp>      // Validation settings for Vulkan
#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/sbt_generator.hpp>            // Shader binding table generator
#include <nvutils/parameter_parser.hpp>      // Parameter parser


#include "common/gltf_utils.hpp"  // GLTF utilities for loading and importing GLTF models
#include "common/utils.hpp"       // Common utilities for the sample application
#include "common/path_utils.hpp"  // Path utilities for handling resources file paths
#include "slang.h"


//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial
//
// Base class for all ray tracing samples
//
class RtBase : public nvapp::IAppElement
{
public:
  // Type of GBuffers
  enum
  {
    eImgRendered,
    eImgTonemapped
  };

  //-------------------------------------------------------------------------------
  // Virtual methods that must be overridden by derived classes
  //-------------------------------------------------------------------------------

  // Override to customize scene creation
  virtual void createScene() = 0;

  // Override to destroy local created resources
  virtual void sampleDestroy() {};

  // Override to customize ray tracing pipeline creation
  virtual void createRayTracingPipeline() = 0;

  // Override to customize rasterization rendering (optional)
  // virtual void rasterScene(VkCommandBuffer cmd);

  // Override to create rasterization shaders (optional)
  // virtual void createRasterizationShaders();


  RtBase()
  {
    // Scene information
    shaderio::GltfSceneInfo& sceneInfo    = m_sceneResource.sceneInfo;
    sceneInfo.useSky                      = false;                  // Use light
    sceneInfo.instances                   = nullptr;                // Address of the instance buffer
    sceneInfo.meshes                      = nullptr;                // Address of the mesh buffer
    sceneInfo.materials                   = nullptr;                // Address of the material buffer
    sceneInfo.backgroundColor             = {0.85f, 0.85f, 0.85f};  // The background color
    sceneInfo.numLights                   = 1;
    sceneInfo.punctualLights[0].color     = glm::vec3(1.0f, 1.0f, 1.0f);
    sceneInfo.punctualLights[0].intensity = 4.0f;
    sceneInfo.punctualLights[0].position  = glm::vec3(1.0f, 4.0f, -3.0f);  // Position of the light
    sceneInfo.punctualLights[0].direction = glm::vec3(1.0f, 1.0f, 1.0f);   // Direction to the light
    sceneInfo.punctualLights[0].type      = shaderio::GltfLightType::ePoint;
    sceneInfo.punctualLights[0].coneAngle = 0.9f;  // Cone angle for spot lights (0 for point and directional lights)
    m_cameraManip->setClipPlanes({0.01F, 100.0F});
  }
  ~RtBase() override = default;

  //-------------------------------------------------------------------------------
  // Core lifecycle methods - can be overridden by derived classes
  //-------------------------------------------------------------------------------

  void onAttach(nvapp::Application* app) override
  {
    m_app = app;

    // Initialize the VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {
        .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice   = app->getPhysicalDevice(),
        .device           = app->getDevice(),
        .instance         = app->getInstance(),
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };
    m_allocator.init(allocatorInfo);

    // m_allocator.setLeakID(14);  // Set a leak ID for the allocator to track memory leaks

    // The VMA allocator is used for all allocations, the staging uploader will use it for staging buffers and images
    m_stagingUploader.init(&m_allocator, true);

    // Setting up the Slang compiler for hot reload shader
    m_slangCompiler.addSearchPaths(nvsamples::getShaderDirs());
    m_slangCompiler.defaultTarget();
    m_slangCompiler.defaultOptions();
    m_slangCompiler.addOption({slang::CompilerOptionName::DebugInformation,
                               {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_STANDARD}});
    m_slangCompiler.addOption(
        {slang::CompilerOptionName::Optimization, {slang::CompilerOptionValueKind::Int, SLANG_OPTIMIZATION_LEVEL_NONE}});
#if defined(AFTERMATH_AVAILABLE)
    // This aftermath callback is used to report the shader hash (Spirv) to the Aftermath library.
    m_slangCompiler.setCompileCallback([&](const std::filesystem::path& sourceFile, const uint32_t* spirvCode, size_t spirvSize) {
      std::span<const uint32_t> data(spirvCode, spirvSize / sizeof(uint32_t));
      AftermathCrashTracker::getInstance().addShaderBinary(data);
    });
#endif

    // Adding capabilities to the Slang compiler, this will allow us to use the corresponding features in the shader code (like ray tracing, ray query, etc.)
    for(const auto& cap : m_slangCapabilities)
      m_slangCompiler.addCapability(cap.c_str());

    // Acquiring the texture sampler which will be used for displaying the GBuffer
    m_samplerPool.init(app->getDevice());
    VkSampler linearSampler{};
    NVVK_CHECK(m_samplerPool.acquireSampler(linearSampler));
    NVVK_DBG_NAME(linearSampler);

    // Create the G-Buffers
    createGBuffers(linearSampler);

    createScene();                        // Create the scene with a teapot and a plane
    createGraphicsDescriptorSetLayout();  // Create the descriptor set layout for the graphics pipeline
    createGraphicsPipelineLayout();       // Create the graphics pipeline layout
    updateTextures();                     // Update the textures in the descriptor set (if any)

    // Initialize the Sky with the pre-compiled shader
    m_skySimple.init(&m_allocator, std::span(sky_simple_slang));

    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(&m_allocator, std::span(tonemapper_slang));

    // Get ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_rtProperties;
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);

    // Initialize acceleration structure builder
    m_asBuilder.init(&m_allocator, &m_stagingUploader, m_app->getQueue(0));

    // Initialize SBT generator
    m_sbtGenerator.init(m_app->getDevice(), m_rtProperties);


    // Set up acceleration structure infrastructure
    createBottomLevelAS();  // Set up BLAS infrastructure
    createTopLevelAS();     // Set up TLAS infrastructure

    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout();  // Create descriptor layout
    createRayTracingPipeline();        // Create pipeline structure and SBT

    // Set up rasterization infrastructure (optional)
    createRasterizationShaders();  // Create rasterization shaders
  }

  //-------------------------------------------------------------------------------
  // Destroy all elements that were created
  // - Called when the application is shutting down
  //
  void onDetach() override
  {
    NVVK_CHECK(vkQueueWaitIdle(m_app->getQueue(0).queue));

    sampleDestroy();  // <<-- Allow derived class to destroy local resources

    // Base cleanup
    VkDevice device = m_app->getDevice();

    m_descPack.deinit();
    vkDestroyPipelineLayout(device, m_graphicPipelineLayout, nullptr);

    m_allocator.destroyBuffer(m_sceneResource.bSceneInfo);
    m_allocator.destroyBuffer(m_sceneResource.bMeshes);
    m_allocator.destroyBuffer(m_sceneResource.bMaterials);
    m_allocator.destroyBuffer(m_sceneResource.bInstances);
    for(auto& gltfData : m_sceneResource.bGltfDatas)
    {
      m_allocator.destroyBuffer(gltfData);
    }
    for(auto& texture : m_textures)
    {
      m_allocator.destroyImage(texture);
    }

    m_gBuffers.deinit();
    m_stagingUploader.deinit();
    m_skySimple.deinit();
    m_tonemapper.deinit();
    m_samplerPool.deinit();

    // Cleanup acceleration structures
    vkDestroyPipelineLayout(device, m_rtPipelineLayout, nullptr);
    vkDestroyPipeline(device, m_rtPipeline, nullptr);
    m_rtDescPack.deinit();
    m_allocator.destroyBuffer(m_sbtBuffer);

    m_asBuilder.deinitAccelerationStructures();
    m_asBuilder.deinit();
    m_sbtGenerator.deinit();

    m_allocator.deinit();
  }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering all UI elements, this includes the image of the GBuffer, the camera controls, and the sky parameters.
  // - Called every frame
  void onUIRender() override { renderUI(); }

  // Base for UI rendering
  // Return true if a UI parameter was modified
  bool renderUI()
  {
    bool modified = false;
    namespace PE  = nvgui::PropertyEditor;
    // Display the rendering GBuffer in the ImGui window ("Viewport")
    if(ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(eImgTonemapped)), ImGui::GetContentRegionAvail());
    }
    ImGui::End();

    // Setting panel
    if(ImGui::Begin("Settings"))
    {
      ImGui::Separator();

      if(ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_cameraManip);
      if(ImGui::CollapsingHeader("Lighting"))
      {
        shaderio::GltfSceneInfo& sceneInfo = m_sceneResource.sceneInfo;
        modified |= ImGui::Checkbox("Use Sky", (bool*)&sceneInfo.useSky);
        if(sceneInfo.useSky)
        {
          modified |= nvgui::skySimpleParametersUI(sceneInfo.skySimpleParam);
        }
        else
        {
          PE::begin();
          modified |= PE::ColorEdit3("Background", (float*)&sceneInfo.backgroundColor);
          PE::end();
          // Light
          PE::begin();
          if(sceneInfo.punctualLights[0].type == shaderio::GltfLightType::ePoint
             || sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            modified |= PE::DragFloat3("Light Position", glm::value_ptr(sceneInfo.punctualLights[0].position), 1.0,
                                       -20.0f, 20.0f, "%.2f", ImGuiSliderFlags_None, "Position of the light");
          }
          if(sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eDirectional
             || sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            modified |= PE::SliderFloat3("Light Direction", glm::value_ptr(sceneInfo.punctualLights[0].direction),
                                         -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_None, "Direction of the light");
          }

          modified |= PE::SliderFloat("Light Intensity", &sceneInfo.punctualLights[0].intensity, 0.0f, 1000.0f, "%.2f",
                                      ImGuiSliderFlags_Logarithmic, "Intensity of the light");
          modified |= PE::ColorEdit3("Light Color", glm::value_ptr(sceneInfo.punctualLights[0].color),
                                     ImGuiColorEditFlags_NoInputs, "Color of the light");
          modified |= PE::Combo("Light Type", (int*)&sceneInfo.punctualLights[0].type, "Point\0Spot\0Directional\0", 3,
                                "Type of the light (Point, Spot, Directional)");
          if(sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            modified |= PE::SliderAngle("Cone Angle", &sceneInfo.punctualLights[0].coneAngle, 0.f, 90.f, "%.2f",
                                        ImGuiSliderFlags_None, "Cone angle of the spot light");
          }
          PE::end();
        }
      }
      if(ImGui::CollapsingHeader("Tonemapper"))
      {
        // Tonemapper settings
        nvgui::tonemapperWidget(m_tonemapperData);
      }
      ImGui::Separator();
      PE::begin();
      modified |= PE::SliderFloat2("Metallic/Roughness Override", glm::value_ptr(m_metallicRoughnessOverride), -0.01f, 1.0f,
                                   "%.2f", ImGuiSliderFlags_AlwaysClamp, "Override all material metallic and roughness");
      PE::end();
    }
    ImGui::End();
    return modified;
  }

  //---------------------------------------------------------------------------------------------------------------
  // When the viewport is resized, the GBuffer must be resized
  // - Called when the Window "viewport is resized
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) override { NVVK_CHECK(m_gBuffers.update(cmd, size)); }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering the scene
  // The scene is rendered to a GBuffer and the GBuffer is displayed in the ImGui window.
  // Only the ImGui is rendered to the swapchain image.
  // - Called every frame
  void onRender(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Update the scene information buffer, this cannot be done in between dynamic rendering
    updateSceneBuffer(cmd);

    // Render scene (either rasterization or ray tracing)
    if(m_useRayTracing)
    {
      raytraceScene(cmd);  // Use ray tracing
    }
    else
    {
      rasterScene(cmd);  // Use rasterization
    }

    postProcess(cmd);
  }

  // Override to customize post-processing
  virtual void postProcess(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Default post-processing: tonemapping
    m_tonemapper.runCompute(cmd, m_gBuffers.getSize(), m_tonemapperData, m_gBuffers.getDescriptorImageInfo(eImgRendered),
                            m_gBuffers.getDescriptorImageInfo(eImgTonemapped));

    // Barrier to make sure the image is ready for been display
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
  }

  //---------------------------------------------------------------------------------------------------------------
  // This renders the toolbar of the window
  // - Called when the ImGui menu is rendered
  void onUIMenu() override
  {
    bool reload = false;
    if(ImGui::BeginMenu("Tools"))
    {
      reload |= ImGui::MenuItem("Reload Shaders", "F5");
      ImGui::EndMenu();
    }
    reload |= ImGui::IsKeyPressed(ImGuiKey_F5);
    if(reload)
    {
      vkQueueWaitIdle(m_app->getQueue(0).queue);
      createRayTracingPipeline();
    }
  }

  // Create and initialize Vulkan context
  static std::unique_ptr<nvvk::Context> createVulkanContext(nvvk::ContextInitInfo& vkSetup)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions);

    // Adding control on the validation layers
    nvvk::ValidationSettings validationSettings;
    validationSettings.setPreset(nvvk::ValidationSettings::LayerPresets::eStandard);
    vkSetup.instanceCreateInfoExt = validationSettings.buildPNextChain();

#if defined(USE_NSIGHT_AFTERMATH)
    // Adding the Aftermath extension to the device and initialize the Aftermath
    auto& aftermath = AftermathCrashTracker::getInstance();
    aftermath.initialize();
    aftermath.addExtensions(vkSetup.deviceExtensions);
    // The callback function is called when a validation error is triggered. This will wait to give time to dump the GPU crash.
    nvvk::CheckError::getInstance().setCallbackFunction([&](VkResult result) { aftermath.errorCallback(result); });
#endif

    // Initialize the Vulkan context
    auto vkContext = std::make_unique<nvvk::Context>();
    if(vkContext->init(vkSetup) != VK_SUCCESS)
    {
      LOGE("Error in Vulkan context creation\n");
      return nullptr;
    }

    return vkContext;
  }

  // Override to add more GBuffers
  virtual void createGBuffers(VkSampler linearSampler)
  {  // Create G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator      = &m_allocator,
        .colorFormats   = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
        .depthFormat    = nvvk::findDepthFormat(m_app->getPhysicalDevice()),
        .imageSampler   = linearSampler,
        .descriptorPool = m_app->getTextureDescriptorPool(),
    };
    m_gBuffers.init(gBufferInit);
  }

  //-------------------------------------------------------------------------------
  // Base implementation methods
  //-------------------------------------------------------------------------------

  void createGraphicsDescriptorSetLayout()
  {
    m_bindings.addBinding({.binding         = shaderio::BindingPoints::eTextures,
                           .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           .descriptorCount = 10,
                           .stageFlags      = VK_SHADER_STAGE_ALL},
                          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                              | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
    m_descPack.init(m_bindings, m_app->getDevice(), 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    NVVK_DBG_NAME(m_descPack.getLayout());
    NVVK_DBG_NAME(m_descPack.getPool());
    NVVK_DBG_NAME(m_descPack.getSet(0));
  }

  //--------------------------------------------------------------------------------------------------
  // The graphic pipeline is all the stages that are used to render a section of the scene.
  // Stages like: vertex shader, fragment shader, rasterization, and blending.
  //
  virtual void createGraphicsPipelineLayout()
  {
    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS, .offset = 0, .size = sizeof(shaderio::TutoPushConstant)};

    // The pipeline layout is used to pass data to the pipeline, anything with "layout" in the shader
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = m_descPack.getLayoutPtr(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange,
    };
    NVVK_CHECK(vkCreatePipelineLayout(m_app->getDevice(), &pipelineLayoutInfo, nullptr, &m_graphicPipelineLayout));
    NVVK_DBG_NAME(m_graphicPipelineLayout);

    m_dynamicPipeline.rasterizationState.cullMode = VK_CULL_MODE_NONE;  // Don't cull any triangles (double-sided rendering)
  }


  //--------------------------------------------------------------------------------------------------
  // Update the textures: this is called when the scene is loaded
  // Textures are updated in the descriptor set (0)
  void updateTextures()
  {
    if(m_textures.empty())
      return;

    // Update the descriptor set with the textures
    nvvk::WriteSetContainer write{};
    VkWriteDescriptorSet    allTextures =
        m_descPack.makeWrite(shaderio::BindingPoints::eTextures, 0, 0, uint32_t(m_textures.size()));
    nvvk::Image* allImages = m_textures.data();
    write.append(allTextures, allImages);
    vkUpdateDescriptorSets(m_app->getDevice(), write.size(), write.data(), 0, nullptr);
  }

  // This function is used to compile the Slang shader, and when it fails, it will use the pre-compiled shaders
  VkShaderModuleCreateInfo compileSlangShader(const std::filesystem::path& filename, const std::span<const uint32_t>& spirv)
  {
    SCOPED_TIMER(__FUNCTION__);

    // Use pre-compiled shaders by default
    VkShaderModuleCreateInfo shaderCode = nvsamples::getShaderModuleCreateInfo(spirv);

    // Try compiling the shader
    std::filesystem::path shaderSource = nvutils::findFile(filename, nvsamples::getShaderDirs());
    if(m_slangCompiler.compileFile(shaderSource))
    {
      // Using the Slang compiler to compile the shaders
      shaderCode.codeSize = m_slangCompiler.getSpirvSize();
      shaderCode.pCode    = m_slangCompiler.getSpirv();
    }
    else
    {
      LOGE("Error compiling shaders: %s\n%s\n", shaderSource.string().c_str(),
           m_slangCompiler.getLastDiagnosticMessage().c_str());
    }
    return shaderCode;
  }
  //---------------------------------------------------------------------------------------------------------------
  // The update of scene information buffer (UBO)
  //
  void updateSceneBuffer(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
    const glm::mat4& viewMatrix = m_cameraManip->getViewMatrix();
    const glm::mat4& projMatrix = m_cameraManip->getPerspectiveMatrix();

    m_sceneResource.sceneInfo.viewProjMatrix = projMatrix * viewMatrix;   // Combine the view and projection matrices
    m_sceneResource.sceneInfo.projInvMatrix  = glm::inverse(projMatrix);  // Inverse projection matrix
    m_sceneResource.sceneInfo.viewInvMatrix  = glm::inverse(viewMatrix);  // Inverse view matrix
    m_sceneResource.sceneInfo.cameraPosition = m_cameraManip->getEye();   // Get the camera position
    m_sceneResource.sceneInfo.instances = (shaderio::GltfInstance*)m_sceneResource.bInstances.address;  // Get the address of the instance buffer
    m_sceneResource.sceneInfo.meshes = (shaderio::GltfMesh*)m_sceneResource.bMeshes.address;  // Get the address of the mesh buffer
    m_sceneResource.sceneInfo.materials = (shaderio::GltfMetallicRoughness*)m_sceneResource.bMaterials.address;  // Get the address of the material buffer

    // Making sure the scene information buffer is updated before rendering
    // Wait that the fragment shader is done reading the previous scene information and wait for the transfer to complete
    nvvk::cmdBufferMemoryBarrier(cmd, {m_sceneResource.bSceneInfo.buffer, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                       VK_PIPELINE_STAGE_2_TRANSFER_BIT});
    vkCmdUpdateBuffer(cmd, m_sceneResource.bSceneInfo.buffer, 0, sizeof(shaderio::GltfSceneInfo), &m_sceneResource.sceneInfo);
    nvvk::cmdBufferMemoryBarrier(cmd, {m_sceneResource.bSceneInfo.buffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  }

  void onLastHeadlessFrame() override
  {
    m_app->saveImageToFile(m_gBuffers.getColorImage(eImgTonemapped), m_gBuffers.getSize(),
                           nvutils::getExecutablePath().replace_extension(".jpg").string());
  }

  // Accessor for camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> getCameraManipulator() const { return m_cameraManip; }

  //--------------------------------------------------------------------------------------------------
  // Converting a PrimitiveMesh as input for BLAS
  //
  virtual nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& gltfMesh)
  {
    nvvk::AccelerationStructureGeometryInfo result = {};

    const shaderio::TriangleMesh triMesh       = gltfMesh.triMesh;
    const auto                   triangleCount = static_cast<uint32_t>(triMesh.indices.count / 3U);

    // Describe buffer as array of VertexObj.
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,  // vec3 vertex position data
        .vertexData   = {.deviceAddress = VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.positions.offset},
        .vertexStride = triMesh.positions.byteStride,
        .maxVertex    = triMesh.positions.count - 1,
        .indexType    = VkIndexType(gltfMesh.indexType),  // Index type (VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32)
        .indexData    = {.deviceAddress = VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.indices.offset},
    };

    // Identify the above data as containing opaque triangles.
    result.geometry = VkAccelerationStructureGeometryKHR{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = {.triangles = triangles},
        .flags        = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    result.rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = triangleCount};

    return result;
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create bottom-level acceleration structures
  virtual void createBottomLevelAS()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Prepare geometry information for all meshes
    std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(m_sceneResource.meshes.size());
    for(uint32_t p_idx = 0; p_idx < m_sceneResource.meshes.size(); p_idx++)
    {
      geoInfos[p_idx] = primitiveToGeometry(m_sceneResource.meshes[p_idx]);
    }

    // Build the bottom-level acceleration structures
    m_asBuilder.blasSubmitBuildAndWait(geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }


  //--------------------------------------------------------------------------------------------------
  // Create the top level acceleration structures, referencing all BLAS
  //
  virtual void createTopLevelAS()
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
      ray_inst.instanceCustomIndex = instance.meshIndex;                              // gl_InstanceCustomIndexEXT
      ray_inst.accelerationStructureReference         = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset = 0;  // We will use the same hit group for all objects
      ray_inst.flags                                  = flags;
      ray_inst.mask                                   = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }

    // Build the top-level acceleration structure
    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create the descriptor set layout for ray tracing
  virtual void createRaytraceDescriptorLayout()
  {
    SCOPED_TIMER(__FUNCTION__);
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eTlas,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eOutImage,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});

    // Creating a PUSH descriptor set and set layout from the bindings
    m_rtDescPack.init(m_rtBindings, m_app->getDevice(), 0, VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  // Create Ray Trace Pipeline
  VkRayTracingPipelineCreateInfoKHR createRayTracingPipelineCreateInfo(std::span<const VkPipelineShaderStageCreateInfo> stages,
                                                                       std::span<const VkRayTracingShaderGroupCreateInfoKHR> shaderGroups,
                                                                       uint32_t depth)
  {

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

    // Assemble the shader stages and recursion depth info into the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount                   = static_cast<uint32_t>(stages.size()),  // Stages are shaders
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(shaderGroups.size()),
        .pGroups                      = shaderGroups.data(),
        .maxPipelineRayRecursionDepth = std::min(depth, m_rtProperties.maxRayRecursionDepth),  // Ray depth
        .layout                       = m_rtPipelineLayout,
    };
    return rtPipelineInfo;
  }

  void destroyRayTracingPipeline()
  {
    m_allocator.destroyBuffer(m_sbtBuffer);
    vkDestroyPipeline(m_app->getDevice(), m_rtPipeline, nullptr);
    vkDestroyPipelineLayout(m_app->getDevice(), m_rtPipelineLayout, nullptr);
  }


  // Creating the SBT
  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
  {
    // Calculate required SBT buffer size
    size_t bufferSize = m_sbtGenerator.calculateSBTBufferSize(m_rtPipeline, rtPipelineInfo);

    // Create SBT buffer using the size from above
    NVVK_CHECK(m_allocator.createBuffer(m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                        m_sbtGenerator.getBufferAlignment()));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate the SBT buffer with shader handles and data using the CPU-mapped memory pointer
    NVVK_CHECK(m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize, m_sbtBuffer.mapping));
  }


  //---------------------------------------------------------------------------------------------------------------
  // Ray tracing rendering method
  virtual void raytraceScene(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Ray trace pipeline
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

    // Push constant information, see usage later
    m_pushValues.sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;  // Pass the address of the scene information buffer to the shader
    m_pushValues.metallicRoughnessOverride = m_metallicRoughnessOverride;  // Override the metallic and roughness values

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

  //---------------------------------------------------------------------------------------------------------------
  // Rasterization rendering method (base implementation)
  // This provides basic rasterization support for tutorials that need G-buffer generation
  virtual void rasterScene(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Rendering the Sky
    if(m_sceneResource.sceneInfo.useSky)
    {
      const glm::mat4& viewMatrix = m_cameraManip->getViewMatrix();
      const glm::mat4& projMatrix = m_cameraManip->getPerspectiveMatrix();
      m_skySimple.runCompute(cmd, m_app->getViewportSize(), viewMatrix, projMatrix,
                             m_sceneResource.sceneInfo.skySimpleParam, m_gBuffers.getDescriptorImageInfo(eImgRendered));
    }

    // Rendering to the GBuffer
    VkRenderingAttachmentInfo colorAttachment = DEFAULT_VkRenderingAttachmentInfo;
    colorAttachment.loadOp = m_sceneResource.sceneInfo.useSky ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.imageView  = m_gBuffers.getColorImageView(eImgRendered);
    colorAttachment.clearValue = {.color = {m_sceneResource.sceneInfo.backgroundColor.x,
                                            m_sceneResource.sceneInfo.backgroundColor.y,
                                            m_sceneResource.sceneInfo.backgroundColor.z, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
    depthAttachment.imageView                 = m_gBuffers.getDepthImageView();
    depthAttachment.clearValue                = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

    // Create the rendering info
    VkRenderingInfo renderingInfo      = DEFAULT_VkRenderingInfo;
    renderingInfo.renderArea           = DEFAULT_VkRect2D(m_gBuffers.getSize());
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments    = &colorAttachment;
    renderingInfo.pDepthAttachment     = &depthAttachment;

    // Change the GBuffer layout to prepare for rendering (attachment)
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_GENERAL,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getDepthImage(),
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                      {VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}});

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{.sType      = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
                                                          .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                                          .layout     = m_graphicPipelineLayout,
                                                          .firstSet   = 0,
                                                          .descriptorSetCount = 1,
                                                          .pDescriptorSets    = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // ** BEGIN RENDERING **
    vkCmdBeginRendering(cmd, &renderingInfo);

    // All dynamic states are set here
    m_dynamicPipeline.cmdApplyAllStates(cmd);
    m_dynamicPipeline.cmdSetViewportAndScissor(cmd, m_app->getViewportSize());
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);

    // Bind shaders if they exist (for rasterization tutorials)
    if(m_vertexShader && m_fragmentShader)
    {
      m_dynamicPipeline.cmdBindShaders(cmd, {.vertex = m_vertexShader, .fragment = m_fragmentShader});
    }

    // We don't send vertex attributes, they are pulled in the shader
    VkVertexInputBindingDescription2EXT   bindingDescription   = {};
    VkVertexInputAttributeDescription2EXT attributeDescription = {};
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    // Push constant information
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress          = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };
    const VkPushConstantsInfo pushInfo{
        .sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
        .layout     = m_graphicPipelineLayout,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset     = 0,
        .size       = sizeof(shaderio::TutoPushConstant),
        .pValues    = &pushValues,
    };

    for(size_t i = 0; i < m_sceneResource.instances.size(); i++)
    {
      uint32_t                      meshIndex = m_sceneResource.instances[i].meshIndex;
      const shaderio::GltfMesh&     gltfMesh  = m_sceneResource.meshes[meshIndex];
      const shaderio::TriangleMesh& triMesh   = gltfMesh.triMesh;

      // Push constant is information that is passed to the shader at each draw call.
      pushValues.normalMatrix  = glm::transpose(glm::inverse(glm::mat3(m_sceneResource.instances[i].transform)));
      pushValues.instanceIndex = int(i);  // The index of the instance in the m_instances vector
      vkCmdPushConstants2(cmd, &pushInfo);

      // Get the buffer directly using the pre-computed mapping
      uint32_t            bufferIndex = m_sceneResource.meshToBufferIndex[meshIndex];
      const nvvk::Buffer& v           = m_sceneResource.bGltfDatas[bufferIndex];

      // Bind index buffers
      vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset, VkIndexType(gltfMesh.indexType));

      // Draw the mesh
      vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);  // All indices
    }

    // ** END RENDERING **
    vkCmdEndRendering(cmd);
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getDepthImage(),
                                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      {VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}});
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create rasterization shaders (base implementation)
  // This provides basic shader creation support for tutorials that need rasterization
  virtual void createRasterizationShaders()
  {
    // Base implementation does nothing - derived classes should override
    // and create their specific vertex/fragment shaders
  }


protected:
  // Application and core components
  nvapp::Application*     m_app{};             // The application framework
  nvvk::ResourceAllocator m_allocator{};       // Resource allocator for Vulkan resources, used for buffers and images
  nvvk::StagingUploader  m_stagingUploader{};  // Utility to upload data to the GPU, used for staging buffers and images
  nvvk::SamplerPool      m_samplerPool{};      // Texture sampler pool, used to acquire texture samplers for images
  nvvk::GBuffer          m_gBuffers{};         // The G-Buffer
  nvslang::SlangCompiler m_slangCompiler{};    // The Slang compiler used to compile the shaders
  std::vector<std::string> m_slangCapabilities{};  // Extra SPIR-V capabilities for hot-reload (e.g. spvRayQueryKHR); set in derived constructor

  // Camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{std::make_shared<nvutils::CameraManipulator>()};

  // Pipeline
  nvvk::GraphicsPipelineState m_dynamicPipeline;  // The dynamic pipeline state used to set the graphics pipeline state, like viewport, scissor, and depth test
  nvvk::DescriptorPack m_descPack;  // The descriptor bindings used to create the descriptor set layout and descriptor sets
  VkPipelineLayout m_graphicPipelineLayout{};  // The pipeline layout use with graphics pipeline
  nvvk::DescriptorBindings m_bindings;  // The descriptor bindings used to create the descriptor set layout and descriptor sets

  // Shaders (for rasterization)
  VkShaderEXT m_vertexShader{};    // The vertex shader used to render the scene
  VkShaderEXT m_fragmentShader{};  // The fragment shader used to render the scene

  // Rendering mode toggle
  bool m_useRayTracing = true;  // Set to true to use ray tracing, false for rasterization

  // Scene information buffer (UBO)
  nvsamples::GltfSceneResource m_sceneResource{};  // The GLTF scene resource, contains all the buffers and data for the scene
  std::vector<nvvk::Image> m_textures{};           // Textures used in the scene

  nvshaders::SkySimple     m_skySimple{};       // Sky rendering
  nvshaders::Tonemapper    m_tonemapper{};      // Tonemapper for post-processing effects
  shaderio::TonemapperData m_tonemapperData{};  // Tonemapper data used to pass parameters to the tonemapper shader
  glm::vec2 m_metallicRoughnessOverride{-0.01f, -0.01f};  // Override values for metallic and roughness, used in the UI to control the material properties

  // Ray Tracing Pipeline Components
  nvvk::DescriptorPack     m_rtDescPack;          // Ray tracing descriptor bindings
  VkPipeline               m_rtPipeline{};        // Ray tracing pipeline
  VkPipelineLayout         m_rtPipelineLayout{};  // Ray tracing pipeline layout
  nvvk::DescriptorBindings m_rtBindings;          // Ray tracing descriptor bindings

  shaderio::TutoPushConstant m_pushValues{};  // Push constant values used to pass data to the shaders

  // Acceleration Structure Components
  nvvk::AccelerationStructureHelper m_asBuilder{};   // Helper to create acceleration structures
  nvvk::SBTGenerator                m_sbtGenerator;  // Shader binding table wrapper
  nvvk::Buffer                      m_sbtBuffer;     // Buffer for shader binding table

  // Ray Tracing Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
};