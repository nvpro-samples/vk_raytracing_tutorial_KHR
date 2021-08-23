/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "nvvk/appbase_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"

// #VKRay
#include "nvvk/raytraceKHR_vk.hpp"

//--------------------------------------------------------------------------------------------------
// Simple rasterizer of OBJ objects
// - Each OBJ loaded are stored in an `ObjModel` and referenced by a `ObjInstance`
// - It is possible to have many `ObjInstance` referencing the same `ObjModel`
// - Rendering is done in an offscreen framebuffer
// - The image of the framebuffer is displayed in post-process in a full-screen quad
//
class HelloVulkan : public nvvk::AppBaseVk
{
public:
  void setup(const VkInstance& instance, const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t queueFamily) override;
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  void loadModel(const std::string& filename, nvmath::mat4f transform = nvmath::mat4f(1));
  void updateDescriptorSet();
  void createUniformBuffer();
  void createSceneDescriptionBuffer();
  void createTextureImages(const VkCommandBuffer& cmdBuf, const std::vector<std::string>& textures);
  void updateUniformBuffer(const VkCommandBuffer& cmdBuf);
  void onResize(int /*w*/, int /*h*/) override;
  void destroyResources();
  void rasterize(const VkCommandBuffer& cmdBuff);

  // The OBJ model
  struct ObjModel
  {
    uint32_t     nbIndices{0};
    uint32_t     nbVertices{0};
    nvvk::Buffer vertexBuffer;    // Device buffer of all 'Vertex'
    nvvk::Buffer indexBuffer;     // Device buffer of the indices forming triangles
    nvvk::Buffer matColorBuffer;  // Device buffer of array of 'Wavefront material'
    nvvk::Buffer matIndexBuffer;  // Device buffer of array of 'Wavefront material'
  };

  // Instance of the OBJ
  struct ObjInstance
  {
    nvmath::mat4f   transform{1};    // Position of the instance
    nvmath::mat4f   transformIT{1};  // Inverse transpose
    uint32_t        objIndex{0};     // Reference to the `m_objModel`
    uint32_t        txtOffset{0};    // Offset in `m_textures`
    VkDeviceAddress vertices{0};
    VkDeviceAddress indices{0};
    VkDeviceAddress materials{0};
    VkDeviceAddress materialIndices{0};
  };

  // Information pushed at each draw call
  struct ObjPushConstant
  {
    nvmath::vec3f lightPosition{10.f, 15.f, 8.f};
    int           instanceId{0};  // To retrieve the transformation matrix
    float         lightIntensity{100.f};
    int           lightType{0};  // 0: point, 1: infinite
  };
  ObjPushConstant m_pushConstant;

  // Array of objects and instances in the scene
  std::vector<ObjModel>    m_objModel;
  std::vector<ObjInstance> m_objInstance;

  // Graphic pipeline
  VkPipelineLayout            m_pipelineLayout;
  VkPipeline                  m_graphicsPipeline;
  nvvk::DescriptorSetBindings m_descSetLayoutBind;
  VkDescriptorPool            m_descPool;
  VkDescriptorSetLayout       m_descSetLayout;
  VkDescriptorSet             m_descSet;

  nvvk::Buffer m_cameraMat;  // Device-Host of the camera matrices
  nvvk::Buffer m_sceneDesc;  // Device buffer of the OBJ instances

  std::vector<nvvk::Texture> m_textures;  // vector of all textures of the scene


  nvvk::ResourceAllocatorDma m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvk::DebugUtil            m_debug;  // Utility to name objects


  // #Post
  void createOffscreenRender();
  void createPostPipeline();
  void createPostDescriptor();
  void updatePostDescriptorSet();
  void drawPost(VkCommandBuffer cmdBuf);

  nvvk::DescriptorSetBindings m_postDescSetLayoutBind;
  VkDescriptorPool            m_postDescPool{VK_NULL_HANDLE};
  VkDescriptorSetLayout       m_postDescSetLayout{VK_NULL_HANDLE};
  VkDescriptorSet             m_postDescSet{VK_NULL_HANDLE};
  VkPipeline                  m_postPipeline{VK_NULL_HANDLE};
  VkPipelineLayout            m_postPipelineLayout{VK_NULL_HANDLE};
  VkRenderPass                m_offscreenRenderPass{VK_NULL_HANDLE};
  VkFramebuffer               m_offscreenFramebuffer{VK_NULL_HANDLE};
  nvvk::Texture               m_offscreenColor;
  nvvk::Texture               m_offscreenDepth;
  VkFormat                    m_offscreenColorFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
  VkFormat                    m_offscreenDepthFormat{VK_FORMAT_X8_D24_UNORM_PACK32};

  // #VKRay
  void initRayTracing();
  auto objectToVkGeometryKHR(const ObjModel& model);
  void createBottomLevelAS();
  void createTopLevelAS();
  void createRtDescriptorSet();
  void updateRtDescriptorSet();
  void createRtPipeline();
  void createRtShaderBindingTable();
  void raytrace(const VkCommandBuffer& cmdBuf, const nvmath::vec4f& clearColor);
  void resetFrame();
  void updateFrame();

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
  nvvk::RaytracingBuilderKHR                      m_rtBuilder;
  nvvk::DescriptorSetBindings                     m_rtDescSetLayoutBind;
  VkDescriptorPool                                m_rtDescPool;
  VkDescriptorSetLayout                           m_rtDescSetLayout;
  VkDescriptorSet                                 m_rtDescSet;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  VkPipelineLayout                                  m_rtPipelineLayout;
  VkPipeline                                        m_rtPipeline;
  nvvk::Buffer                                      m_rtSBTBuffer;
  int                                               m_maxFrames{10000};

  struct RtPushConstant
  {
    nvmath::vec4f clearColor;
    nvmath::vec3f lightPosition;
    float         lightIntensity{100.0f};
    int           lightType{0};
    int           frame{0};
  } m_rtPushConstants;
};
