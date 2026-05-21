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
// Ray Tracing Tutorial - 02 Basic
//
// This is the sample before converting it to ray tracing.
// You will find in 02_basic the first transformation of the shader and further,
// different features of ray tracing.
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


#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"  // from nvpro_core2
#include "_autogen/tonemapper.slang.h"  //   "    "
#include "_autogen/foundation.slang.h"  // Local shader
#include "_autogen/rtbasic.slang.h"     // Local shader


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
class RtBasic : public nvapp::IAppElement
{
  // Type of GBuffers
  enum
  {
    eImgRendered,
    eImgTonemapped
  };

public:
  RtBasic()           = default;
  ~RtBasic() override = default;

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

    // m_allocator.setLeakID(14);  // Set a leak ID for the allocator to track memory leaks

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

    // Get ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    m_rtProperties.pNext = &m_asProperties;
    prop2.pNext          = &m_rtProperties;
    vkGetPhysicalDeviceProperties2(m_app->getPhysicalDevice(), &prop2);

    // Set up acceleration structure infrastructure
    createBottomLevelAS();  // Set up BLAS infrastructure
    createTopLevelAS();     // Set up TLAS infrastructure

    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout();  // Create descriptor layout
    createRayTracingPipeline();        // Create pipeline structure and SBT
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

    // Cleanup acceleration structures
    for(auto& blas : m_blasAccel)
    {
      m_allocator.destroyAcceleration(blas);
    }
    m_allocator.destroyAcceleration(m_tlasAccel);
    vkDestroyPipelineLayout(device, m_rtPipelineLayout, nullptr);
    vkDestroyPipeline(device, m_rtPipeline, nullptr);
    m_rtDescPack.deinit();
    m_allocator.destroyBuffer(m_sbtBuffer);

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
      // Ray tracing toggle
      ImGui::Checkbox("Use Ray Tracing", &m_useRayTracing);

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

    if(m_useRayTracing)
    {
      raytraceScene(cmd);
    }
    else
    {
      rasterScene(cmd);
    }

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

