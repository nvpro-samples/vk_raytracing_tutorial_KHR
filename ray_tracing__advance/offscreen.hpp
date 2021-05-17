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


#include <vulkan/vulkan.hpp>

#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"

//--------------------------------------------------------------------------------------------------
// Class to render in off-screen framebuffers. Instead of rendering directly to the
// screen back buffer, this class create the output frame buffer 'createFramebuffer',
// and use the pipeline from 'createPipeline' to render a quad 'draw' with the colorTexture
// image

class Offscreen
{
public:
  void setup(const vk::Device&         device,
             const vk::PhysicalDevice& physicalDevice,
             nvvk::ResourceAllocator*  allocator,
             uint32_t                  queueFamily);
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
  vk::Format    m_depthFormat;

  nvvk::ResourceAllocator* m_alloc{
      nullptr};  // Allocator for buffer, images, acceleration structures
  vk::Device      m_device;
  int             m_graphicsQueueIndex{0};
  nvvk::DebugUtil m_debug;  // Utility to name objects
};
