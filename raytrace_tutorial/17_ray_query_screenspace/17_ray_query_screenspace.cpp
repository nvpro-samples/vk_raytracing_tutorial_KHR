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
// Ray Tracing Tutorial - 17 Ray Query Screen-Space Effects
//
// This sample demonstrates practical ray query usage in compute shaders for
// screen-space effects like ambient occlusion. Unlike tutorial 16 which shows
// complex Monte Carlo path tracing, this tutorial focuses on simple, practical
// ray query applications that can be integrated into existing rasterization
// pipelines.
//
// Key Features:
// - Compute shader ray queries for screen-space ambient occlusion
// - Integration with rasterization pipeline using G-buffer
// - Performance optimization by processing only visible pixels
// - Real-time screen-space effects with ray tracing
//

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
#include "_autogen/sky_simple.slang.h"             // from nvpro_core2
#include "_autogen/tonemapper.slang.h"             //   "    "
#include "_autogen/raster.slang.h"                 // from nvpro_core2 (for rasterization)
#include "_autogen/ray_query_screenspace.slang.h"  // Local shader

// Common base class (see 02_basic)
#include "common/rt_base.hpp"

//---------------------------------------------------------------------------------------
// Ray Tracing Tutorial - 17 Ray Query Screen-Space Effects
//
// This class demonstrates practical ray query usage for screen-space effects.
// It shows how to integrate ray tracing with rasterization pipelines using
// compute shaders to avoid the performance issues of fragment shader ray queries.
//
// Key Features:
// - Hybrid rendering: rasterization for G-buffer + compute shader ray queries for effects
// - Screen-space ambient occlusion using ray queries
// - Performance optimization by processing only visible pixels
// - Real-time integration with existing rasterization pipelines
//
class Rt17RayQueryScreenSpace : public RtBase
{

public:
  // Additional G-buffer types for this tutorial
  enum
  {
    eImgRendered   = RtBase::eImgRendered,    // Final rendered color output
    eImgTonemapped = RtBase::eImgTonemapped,  // Tonemapped display output
    eImgHit,  // Hit data buffer for screen-space effects (world pos + compressed normal)
  };

  Rt17RayQueryScreenSpace()
  {
    m_useRayTracing = false;  // Use rasterization for G-buffer generation, not ray tracing
  }
  ~Rt17RayQueryScreenSpace() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  //---------------------------------------------------------------------------------------------------------------
  // Rendering all UI elements, including camera controls, sky parameters, and ray query settings.
  // - Called every frame to update the user interface
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;
    bool changed = false;
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("17 Ray Query Screen-Space Effects");


      // Optional: Display G-buffer debug view
      // ImGui::Image(ImTextureID(m_gBuffers.getDescriptorSet(eImgHit)), ImVec2(256 * m_gBuffers.getAspectRatio(), 256));


