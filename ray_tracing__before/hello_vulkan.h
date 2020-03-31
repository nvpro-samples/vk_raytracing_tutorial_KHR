/* Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include "nvvkpp/allocator_dedicated_vkpp.hpp"
#include "nvvkpp/appbase_vkpp.hpp"
#include "nvvkpp/debug_util_vkpp.hpp"


using nvvkBuffer  = nvvkpp::BufferDedicated;
using nvvkTexture = nvvkpp::TextureDedicated;

//--------------------------------------------------------------------------------------------------
// Simple rasterizer of OBJ objects
// - Each OBJ loaded are stored in an `ObjModel` and referenced by a `ObjInstance`
// - It is possible to have many `ObjInstance` referencing the same `ObjModel`
// - Rendering is done in an offscreen framebuffer
// - The image of the framebuffer is displayed in post-process in a full-screen quad
//
class HelloVulkan : public nvvkpp::AppBase
{
public:
  void setup(const vk::Device& device, const vk::PhysicalDevice& physicalDevice, uint32_t queueFamily) override;
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  void loadModel(const std::string& filename, nvmath::mat4f transform = nvmath::mat4f(1));
  void updateDescriptorSet();
  void createUniformBuffer();
  void createSceneDescriptionBuffer();
  void createTextureImages(const vk::CommandBuffer& cmdBuf, const std::vector<std::string>& textures);
  void updateUniformBuffer();
  void onResize(int /*w*/, int /*h*/) override;
  void destroyResources();
  void rasterize(const vk::CommandBuffer& cmdBuff);

  // The OBJ model
  struct ObjModel
  {
    uint32_t   nbIndices{0};
    uint32_t   nbVertices{0};
    nvvkBuffer vertexBuffer;    // Device buffer of all 'Vertex'
    nvvkBuffer indexBuffer;     // Device buffer of the indices forming triangles
    nvvkBuffer matColorBuffer;  // Device buffer of array of 'Wavefront material'
    nvvkBuffer matIndexBuffer;  // Device buffer of array of 'Wavefront material'
  };

  // Instance of the OBJ
  struct ObjInstance
  {
    uint32_t      objIndex{0};     // Reference to the `m_objModel`
    uint32_t      txtOffset{0};    // Offset in `m_textures`
    nvmath::mat4f transform{1};    // Position of the instance
    nvmath::mat4f transformIT{1};  // Inverse transpose
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
  vk::PipelineLayout                          m_pipelineLayout;
  vk::Pipeline                                m_graphicsPipeline;
  std::vector<vk::DescriptorSetLayoutBinding> m_descSetLayoutBind;
  vk::DescriptorPool                          m_descPool;
  vk::DescriptorSetLayout                     m_descSetLayout;
  vk::DescriptorSet                           m_descSet;

  nvvkBuffer               m_cameraMat;  // Device-Host of the camera matrices
  nvvkBuffer               m_sceneDesc;  // Device buffer of the OBJ instances
  std::vector<nvvkTexture> m_textures;   // vector of all textures of the scene


  nvvkpp::AllocatorDedicated m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvkpp::DebugUtil          m_debug;  // Utility to name objects


  // #Post
  void createOffscreenRender();
  void createPostPipeline();
  void createPostDescriptor();
  void updatePostDescriptorSet();
  void drawPost(vk::CommandBuffer cmdBuf);

  std::vector<vk::DescriptorSetLayoutBinding> m_postDescSetLayoutBind;
  vk::DescriptorPool                          m_postDescPool;
  vk::DescriptorSetLayout                     m_postDescSetLayout;
  vk::DescriptorSet                           m_postDescSet;
  vk::Pipeline                                m_postPipeline;
  vk::PipelineLayout                          m_postPipelineLayout;
  vk::RenderPass                              m_offscreenRenderPass;
  vk::Framebuffer                             m_offscreenFramebuffer;
  nvvkTexture                                 m_offscreenColor;
  vk::Format                                  m_offscreenColorFormat{vk::Format::eR32G32B32A32Sfloat};
  nvvkTexture                                 m_offscreenDepth;
  vk::Format                                  m_offscreenDepthFormat{vk::Format::eD32Sfloat};
};
