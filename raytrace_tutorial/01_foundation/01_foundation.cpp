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

// Ray Tracing Tutorial - 01 Foundation
//
// This is the sample before converting it to ray tracing.
// You will find in 02_basic the first transformation of the shader and further,
// different features of ray tracing.
//


// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH  (not always on, as it slows down the application)


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


#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"  // from nvpro_core2
#include "_autogen/tonemapper.slang.h"  //   "    "
#include "_autogen/foundation.slang.h"  // Local shader


#include <nvaftermath/aftermath.hpp>       // Nsight Aftermath for crash tracking and shader debugging
#include <nvapp/application.hpp>           // Application framework
#include <nvapp/elem_camera.hpp>           // Camera manipulator
#include <nvapp/elem_default_title.hpp>    // Default title element
#include <nvapp/elem_default_menu.hpp>     // Default menu element
#include <nvgui/camera.hpp>                // Camera widget
#include <nvgui/sky.hpp>                   // Sky widget
#include <nvgui/tonemapper.hpp>            // Tonemapper widget
#include <nvshaders_host/sky.hpp>          // Sky shader
#include <nvshaders_host/tonemapper.hpp>   // Tonemapper shader
#include <nvslang/slang.hpp>               // Slang compiler
#include <nvutils/camera_manipulator.hpp>  // Camera manipulator
#include <nvutils/logger.hpp>              // Logger for debug messages
#include <nvutils/timers.hpp>              // Timers for profiling
#include <nvvk/context.hpp>                // Vulkan context management
#include <nvvk/default_structs.hpp>        // Default Vulkan structures
#include <nvvk/descriptors.hpp>            // Descriptor set management
#include <nvvk/formats.hpp>                // Finding Vulkan formats utilities
#include <nvvk/gbuffers.hpp>               // GBuffer management
#include <nvvk/graphics_pipeline.hpp>      // Graphics pipeline management
#include <nvvk/sampler_pool.hpp>           // Sampler pool management
#include <nvvk/validation_settings.hpp>    // Validation settings for Vulkan
#include <nvutils/parameter_parser.hpp>    // Parameter parser

#include "common/gltf_utils.hpp"  // GLTF utilities for loading and importing GLTF models
#include "common/utils.hpp"       // Common utilities for the sample application
#include "common/path_utils.hpp"  // Path utilities for handling resources file paths

//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial
//
// This is the base class before starting the ray tracing tutorial.
// It shows the rasterizer rendering of a scene with a teapot and a plane.
// The tutorial is starting from this class, and will add the ray tracing rendering.
//
class RtFoundation : public nvapp::IAppElement
{
  // Type of GBuffers
  enum
  {
    eImgRendered,
    eImgTonemapped
  };

public:
  RtFoundation()           = default;
  ~RtFoundation() override = default;

  //-------------------------------------------------------------------------------
  // Create the what is needed
  // - Called when the application initialize
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

    // The VMA allocator is used for all allocations, the staging uploader will use it for staging buffers and images
    m_stagingUploader.init(&m_allocator, true);

    // Setting up the Slang compiler for hot reload shader
    m_slangCompiler.addSearchPaths(nvsamples::getShaderDirs());
    m_slangCompiler.defaultTarget();
    m_slangCompiler.defaultOptions();
    m_slangCompiler.addOption({slang::CompilerOptionName::DebugInformation,
                               {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_MAXIMAL}});
#if defined(AFTERMATH_AVAILABLE)
    // This aftermath callback is used to report the shader hash (Spirv) to the Aftermath library.
    m_slangCompiler.setCompileCallback([&](const std::filesystem::path& sourceFile, const uint32_t* spirvCode, size_t spirvSize) {
      std::span<const uint32_t> data(spirvCode, spirvSize / sizeof(uint32_t));
      AftermathCrashTracker::getInstance().addShaderBinary(data);
    });
#endif

    // Acquiring the texture sampler which will be used for displaying the GBuffer
    m_samplerPool.init(app->getDevice());
    VkSampler linearSampler{};
    NVVK_CHECK(m_samplerPool.acquireSampler(linearSampler));
    NVVK_DBG_NAME(linearSampler);