      if(PE::begin())
      {
        bool enableAO = m_pushValues.enableRayQueryAO != 0;
        PE::Checkbox("Enable Ray Query AO", &enableAO);
        m_pushValues.enableRayQueryAO = enableAO ? 1 : 0;
        PE::DragFloat("AO Radius", &m_pushValues.aoRadius, 0.1f, 0.1f, 5.0f);
        PE::DragInt("AO Samples", (int*)&m_pushValues.aoSamples, 1, 1, 128);
        PE::DragFloat("AO Intensity", &m_pushValues.aoIntensity, 0.1f, 0.0f, 5.0f);
        bool showAOOnly = m_pushValues.showAOOnly != 0;
        PE::Checkbox("Show AO Only", &showAOOnly);
        m_pushValues.showAOOnly = showAOOnly ? 1 : 0;
        PE::Checkbox("Enable Random", &m_enableRandom);
        PE::end();
      }
    }
    ImGui::End();
    changed |= RtBase::renderUI();
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Create primitive meshes (cubes) and arrange them in a grid pattern
  // - Create instances with varying heights for interesting AO effects
  // - Set up lighting and camera for the screen-space effects demonstration
  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Create primitive mesh
    nvutils::PrimitiveMesh cube = nvutils::createCube();
    nvsamples::primitiveMeshToResource(m_sceneResource, m_stagingUploader, cube);

    // Create a grid of 10x10 instances using the cube mesh, with different heights for each instance
    for(int i = 0; i < 10; i++)
    {
      for(int j = 0; j < 10; j++)
      {
        // Give each cube a different height using a sine/cosine function for interesting variation
        float height = 2.0f * std::sin(i * 0.5f) * std::cos(j * 0.5f);
        // Add slight offset to create more interesting geometry for AO effects
        glm::mat4 transform = glm::translate(glm::mat4(1), glm::vec3(i + (j % 2 ? 0.1 : 0), height, j + (i % 2 ? 0.1 : 0)));
        m_sceneResource.instances.push_back({.transform = transform, .materialIndex = 0, .meshIndex = 0});
      }
    }

    // One material for all instances - light green with moderate metallic/roughness
    m_sceneResource.materials = {
        {
            .baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),  // Light green color
            .metallicFactor  = 0.5f,                               // Semi-metallic
            .roughnessFactor = 0.5f,                               // Moderate roughness
        },
    };

    // Creating the resources
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);

    // Uploading the scene to GPU
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    m_stagingUploader.cmdUploadAppended(cmd);
    m_app->submitAndWaitTempCmdBuffer(cmd);  // Submit the command buffer to upload the resources

    // Set the camera
    m_cameraManip->setCamera({{-0.067451, 5.5812116, 10.620479}, {7.5854416, -5.069052, 1.9508885}, {0, 1, 0}, {60}, {0.01, 100}});

    // Set up lighting for the scene
    m_sceneResource.sceneInfo.useSky = false;  // Disable sky lighting for clearer AO visibility

    // Configure a point light for illumination
    m_sceneResource.sceneInfo.punctualLights[0].position  = {-1.0f, 4.0f, 4.0f};  // Light position
    m_sceneResource.sceneInfo.punctualLights[0].direction = {.7f, 1.0f, 1.0f};    // Light direction (normalized)
    m_sceneResource.sceneInfo.punctualLights[0].color     = {1.0f, 1.0f, 1.0f};   // White light
    m_sceneResource.sceneInfo.punctualLights[0].intensity = 50.0f;                // Light intensity
    m_sceneResource.sceneInfo.punctualLights[0].type      = shaderio::GltfLightType::ePoint;
    m_sceneResource.sceneInfo.backgroundColor             = {0.03f, 0.03f, 0.03f};  // Dark background
  }

  //--------------------------------------------------------------------------------------------------
  // Override G-buffer creation to add hit buffer for screen-space effects
  // Creates additional color attachment to store world position and compressed normals
  void createGBuffers(VkSampler linearSampler) override
  {
    // Create G-Buffers with extra hit buffer for screen-space effects
    nvvk::GBufferInitInfo gBufferInit{
        .allocator = &m_allocator,
        .colorFormats =
            {
                VK_FORMAT_R32G32B32A32_SFLOAT,  // Color buffer (RGBA32F for HDR colors)
                VK_FORMAT_R8G8B8A8_UNORM,       // Tonemapped output (LDR display)
                VK_FORMAT_R32G32B32A32_SFLOAT,  // Hit buffer (RGBA32F for world pos + compressed normals)
            },
        .depthFormat    = nvvk::findDepthFormat(m_app->getPhysicalDevice()),
        .imageSampler   = linearSampler,
        .descriptorPool = m_app->getTextureDescriptorPool(),
    };
    m_gBuffers.init(gBufferInit);
  }

  //--------------------------------------------------------------------------------------------------
  // Override graphics pipeline layout to support multiple color attachments
  // By default, the raster has only one attachment for color, but now we need two (color + hit data)
  void createGraphicsPipelineLayout() override
  {
    RtBase::createGraphicsPipelineLayout();

    // Set up color blend enable for 2 attachments (both disabled for opaque rendering)
    m_dynamicPipeline.colorBlendEnables = {VK_FALSE, VK_FALSE};
    m_dynamicPipeline.colorWriteMasks.push_back(m_dynamicPipeline.colorWriteMasks[0]);  // Same write mask as color
    m_dynamicPipeline.colorBlendEquations.push_back(m_dynamicPipeline.colorBlendEquations[0]);  // Same blend equation
  }

  //--------------------------------------------------------------------------------------------------
  // Create rasterization shaders for G-buffer generation
  // This creates vertex and fragment shaders that output both color and hit data
  void createRasterizationShaders() override
  {
    SCOPED_TIMER(__FUNCTION__);

    // Compile shader, and if compilation fails, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("raster.slang", raster_slang);

    // Destroy the previous shaders if they exist (for re-creation)
    vkDestroyShaderEXT(m_app->getDevice(), m_vertexShader, nullptr);
    vkDestroyShaderEXT(m_app->getDevice(), m_fragmentShader, nullptr);

    // Push constant range is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,  // Available in all graphics stages
        .offset     = 0,
        .size       = sizeof(shaderio::TutoPushConstant),
    };

    // Shader create information, this is used to create the shader modules
    VkShaderCreateInfoEXT shaderInfo{
        .sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT,  // SPIR-V bytecode
        .pName                  = "main",                         // Default entry point name
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
    shaderInfo.pName     = "fragmentMain";  // The entry point of the fragment shader
    shaderInfo.codeSize  = shaderCode.codeSize;
    shaderInfo.pCode     = shaderCode.pCode;
    vkCreateShadersEXT(m_app->getDevice(), 1U, &shaderInfo, nullptr, &m_fragmentShader);
    NVVK_DBG_NAME(m_fragmentShader);
  }


  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene with G-buffer generation
  // - Renders to two color attachments: color buffer and hit data buffer
  // - Hit data contains world position and compressed normals for screen-space effects
  void rasterScene(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Push constant information for the rasterization shaders
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address,  // Scene data buffer address
        .metallicRoughnessOverride = m_metallicRoughnessOverride,  // Material property overrides
    };
    const VkPushConstantsInfo pushInfo{
        .sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
        .layout     = m_graphicPipelineLayout,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset     = 0,
        .size       = sizeof(shaderio::TutoPushConstant),
        .pValues    = &pushValues,  // Other values are passed later
    };

    // Render the sky background if enabled
    if(m_sceneResource.sceneInfo.useSky)
    {
      const glm::mat4& viewMatrix = m_cameraManip->getViewMatrix();
      const glm::mat4& projMatrix = m_cameraManip->getPerspectiveMatrix();
      m_skySimple.runCompute(cmd, m_app->getViewportSize(), viewMatrix, projMatrix,
                             m_sceneResource.sceneInfo.skySimpleParam, m_gBuffers.getDescriptorImageInfo(eImgRendered));
    }

    // Set up multiple color attachments for G-buffer rendering
    std::array<VkRenderingAttachmentInfo, 2> colorAttachments{};

    // Color attachment (eImgRendered) - stores the final rendered color
    colorAttachments[0] = DEFAULT_VkRenderingAttachmentInfo;
    colorAttachments[0].loadOp = m_sceneResource.sceneInfo.useSky ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[0].imageView  = m_gBuffers.getColorImageView(eImgRendered);
    colorAttachments[0].clearValue = {.color = {m_sceneResource.sceneInfo.backgroundColor.x,
                                                m_sceneResource.sceneInfo.backgroundColor.y,
                                                m_sceneResource.sceneInfo.backgroundColor.z, 1.0f}};

    // Hit data attachment (eImgHit) - stores world position and compressed normals
    colorAttachments[1]            = DEFAULT_VkRenderingAttachmentInfo;
    colorAttachments[1].loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[1].imageView  = m_gBuffers.getColorImageView(eImgHit);
    colorAttachments[1].clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f}};  // Clear to zero (indicates invalid hit)

    VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
    depthAttachment.imageView                 = m_gBuffers.getDepthImageView();
    depthAttachment.clearValue                = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

    // Create the rendering info
    VkRenderingInfo renderingInfo      = DEFAULT_VkRenderingInfo;
    renderingInfo.renderArea           = DEFAULT_VkRect2D(m_gBuffers.getSize());
    renderingInfo.colorAttachmentCount = uint32_t(colorAttachments.size());
    renderingInfo.pColorAttachments    = colorAttachments.data();
    renderingInfo.pDepthAttachment     = &depthAttachment;

    // Transition G-buffer images to color attachment layout for rendering
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_GENERAL,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgHit), VK_IMAGE_LAYOUT_GENERAL,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getDepthImage(),
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                      {VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}});

    // Bind the descriptor sets for the graphics pipeline (making textures and buffers available to shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{.sType      = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
                                                          .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                                          .layout     = m_graphicPipelineLayout,
                                                          .firstSet   = 0,
                                                          .descriptorSetCount = 1,
                                                          .pDescriptorSets    = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);


    // ** BEGIN RENDERING **
    vkCmdBeginRendering(cmd, &renderingInfo);

    // Set all dynamic pipeline states
    m_dynamicPipeline.rasterizationState.cullMode = VK_CULL_MODE_NONE;  // Disable culling for double-sided rendering
    m_dynamicPipeline.cmdApplyAllStates(cmd);
    m_dynamicPipeline.cmdSetViewportAndScissor(cmd, m_app->getViewportSize());
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);  // Enable depth testing

    // Bind the same shader pair for all meshes
    m_dynamicPipeline.cmdBindShaders(cmd, {.vertex = m_vertexShader, .fragment = m_fragmentShader});


    // No vertex attributes are sent - vertex data is pulled directly in the shader
    VkVertexInputBindingDescription2EXT   bindingDescription   = {};
    VkVertexInputAttributeDescription2EXT attributeDescription = {};
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    // Render all mesh instances
    for(size_t i = 0; i < m_sceneResource.instances.size(); i++)
    {
      uint32_t                      meshIndex = m_sceneResource.instances[i].meshIndex;
      const shaderio::GltfMesh&     gltfMesh  = m_sceneResource.meshes[meshIndex];
      const shaderio::TriangleMesh& triMesh   = gltfMesh.triMesh;

      // Update push constants for this instance
      pushValues.normalMatrix  = glm::transpose(glm::inverse(glm::mat3(m_sceneResource.instances[i].transform)));
      pushValues.instanceIndex = int(i);  // Instance index for shader access
      vkCmdPushConstants2(cmd, &pushInfo);

      // Get the buffer using the pre-computed mesh-to-buffer mapping
      uint32_t            bufferIndex = m_sceneResource.meshToBufferIndex[meshIndex];
      const nvvk::Buffer& v           = m_sceneResource.bGltfDatas[bufferIndex];

      // Bind the index buffer for this mesh
      vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset, VkIndexType(gltfMesh.indexType));

      // Draw all triangles in this mesh
      vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);
    }

    // End the rendering pass
    vkCmdEndRendering(cmd);

    // Transition G-buffer images back to general layout for compute shader access
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgHit), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL});
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getDepthImage(),
                                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      {VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}});
  }

  //--------------------------------------------------------------------------------------------------
  // Create compute pipeline for screen-space ray query effects
  // This creates a compute pipeline that processes G-buffer data and applies
  // ray query effects like ambient occlusion
  // - Note: This is a compute pipeline using ray queries, not a ray tracing pipeline
  void createRayTracingPipeline() override
  {
    // Clean up existing pipeline for re-creation
    destroyRayTracingPipeline();

    // Compile the ray query compute shader, fall back to pre-compiled if compilation fails
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("ray_query_screenspace.slang", ray_query_screenspace_slang);

    // Push constant range for passing data to the compute shader
    const VkPushConstantRange pushConstant{VK_SHADER_STAGE_ALL, 0, sizeof(shaderio::TutoPushConstant)};

    // Descriptor set layouts: shared rasterization layout + ray tracing specific layout
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
  // Hybrid rendering: rasterization + ray query screen-space effects
  // This method first renders the scene with rasterization to create G-buffer,
  // then applies ray query effects like ambient occlusion using compute shaders
  void onRender(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Update the scene information buffer (must be done before dynamic rendering)
    updateSceneBuffer(cmd);

    // Step 1: Render scene with rasterization to create G-buffer
    rasterScene(cmd);

    // Step 2: Apply ray query screen-space effects using compute shaders
    applyScreenSpaceEffects(cmd);

    // Step 3: Apply post-processing (tonemapping, etc.)
    postProcess(cmd);
  }

  //---------------------------------------------------------------------------------------------------------------
  // Add the hit buffer to the ray tracing descriptor layout
  // This adds the G-buffer hit data as an input to the ray query compute shader
  void createRaytraceDescriptorLayout() override
  {
    // Add the hit buffer binding for reading G-buffer data
    m_rtBindings.addBinding({.binding         = shaderio::BindingPoints::eHitBuffer,
                             .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             .descriptorCount = 1,
                             .stageFlags      = VK_SHADER_STAGE_ALL});

    // Create the descriptor layout with the additional hit buffer binding
    RtBase::createRaytraceDescriptorLayout();
  }


  //---------------------------------------------------------------------------------------------------------------
  // Apply screen-space ray query effects using compute shaders
  // This method executes the compute shader that processes G-buffer data
  // and applies ray query effects like ambient occlusion
  void applyScreenSpaceEffects(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Bind the descriptor sets for the compute pipeline (making textures and buffers available to shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{.sType      = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
                                                          .stageFlags = VK_SHADER_STAGE_ALL,
                                                          .layout     = m_rtPipelineLayout,
                                                          .firstSet   = 0,
                                                          .descriptorSetCount = 1,
                                                          .pDescriptorSets    = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // Push descriptor sets for ray tracing resources
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_asBuilder.tlas);

    // Bind G-buffer data as input to the ray query compute shader
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage), m_gBuffers.getColorImageView(eImgRendered),
                 VK_IMAGE_LAYOUT_GENERAL);  // Color buffer (input/output)
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eHitBuffer), m_gBuffers.getColorImageView(eImgHit),
                 VK_IMAGE_LAYOUT_GENERAL);  // Hit data buffer (read-only storage image)

    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipelineLayout, 1, write.size(), write.data());

    // Update push constant data for the compute shader
    m_pushValues.sceneInfoAddress = (shaderio::GltfSceneInfo*)m_sceneResource.bSceneInfo.address;  // Scene data buffer address

    if(m_enableRandom)
      m_pushValues.frame++;  // Increment frame counter for random number generation

    const VkPushConstantsInfo pushInfo{.sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout     = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size       = sizeof(shaderio::TutoPushConstant),
                                       .pValues    = &m_pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Execute the compute shader with ray queries
    const VkExtent2D& size = m_app->getViewportSize();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipeline);
    // Dispatch compute shader with workgroups covering the entire screen
    vkCmdDispatch(cmd, (size.width + (WORKGROUP_SIZE - 1)) / WORKGROUP_SIZE,
                  (size.height + (WORKGROUP_SIZE - 1)) / WORKGROUP_SIZE, 1);

    // Ensure the rendered image is ready for the next stage (tonemapping)
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  // Clean up shader resources
  void sampleDestroy() override
  {
    VkDevice device = m_app->getDevice();
    vkDestroyShaderEXT(device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(device, m_fragmentShader, nullptr);
  }

private:
  bool m_enableRandom = false;
};


