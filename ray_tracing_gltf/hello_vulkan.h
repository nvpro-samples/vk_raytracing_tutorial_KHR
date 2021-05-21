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
#include <vulkan/vulkan.hpp>


#include "nvvk/appbase_vkpp.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"

// #VKRay
#include "nvh/gltfscene.hpp"
#include "nvvk/raytraceKHR_vk.hpp"
#include "nvvk/sbtwrapper_vk.hpp"

//--------------------------------------------------------------------------------------------------
// Simple rasterizer of OBJ objects
// - Each OBJ loaded are stored in an `ObjModel` and referenced by a `ObjInstance`
// - It is possible to have many `ObjInstance` referencing the same `ObjModel`
// - Rendering is done in an offscreen framebuffer
// - The image of the framebuffer is displayed in post-process in a full-screen quad
//
class HelloVulkan : public nvvk::AppBase
{
public:
  void setup(const vk::Instance&       instance,
             const vk::Device&         device,
             const vk::PhysicalDevice& physicalDevice,
             uint32_t                  queueFamily) override;
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  void loadScene(const std::string& filename);
  void updateDescriptorSet();
  void createUniformBuffer();
  void createTextureImages(const vk::CommandBuffer& cmdBuf, tinygltf::Model& gltfModel);
  void updateUniformBuffer(const vk::CommandBuffer& cmdBuf);
  void onResize(int /*w*/, int /*h*/) override;
  void destroyResources();
  void rasterize(const vk::CommandBuffer& cmdBuff);

  // Structure used for retrieving the primitive information in the closest hit
  // The gl_InstanceCustomIndexNV
  struct RtPrimitiveLookup
  {
    uint32_t indexOffset;
    uint32_t vertexOffset;
    int      materialIndex;
  };


  nvh::GltfScene m_gltfScene;
  nvvk::Buffer   m_vertexBuffer;
  nvvk::Buffer   m_normalBuffer;
  nvvk::Buffer   m_uvBuffer;
  nvvk::Buffer   m_indexBuffer;
  nvvk::Buffer   m_materialBuffer;
  nvvk::Buffer   m_matrixBuffer;
  nvvk::Buffer   m_rtPrimLookup;

  // Information pushed at each draw call
  struct ObjPushConstant
  {
    nvmath::vec3f lightPosition{0.f, 4.5f, 0.f};
    int           instanceId{0};  // To retrieve the transformation matrix
    float         lightIntensity{10.f};
    int           lightType{0};  // 0: point, 1: infinite
    int           materialId{0};
  };
  ObjPushConstant m_pushConstant;

  // Graphic pipeline
  vk::PipelineLayout          m_pipelineLayout;
  vk::Pipeline                m_graphicsPipeline;
  nvvk::DescriptorSetBindings m_descSetLayoutBind;
  vk::DescriptorPool          m_descPool;
  vk::DescriptorSetLayout     m_descSetLayout;
  vk::DescriptorSet           m_descSet;

  nvvk::Buffer               m_cameraMat;  // Device-Host of the camera matrices
  std::vector<nvvk::Texture> m_textures;   // vector of all textures of the scene

  nvvk::ResourceAllocatorDma m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvk::DebugUtil            m_debug;  // Utility to name objects

  // #Post
  void createOffscreenRender();
  void createPostPipeline();
  void createPostDescriptor();
  void updatePostDescriptorSet();
  void drawPost(vk::CommandBuffer cmdBuf);

  nvvk::DescriptorSetBindings m_postDescSetLayoutBind;
  vk::DescriptorPool          m_postDescPool;
  vk::DescriptorSetLayout     m_postDescSetLayout;
  vk::DescriptorSet           m_postDescSet;
  vk::Pipeline                m_postPipeline;
  vk::PipelineLayout          m_postPipelineLayout;
  vk::RenderPass              m_offscreenRenderPass;
  vk::Framebuffer             m_offscreenFramebuffer;
  nvvk::Texture               m_offscreenColor;
  vk::Format                  m_offscreenColorFormat{vk::Format::eR32G32B32A32Sfloat};
  nvvk::Texture               m_offscreenDepth;
  vk::Format                  m_offscreenDepthFormat{vk::Format::eX8D24UnormPack32};

  // #VKRay
  auto primitiveToGeometry(const nvh::GltfPrimMesh& prim);
  void initRayTracing();
  void createBottomLevelAS();
  void createTopLevelAS();
  void createRtDescriptorSet();
  void updateRtDescriptorSet();
  void createRtPipeline();
  void raytrace(const vk::CommandBuffer& cmdBuf, const nvmath::vec4f& clearColor);
  void updateFrame();
  void resetFrame();

  vk::PhysicalDeviceRayTracingPipelinePropertiesKHR   m_rtProperties;
  nvvk::RaytracingBuilderKHR                          m_rtBuilder;
  nvvk::DescriptorSetBindings                         m_rtDescSetLayoutBind;
  vk::DescriptorPool                                  m_rtDescPool;
  vk::DescriptorSetLayout                             m_rtDescSetLayout;
  vk::DescriptorSet                                   m_rtDescSet;
  std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  vk::PipelineLayout                                  m_rtPipelineLayout;
  vk::Pipeline                                        m_rtPipeline;
  nvvk::SBTWrapper                                    m_sbtWrapper;

  struct RtPushConstant
  {
    nvmath::vec4f clearColor;
    nvmath::vec3f lightPosition;
    float         lightIntensity;
    int           lightType;
    int           frame{0};
  } m_rtPushConstants;
};