    // Create the G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator      = &m_allocator,
        .colorFormats   = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},  // Render target, tonemapped
        .depthFormat    = nvvk::findDepthFormat(m_app->getPhysicalDevice()),
        .imageSampler   = linearSampler,
        .descriptorPool = m_app->getTextureDescriptorPool(),
    };
    m_gBuffers.init(gBufferInit);

    createScene();                        // Create the scene with a teapot and a plane
    createGraphicsDescriptorSetLayout();  // Create the descriptor set layout for the graphics pipeline
    createGraphicsPipelineLayout();       // Create the graphics pipeline layout
    compileAndCreateGraphicsShaders();    // Compile the graphics shaders and create the shader modules
    updateTextures();                     // Update the textures in the descriptor set (if any)

    // Initialize the Sky with the pre-compiled shader
    m_skySimple.init(&m_allocator, std::span(sky_simple_slang));

    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(&m_allocator, std::span(tonemapper_slang));
  }

  //-------------------------------------------------------------------------------
  // Destroy all elements that were created
  // - Called when the application is shutting down
  //
  void onDetach() override
  {
    NVVK_CHECK(vkQueueWaitIdle(m_app->getQueue(0).queue));

    VkDevice device = m_app->getDevice();

    m_descPack.deinit();
    vkDestroyPipelineLayout(device, m_graphicPipelineLayout, nullptr);
    vkDestroyShaderEXT(device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(device, m_fragmentShader, nullptr);

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
    m_allocator.deinit();
  }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering all UI elements, this includes the image of the GBuffer, the camera controls, and the sky parameters.
  // - Called every frame
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;
    // Display the rendering GBuffer in the ImGui window ("Viewport")
    if(ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(eImgTonemapped)), ImGui::GetContentRegionAvail());
    }
    ImGui::End();

    // Setting panel
    if(ImGui::Begin("Settings"))
    {
      if(ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_cameraManip);
      if(ImGui::CollapsingHeader("Environment"))
      {
        ImGui::Checkbox("Use Sky", (bool*)&m_sceneResource.sceneInfo.useSky);
        if(m_sceneResource.sceneInfo.useSky)
          nvgui::skySimpleParametersUI(m_sceneResource.sceneInfo.skySimpleParam);
        else
        {
          PE::begin();
          PE::ColorEdit3("Background", (float*)&m_sceneResource.sceneInfo.backgroundColor);
          PE::end();
          // Light
          PE::begin();
          if(m_sceneResource.sceneInfo.punctualLights[0].type == shaderio::GltfLightType::ePoint
             || m_sceneResource.sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            PE::DragFloat3("Light Position", glm::value_ptr(m_sceneResource.sceneInfo.punctualLights[0].position), 1.0f,
                           -20.0f, 20.0f, "%.2f", ImGuiSliderFlags_None, "Position of the light");
          }
          if(m_sceneResource.sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eDirectional
             || m_sceneResource.sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            PE::SliderFloat3("Light Direction", glm::value_ptr(m_sceneResource.sceneInfo.punctualLights[0].direction),
                             -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_None, "Direction of the light");
          }

          PE::SliderFloat("Light Intensity", &m_sceneResource.sceneInfo.punctualLights[0].intensity, 0.0f, 1000.0f,
                          "%.2f", ImGuiSliderFlags_Logarithmic, "Intensity of the light");
          PE::ColorEdit3("Light Color", glm::value_ptr(m_sceneResource.sceneInfo.punctualLights[0].color),
                         ImGuiColorEditFlags_NoInputs, "Color of the light");
          PE::Combo("Light Type", (int*)&m_sceneResource.sceneInfo.punctualLights[0].type, "Point\0Spot\0Directional\0",
                    3, "Type of the light (Point, Spot, Directional)");
          if(m_sceneResource.sceneInfo.punctualLights[0].type == shaderio::GltfLightType::eSpot)
          {
            PE::SliderAngle("Cone Angle", &m_sceneResource.sceneInfo.punctualLights[0].coneAngle, 0.f, 90.f, "%.2f",
                            ImGuiSliderFlags_AlwaysClamp, "Cone angle of the spot light");
          }
          PE::end();
        }
      }
      if(ImGui::CollapsingHeader("Tonemapper"))
      {
        nvgui::tonemapperWidget(m_tonemapperData);
      }
      ImGui::Separator();
      PE::begin();
      PE::SliderFloat2("Metallic/Roughness Override", glm::value_ptr(m_metallicRoughnessOverride), -0.01f, 1.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp, "Override all material metallic and roughness");
      PE::end();
    }
    ImGui::End();
  }

  //---------------------------------------------------------------------------------------------------------------
  // When the viewport is resized, the GBuffer must be resized
  // - Called when the Window "viewport is resized
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) { NVVK_CHECK(m_gBuffers.update(cmd, size)); }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering the scene
  // The scene is rendered to a GBuffer and the GBuffer is displayed in the ImGui window.
  // Only the ImGui is rendered to the swapchain image.
  // - Called every frame
  void onRender(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Update the scene information buffer, this cannot be done in between dynamic rendering
    updateSceneBuffer(cmd);

    rasterScene(cmd);

    postProcess(cmd);
  }

  // Apply post-processing
  void postProcess(VkCommandBuffer cmd)
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
      compileAndCreateGraphicsShaders();  // Recompile shaders on F5 key press
    }
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Load a teapot, a plane and an image.
  // - Create instances for them, assign a material and a transformation
  void createScene()
  {
    SCOPED_TIMER(__FUNCTION__);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Load the GLTF resources
    {
      tinygltf::Model teapotModel =
          nvsamples::loadGltfResources(nvutils::findFile("teapot.gltf", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      tinygltf::Model planeModel =
          nvsamples::loadGltfResources(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      // Textures
      {
        std::filesystem::path imageFilename = nvutils::findFile("tiled_floor.png", nvsamples::getResourcesDirs());
        nvvk::Image texture = nvsamples::loadAndCreateImage(cmd, m_stagingUploader, m_app->getDevice(), imageFilename);  // Load the image from the file and create a texture from it
        NVVK_DBG_NAME(texture.image);
        m_samplerPool.acquireSampler(texture.descriptor.sampler);
        m_textures.emplace_back(texture);  // Store the texture in the vector of textures
      }

      // Upload the GLTF resources to the GPU
      {
        nvsamples::importGltfData(m_sceneResource, teapotModel, m_stagingUploader);  // Import the GLTF resources
        nvsamples::importGltfData(m_sceneResource, planeModel, m_stagingUploader);   // Import the GLTF resources
      }
    }


    m_sceneResource.materials = {
        // Teapot material
        {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f), .metallicFactor = 0.5f, .roughnessFactor = 0.5f},
        // Plane material with texture
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f, .baseColorTextureIndex = -1}};


    m_sceneResource.instances = {
        // Teapot
        {.transform     = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.5f)),
         .materialIndex = 0,
         .meshIndex     = 0},
        // Plane
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.9f, 0)), glm::vec3(2.f)), .materialIndex = 1, .meshIndex = 1},
    };


    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);  // Create buffers for the scene data (GPU buffers)

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the scene information to the GPU

    // Scene information
    shaderio::GltfSceneInfo& sceneInfo = m_sceneResource.sceneInfo;
    sceneInfo.useSky                   = false;                                         // Use light
    sceneInfo.instances = (shaderio::GltfInstance*)m_sceneResource.bInstances.address;  // Address of the instance buffer
    sceneInfo.meshes = (shaderio::GltfMesh*)m_sceneResource.bMeshes.address;            // Address of the mesh buffer
    sceneInfo.materials = (shaderio::GltfMetallicRoughness*)m_sceneResource.bMaterials.address;  // Address of the material buffer
    sceneInfo.backgroundColor             = {0.85f, 0.85f, 0.85f};                               // The background color
    sceneInfo.numLights                   = 1;
    sceneInfo.punctualLights[0].color     = glm::vec3(1.0f, 1.0f, 1.0f);
    sceneInfo.punctualLights[0].intensity = 4.0f;
    sceneInfo.punctualLights[0].position  = glm::vec3(1.0f, 1.0f, 1.0f);  // Position of the light
    sceneInfo.punctualLights[0].direction = glm::vec3(1.0f, 1.0f, 1.0f);  // Direction to the light
    sceneInfo.punctualLights[0].type      = shaderio::GltfLightType::ePoint;
    sceneInfo.punctualLights[0].coneAngle = 0.9f;  // Cone angle for spot lights (0 for point and directional lights)

    m_app->submitAndWaitTempCmdBuffer(cmd);  // Submit the command buffer to upload the resources

    // Default camera
    m_cameraManip->setClipPlanes({0.01F, 100.0F});
    m_cameraManip->setLookat({0.0F, 0.5F, 5.0}, {0.F, 0.F, 0.F}, {0.0F, 1.0F, 0.0F});
  }


  //---------------------------------------------------------------------------------------------------------------
  // The Vulkan descriptor set defines the resources that are used by the shaders.
  // Here we add the bindings for the textures.
  void createGraphicsDescriptorSetLayout()
  {
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding         = shaderio::BindingPoints::eTextures,
                         .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         .descriptorCount = 10,  // Maximum number of textures used in the scene
                         .stageFlags      = VK_SHADER_STAGE_ALL},
                        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                            | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
    // Creating the descriptor set and set layout from the bindings
    m_descPack.init(bindings, m_app->getDevice(), 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    NVVK_DBG_NAME(m_descPack.getLayout());
    NVVK_DBG_NAME(m_descPack.getPool());
    NVVK_DBG_NAME(m_descPack.getSet(0));
  }


  //--------------------------------------------------------------------------------------------------
  // The graphic pipeline is all the stages that are used to render a section of the scene.
  // Stages like: vertex shader, fragment shader, rasterization, and blending.
  //
  void createGraphicsPipelineLayout()
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
  // Compile the graphics shaders and create the shader modules.
  // This function only creates vertex and fragment shader modules for the graphics pipeline.
  // The actual graphics pipeline is created elsewhere and uses these shader modules.
  // This function will use the pre-compiled shaders if the compilation fails.
  void compileAndCreateGraphicsShaders()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Use pre-compiled shaders by default
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("foundation.slang", foundation_slang);

    // Destroy the previous shaders if they exist
    vkDestroyShaderEXT(m_app->getDevice(), m_vertexShader, nullptr);
    vkDestroyShaderEXT(m_app->getDevice(), m_fragmentShader, nullptr);

    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset     = 0,
        .size       = sizeof(shaderio::TutoPushConstant),
    };

    // Shader create information, this is used to create the shader modules
    VkShaderCreateInfoEXT shaderInfo{
        .sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .pName                  = "main",
        .setLayoutCount         = 1,
        .pSetLayouts            = m_descPack.getLayoutPtr(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange,
    };

    // Vertex Shader
    shaderInfo.stage     = VK_SHADER_STAGE_VERTEX_BIT;
    shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderInfo.pName     = "vertexMain";  // The entry point of the vertex shader
    shaderInfo.codeSize  = shaderCode.codeSize;
    shaderInfo.pCode     = shaderCode.pCode;
    vkCreateShadersEXT(m_app->getDevice(), 1U, &shaderInfo, nullptr, &m_vertexShader);
    NVVK_DBG_NAME(m_vertexShader);

    // Fragment Shader
    shaderInfo.stage     = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderInfo.nextStage = 0;
    shaderInfo.pName     = "fragmentMain";  // The entry point of the vertex shader
    shaderInfo.codeSize  = shaderCode.codeSize;
    shaderInfo.pCode     = shaderCode.pCode;
    vkCreateShadersEXT(m_app->getDevice(), 1U, &shaderInfo, nullptr, &m_fragmentShader);
    NVVK_DBG_NAME(m_fragmentShader);
  }

  //---------------------------------------------------------------------------------------------------------------
  // The update of scene information buffer (UBO)
  //
  void updateSceneBuffer(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
    const glm::mat4& viewMatrix = m_cameraManip->getViewMatrix();
    const glm::mat4& projMatrix = m_cameraManip->getPerspectiveMatrix();

    m_sceneResource.sceneInfo.viewProjMatrix = projMatrix * viewMatrix;  // Combine the view and projection matrices
    m_sceneResource.sceneInfo.cameraPosition = m_cameraManip->getEye();  // Get the camera position
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


  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void rasterScene(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Push constant information, see usage later
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address,  // Pass the address of the scene information buffer to the shader
        .metallicRoughnessOverride = m_metallicRoughnessOverride,  // Override the metallic and roughness values
    };
    const VkPushConstantsInfo pushInfo{
        .sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
        .layout     = m_graphicPipelineLayout,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset     = 0,
        .size       = sizeof(shaderio::TutoPushConstant),
        .pValues    = &pushValues,  // Other values are passed later
    };

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
    colorAttachment.loadOp = m_sceneResource.sceneInfo.useSky ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;  // Load the previous content of the GBuffer color attachment (Sky rendering)
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
    m_dynamicPipeline.rasterizationState.cullMode = VK_CULL_MODE_NONE;  // Don't cull any triangles (double-sided rendering)
    m_dynamicPipeline.cmdApplyAllStates(cmd);
    m_dynamicPipeline.cmdSetViewportAndScissor(cmd, m_app->getViewportSize());
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);

    // Same shader for all meshes
    m_dynamicPipeline.cmdBindShaders(cmd, {.vertex = m_vertexShader, .fragment = m_fragmentShader});


    // We don't send vertex attributes, they are pulled in the shader
    VkVertexInputBindingDescription2EXT   bindingDescription   = {};
    VkVertexInputAttributeDescription2EXT attributeDescription = {};
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

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

  void onLastHeadlessFrame() override
  {
    m_app->saveImageToFile(m_gBuffers.getColorImage(eImgTonemapped), m_gBuffers.getSize(),
                           nvutils::getExecutablePath().replace_extension(".jpg").string());
  }

  // Accessor for camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> getCameraManipulator() const { return m_cameraManip; }

