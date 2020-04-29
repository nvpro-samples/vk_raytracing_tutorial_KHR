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

#include <vulkan/vulkan.hpp>

#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "vkalloc.hpp"

//--------------------------------------------------------------------------------------------------
// Class to render in off-screen framebuffers. Instead of rendering directly to the
// screen back buffer, this class create the output frame buffer 'createFramebuffer',
// and use the pipeline from 'createPipeline' to render a quad 'draw' with the colorTexture
// image

class Offscreen
{
public:
  void setup(const vk::Device& device, nvvk::Allocator* allocator, uint32_t queueFamily);
  void destroy();

  void createFramebuffer(VkExtent2D& size);
  void createPipeline(vk::RenderPass& renderPass);
  void createDescriptor();
  void updateDescriptorSet();
  void draw(vk::CommandBuffer cmdBuf, VkExtent2D& size);

  const vk::RenderPass&  renderPass() { return m_renderPass; }
  const vk::Framebuffer& frameBuffer() { return m_framebuffer; }
  const nvvk::Texture&   colorTexture() { return m_colorTexture; }

private:
  nvvk::DescriptorSetBindings m_dsetLayoutBinding;
  vk::DescriptorPool          m_descPool;
  vk::DescriptorSetLayout     m_dsetLayout;
  vk::DescriptorSet           m_dset;
  vk::Pipeline                m_pipeline;
  vk::PipelineLayout          m_pipelineLayout;
  vk::RenderPass              m_renderPass;
  vk::Framebuffer             m_framebuffer;

  nvvk::Texture m_colorTexture;
  vk::Format    m_colorFormat{vk::Format::eR32G32B32A32Sfloat};
  nvvk::Texture m_depthTexture;
  vk::Format    m_depthFormat{vk::Format::eD32Sfloat};

  nvvk::Allocator* m_alloc{nullptr};  // Allocator for buffer, images, acceleration structures
  vk::Device       m_device;
  int              m_graphicsQueueIndex{0};
  nvvk::DebugUtil  m_debug;  // Utility to name objects
};