//---------------------------------------------------------------------------------------------------------------
// Main function - entry point of the application
// Sets up Vulkan context with ray query extensions and runs the tutorial
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  // Parse command line arguments
  nvutils::ParameterParser   cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Set up Vulkan context with required extensions and features
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};

  // Enable ray tracing and ray query features
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDeviceRayQueryFeaturesKHR rayqueryFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},                           // For dynamic descriptor updates
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},      // For shader objects
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // For building acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // For ray tracing pipelines
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
              {VK_KHR_RAY_QUERY_EXTENSION_NAME, &rayqueryFeature},               // For ray queries in compute shaders
          },
  };

  if(!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // Create Vulkan context with the specified extensions and features
  auto vkContext = RtBase::createVulkanContext(vkSetup);
  if(!vkContext)
  {
    return 1;  // Failed to create Vulkan context
  }

  // Set up the application with Vulkan context information
  appInfo.name           = "Ray Tracing Tutorial - 17 Ray Query Screen-Space Effects";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create and initialize the application
  nvapp::Application application;
  application.init(appInfo);

  // Create application elements
  auto tutorial    = std::make_shared<Rt17RayQueryScreenSpace>();           // Main tutorial element
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();              // Camera control element
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();  // Window title element
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();         // Menu element (File->Exit, etc.)
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Add all elements to the application
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();     // Start the application main loop
  application.deinit();  // Clean up application resources
  vkContext->deinit();   // Clean up Vulkan context

  return 0;
}