      if(m_useRayTracing)
      {
        createRayTracingPipeline();
      }
      else
      {
        compileAndCreateGraphicsShaders();
      }
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
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.8f, .baseColorTextureIndex = 0}};


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

  //--------------------------------------------------------------------------------------------------
  // Converting a PrimitiveMesh as input for BLAS
  //
  void primitiveToGeometry(const shaderio::GltfMesh&                 gltfMesh,
                           VkAccelerationStructureGeometryKHR&       geometry,
                           VkAccelerationStructureBuildRangeInfoKHR& rangeInfo)
  {
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
    geometry = VkAccelerationStructureGeometryKHR{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = {.triangles = triangles},
        .flags        = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = triangleCount};
  }

  //--------------------------------------------------------------------------------------------------
  // Generic function to create an acceleration structure (BLAS or TLAS)
  // Note: This function creates and destroys a scratch buffer for each call.
  // Not optimal but easier to read and understand. See Helper function for a better approach.
  void createAccelerationStructure(VkAccelerationStructureTypeKHR asType,  // The type of acceleration structure (BLAS or TLAS)
                                   nvvk::AccelerationStructure& accelStruct,  // The acceleration structure to create
                                   VkAccelerationStructureGeometryKHR& asGeometry,  // The geometry to build the acceleration structure from
                                   VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo,  // The range info for building the acceleration structure
                                   VkBuildAccelerationStructureFlagsKHR flags  // Build flags (e.g. prefer fast trace)
  )
  {
    VkDevice device = m_app->getDevice();

    // Helper function to align a value to a given alignment
    auto alignUp = [](auto value, size_t alignment) noexcept { return ((value + alignment - 1) & ~(alignment - 1)); };

    // Fill the build information with the current information, the rest is filled later (scratch buffer and destination AS)
    VkAccelerationStructureBuildGeometryInfoKHR asBuildInfo{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = asType,  // The type of acceleration structure (BLAS or TLAS)
        .flags         = flags,   // Build flags (e.g. prefer fast trace)
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,  // Build mode vs update
        .geometryCount = 1,                                               // Deal with one geometry at a time
        .pGeometries   = &asGeometry,  // The geometry to build the acceleration structure from
    };

    // One geometry at a time (could be multiple)
    std::vector<uint32_t> maxPrimCount(1);
    maxPrimCount[0] = asBuildRangeInfo.primitiveCount;

    // Find the size of the acceleration structure and the scratch buffer
    VkAccelerationStructureBuildSizesInfoKHR asBuildSize{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &asBuildInfo,
                                            maxPrimCount.data(), &asBuildSize);

    // Make sure the scratch buffer is properly aligned
    VkDeviceSize scratchSize = alignUp(asBuildSize.buildScratchSize, m_asProperties.minAccelerationStructureScratchOffsetAlignment);

    // Create the scratch buffer to store the temporary data for the build
    nvvk::Buffer scratchBuffer;
    NVVK_CHECK(m_allocator.createBuffer(scratchBuffer, scratchSize,
                                        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                            | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                        VMA_MEMORY_USAGE_AUTO, {}, m_asProperties.minAccelerationStructureScratchOffsetAlignment));

    // Create the acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .size  = asBuildSize.accelerationStructureSize,  // The size of the acceleration structure
        .type  = asType,                                 // The type of acceleration structure (BLAS or TLAS)
    };
    NVVK_CHECK(m_allocator.createAcceleration(accelStruct, createInfo));

    // Build the acceleration structure
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();

      // Fill with new information for the build,scratch buffer and destination AS
      asBuildInfo.dstAccelerationStructure  = accelStruct.accel;
      asBuildInfo.scratchData.deviceAddress = scratchBuffer.address;

      VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &asBuildRangeInfo;
      vkCmdBuildAccelerationStructuresKHR(cmd, 1, &asBuildInfo, &pBuildRangeInfo);

      m_app->submitAndWaitTempCmdBuffer(cmd);
    }
    // Cleanup the scratch buffer
    m_allocator.destroyBuffer(scratchBuffer);
  }


  //---------------------------------------------------------------------------------------------------------------
  // Create bottom-level acceleration structures
  void createBottomLevelAS()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Prepare geometry information for all meshes
    m_blasAccel.resize(m_sceneResource.meshes.size());

    // One BLAS per primitive
    for(uint32_t blasId = 0; blasId < m_sceneResource.meshes.size(); blasId++)
    {
      VkAccelerationStructureGeometryKHR       asGeometry{};
      VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

      // Convert the primitive information to acceleration structure geometry
      primitiveToGeometry(m_sceneResource.meshes[blasId], asGeometry, asBuildRangeInfo);

      createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, m_blasAccel[blasId], asGeometry,
                                  asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
      NVVK_DBG_NAME(m_blasAccel[blasId].accel);
    }
  }


  //--------------------------------------------------------------------------------------------------
  // Create the top level acceleration structures, referencing all BLAS
  //
  void createTopLevelAS()
  {
    SCOPED_TIMER(__FUNCTION__);

    // VkTransformMatrixKHR is row-major 3x4, glm::mat4 is column-major; transpose before memcpy.
    auto toTransformMatrixKHR = [](const glm::mat4& m) {
      VkTransformMatrixKHR t;
      memcpy(&t, glm::value_ptr(glm::transpose(m)), sizeof(t));
      return t;
    };

    // First create the instance data for the TLAS
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(m_sceneResource.instances.size());
    for(const shaderio::GltfInstance& instance : m_sceneResource.instances)
    {
      VkAccelerationStructureInstanceKHR asInstance{};
      asInstance.transform                      = toTransformMatrixKHR(instance.transform);  // Position of the instance
      asInstance.instanceCustomIndex            = instance.meshIndex;                       // gl_InstanceCustomIndexEXT
      asInstance.accelerationStructureReference = m_blasAccel[instance.meshIndex].address;  // Address of the BLAS
      asInstance.instanceShaderBindingTableRecordOffset = 0;  // We will use the same hit group for all objects
      asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;  // No culling - double sided
      asInstance.mask  = 0xFF;
      tlasInstances.emplace_back(asInstance);
    }

    // Then create the buffer with the instance data
    nvvk::Buffer tlasInstancesBuffer;
    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();

      // Create the instances buffer and upload the instance data
      // Required alignment for instance data
      // See: https://docs.vulkan.org/spec/latest/chapters/accelstructures.html#VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03717
      constexpr size_t         instanceAlignment = 16;
      VmaAllocationCreateFlags allocFlags        = {};
      NVVK_CHECK(m_allocator.createBuffer(
          tlasInstancesBuffer, std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances).size_bytes(),
          VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
          VMA_MEMORY_USAGE_AUTO, allocFlags, instanceAlignment));
      NVVK_CHECK(m_stagingUploader.appendBuffer(tlasInstancesBuffer, 0,
                                                std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances)));
      NVVK_DBG_NAME(tlasInstancesBuffer.buffer);
      m_stagingUploader.cmdUploadAppended(cmd);
      m_app->submitAndWaitTempCmdBuffer(cmd);
    }

    // Then create the TLAS geometry
    {
      VkAccelerationStructureGeometryKHR       asGeometry{};
      VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

      // Convert the instance information to acceleration structure geometry, similar to primitiveToGeometry()
      VkAccelerationStructureGeometryInstancesDataKHR geometryInstances{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                                                                        .data = {.deviceAddress = tlasInstancesBuffer.address}};
      asGeometry       = {.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                          .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
                          .geometry     = {.instances = geometryInstances}};
      asBuildRangeInfo = {.primitiveCount = static_cast<uint32_t>(m_sceneResource.instances.size())};

      createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, m_tlasAccel, asGeometry,
                                  asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
      NVVK_DBG_NAME(m_tlasAccel.accel);
    }

    m_allocator.destroyBuffer(tlasInstancesBuffer);  // Cleanup
  }

  //--------------------------------------------------------------------------------------------------
  // Create the descriptor set layout for ray tracing
  void createRaytraceDescriptorLayout()
  {
    SCOPED_TIMER(__FUNCTION__);
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding         = shaderio::BindingPoints::eTlas,
                         .descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                         .descriptorCount = 1,
                         .stageFlags      = VK_SHADER_STAGE_ALL});
    bindings.addBinding({.binding         = shaderio::BindingPoints::eOutImage,
                         .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                         .descriptorCount = 1,
                         .stageFlags      = VK_SHADER_STAGE_ALL});

    // Creating a PUSH descriptor set and set layout from the bindings
    m_rtDescPack.init(bindings, m_app->getDevice(), 0, VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create ray tracing pipeline structure
  // We create the entries for ray generation, miss, and closest hit shaders.
  // We also create the shader groups and the pipeline layout.
  // The pipeline is used to execute the ray tracing pipeline.
  // We also create the SBT (Shader Binding Table)
  void createRayTracingPipeline()
  {
    SCOPED_TIMER(__FUNCTION__);
    // For re-creation
    vkDestroyPipeline(m_app->getDevice(), m_rtPipeline, nullptr);
    vkDestroyPipelineLayout(m_app->getDevice(), m_rtPipelineLayout, nullptr);

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

    // Compile shader, fallback to pre-compiled
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtbasic.slang", rtbasic_slang);

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

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;
    // Raygen
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shader_groups.push_back(group);

    // Miss
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shader_groups.push_back(group);

    // closest hit shader
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shader_groups.push_back(group);

    // Push constant: we want to be able to update constants used by the shaders
    const VkPushConstantRange push_constant{VK_SHADER_STAGE_ALL, 0, sizeof(shaderio::TutoPushConstant)};

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges    = &push_constant;

    // Descriptor sets: one specific to ray tracing, and one shared with the rasterization pipeline
    std::array<VkDescriptorSetLayout, 2> layouts = {{m_descPack.getLayout(), m_rtDescPack.getLayout()}};
    pipeline_layout_create_info.setLayoutCount   = uint32_t(layouts.size());
    pipeline_layout_create_info.pSetLayouts      = layouts.data();
    vkCreatePipelineLayout(m_app->getDevice(), &pipeline_layout_create_info, nullptr, &m_rtPipelineLayout);
    NVVK_DBG_NAME(m_rtPipelineLayout);

    // Assemble the shader stages and recursion depth info into the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    rtPipelineInfo.stageCount                   = static_cast<uint32_t>(stages.size());  // Stages are shaders
    rtPipelineInfo.pStages                      = stages.data();
    rtPipelineInfo.groupCount                   = static_cast<uint32_t>(shader_groups.size());
    rtPipelineInfo.pGroups                      = shader_groups.data();
    rtPipelineInfo.maxPipelineRayRecursionDepth = std::max(3U, m_rtProperties.maxRayRecursionDepth);  // Ray depth
    rtPipelineInfo.layout                       = m_rtPipelineLayout;
    NVVK_CHECK(vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline));
    NVVK_DBG_NAME(m_rtPipeline);

    // Create the shader binding table for this pipeline
    createShaderBindingTable(rtPipelineInfo);
  }

  //--------------------------------------------------------------------------------------------------
  // Create the shader binding table
  // The shader binding table is a buffer that contains the shader handles for the ray tracing pipeline,
  // used to identify the shaders for the ray tracing pipeline.
  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
  {
    SCOPED_TIMER(__FUNCTION__);

    m_allocator.destroyBuffer(m_sbtBuffer);  // Cleanup when re-creating

    VkDevice device          = m_app->getDevice();
    uint32_t handleSize      = m_rtProperties.shaderGroupHandleSize;
    uint32_t handleAlignment = m_rtProperties.shaderGroupHandleAlignment;
    uint32_t baseAlignment   = m_rtProperties.shaderGroupBaseAlignment;
    uint32_t groupCount      = rtPipelineInfo.groupCount;

    // Get shader group handles
    size_t dataSize = handleSize * groupCount;
    m_shaderHandles.resize(dataSize);
    NVVK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(device, m_rtPipeline, 0, groupCount, dataSize, m_shaderHandles.data()));

    // Calculate SBT buffer size with proper alignment
    auto     alignUp      = [](uint32_t size, uint32_t alignment) { return (size + alignment - 1) & ~(alignment - 1); };
    uint32_t raygenSize   = alignUp(handleSize, handleAlignment);
    uint32_t missSize     = alignUp(handleSize, handleAlignment);
    uint32_t hitSize      = alignUp(handleSize, handleAlignment);
    uint32_t callableSize = 0;  // No callable shaders in this tutorial

    // Ensure each region starts at a baseAlignment boundary
    uint32_t raygenOffset   = 0;
    uint32_t missOffset     = alignUp(raygenSize, baseAlignment);
    uint32_t hitOffset      = alignUp(missOffset + missSize, baseAlignment);
    uint32_t callableOffset = alignUp(hitOffset + hitSize, baseAlignment);

    size_t bufferSize = callableOffset + callableSize;

    // Create SBT buffer
    NVVK_CHECK(m_allocator.createBuffer(m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate SBT buffer
    uint8_t* pData = static_cast<uint8_t*>(m_sbtBuffer.mapping);

    // Ray generation shader (group 0)
    memcpy(pData + raygenOffset, m_shaderHandles.data() + 0 * handleSize, handleSize);
    m_raygenRegion.deviceAddress = m_sbtBuffer.address + raygenOffset;
    m_raygenRegion.stride        = raygenSize;
    m_raygenRegion.size          = raygenSize;

    // Miss shader (group 1)
    memcpy(pData + missOffset, m_shaderHandles.data() + 1 * handleSize, handleSize);
    m_missRegion.deviceAddress = m_sbtBuffer.address + missOffset;
    m_missRegion.stride        = missSize;
    m_missRegion.size          = missSize;

    // Hit shader (group 2)
    memcpy(pData + hitOffset, m_shaderHandles.data() + 2 * handleSize, handleSize);
    m_hitRegion.deviceAddress = m_sbtBuffer.address + hitOffset;
    m_hitRegion.stride        = hitSize;
    m_hitRegion.size          = hitSize;

    // Callable shaders (none in this tutorial)
    m_callableRegion.deviceAddress = 0;
    m_callableRegion.stride        = 0;
    m_callableRegion.size          = 0;
  }


  //---------------------------------------------------------------------------------------------------------------
  // Ray tracing rendering method
  void raytraceScene(VkCommandBuffer cmd)
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
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_tlasAccel);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage), m_gBuffers.getColorImageView(eImgRendered),
                 VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1, write.size(), write.data());

    // Push constant information
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress          = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };
    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Ray trace
    const VkExtent2D& size = m_app->getViewportSize();
    vkCmdTraceRaysKHR(cmd, &m_raygenRegion, &m_missRegion, &m_hitRegion, &m_callableRegion, size.width, size.height, 1);

    // Barrier to make sure the image is ready for Tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

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

  // Ray Tracing Pipeline Components
  nvvk::DescriptorPack m_rtDescPack;          // Ray tracing descriptor bindings
  VkPipeline           m_rtPipeline{};        // Ray tracing pipeline
  VkPipelineLayout     m_rtPipelineLayout{};  // Ray tracing pipeline layout

  // Acceleration Structure Components
  std::vector<nvvk::AccelerationStructure> m_blasAccel;
  nvvk::AccelerationStructure              m_tlasAccel;

  // Direct SBT management
  nvvk::Buffer                    m_sbtBuffer;         // Buffer for shader binding table
  std::vector<uint8_t>            m_shaderHandles;     // Storage for shader group handles
  VkStridedDeviceAddressRegionKHR m_raygenRegion{};    // Ray generation shader region
  VkStridedDeviceAddressRegionKHR m_missRegion{};      // Miss shader region
  VkStridedDeviceAddressRegionKHR m_hitRegion{};       // Hit shader region
  VkStridedDeviceAddressRegionKHR m_callableRegion{};  // Callable shader region

  // Ray Tracing Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

  // Ray tracing toggle
  bool m_useRayTracing = true;  // Set to true to use ray tracing, false for rasterization
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

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // Build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // Use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
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
  auto tutorial   = std::make_shared<RtBasic>();               // Our tutorial element
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