private:
  // Application and core components
  nvapp::Application*     m_app{};             // The application framework
  nvvk::ResourceAllocator m_allocator{};       // Resource allocator for Vulkan resources, used for buffers and images
  nvvk::StagingUploader  m_stagingUploader{};  // Utility to upload data to the GPU, used for staging buffers and images
  nvvk::SamplerPool      m_samplerPool{};      // Texture sampler pool, used to acquire texture samplers for images
  nvvk::GBuffer          m_gBuffers{};         // The G-Buffer
  nvslang::SlangCompiler m_slangCompiler{};    // The Slang compiler used to compile the shaders

  // Camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{std::make_shared<nvutils::CameraManipulator>()};

  // Pipeline
  nvvk::GraphicsPipelineState m_dynamicPipeline;  // The dynamic pipeline state used to set the graphics pipeline state, like viewport, scissor, and depth test
  nvvk::DescriptorPack m_descPack;  // The descriptor bindings used to create the descriptor set layout and descriptor sets
  VkPipelineLayout m_graphicPipelineLayout{};  // The pipeline layout use with graphics pipeline

  // Shaders
  VkShaderEXT m_vertexShader{};    // The vertex shader used to render the scene
  VkShaderEXT m_fragmentShader{};  // The fragment shader used to render the scene


  // Scene information buffer (UBO)
  nvsamples::GltfSceneResource m_sceneResource{};  // The GLTF scene resource, contains all the buffers and data for the scene
  std::vector<nvvk::Image> m_textures{};           // Textures used in the scene

  nvshaders::SkySimple     m_skySimple{};       // Sky rendering
  nvshaders::Tonemapper    m_tonemapper{};      // Tonemapper for post-processing effects
  shaderio::TonemapperData m_tonemapperData{};  // Tonemapper data used to pass parameters to the tonemapper shader
  glm::vec2 m_metallicRoughnessOverride{-0.01f, -0.01f};  // Override values for metallic and roughness, used in the UI to control the material properties
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
  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
          },
  };
  if(!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

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
  nvvk::Context vkContext;
  if(vkContext.init(vkSetup) != VK_SUCCESS)
  {
    LOGE("Error in Vulkan context creation\n");
    return 1;
  }

  // Setting up the application
  appInfo.name           = "Ray Tracing Tutorial";
  appInfo.instance       = vkContext.getInstance();
  appInfo.device         = vkContext.getDevice();
  appInfo.physicalDevice = vkContext.getPhysicalDevice();
  appInfo.queues         = vkContext.getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial   = std::make_shared<RtFoundation>();          // Our tutorial element
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
  vkContext.deinit();    // De-initialize the Vulkan context

  return 0;
}
