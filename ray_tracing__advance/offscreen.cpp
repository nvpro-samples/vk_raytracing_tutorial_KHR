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


#include "offscreen.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"

extern std::vector<std::string> defaultSearchPaths;

//////////////////////////////////////////////////////////////////////////
// Post-processing
//////////////////////////////////////////////////////////////////////////

void Offscreen::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, nvvk::ResourceAllocator* allocator, uint32_t queueFamily)
{
  m_device             = device;
  m_alloc              = allocator;
  m_graphicsQueueIndex = queueFamily;
  m_debug.setup(m_device);
  m_depthFormat = nvvk::findDepthFormat(physicalDevice);
}

void Offscreen::destroy()
{
  vkDestroyPipeline(m_device, m_pipeline, nullptr);
  vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
  vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
  vkDestroyDescriptorSetLayout(m_device, m_dsetLayout, nullptr);
  vkDestroyRenderPass(m_device, m_renderPass, nullptr);
  vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
  m_alloc->destroy(m_colorTexture);
  m_alloc->destroy(m_depthTexture);
}

//--------------------------------------------------------------------------------------------------
// Creating an offscreen frame buffer and the associated render pass
//
void Offscreen::createFramebuffer(const VkExtent2D& size)
{
  m_alloc->destroy(m_colorTexture);
  m_alloc->destroy(m_depthTexture);

  // Creating the color image
  {
    auto colorCreateInfo = nvvk::makeImage2DCreateInfo(
        size, m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

    nvvk::Image           image  = m_alloc->createImage(colorCreateInfo);
    VkImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    m_colorTexture                        = m_alloc->createTexture(image, ivInfo, sampler);
    m_colorTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }


  // Creating the depth buffer
  {
    auto depthCreateInfo = nvvk::makeImage2DCreateInfo(size, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    nvvk::Image image    = m_alloc->createImage(depthCreateInfo);

    VkImageViewCreateInfo depthStencilView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthStencilView.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    depthStencilView.format           = m_depthFormat;
    depthStencilView.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    depthStencilView.image            = image.image;

    m_depthTexture = m_alloc->createTexture(image, depthStencilView);
  }

  // Setting the image layout for both color and depth
  {
    nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
    auto              cmdBuf = genCmdBuf.createCommandBuffer();
    nvvk::cmdBarrierImageLayout(cmdBuf, m_colorTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    nvvk::cmdBarrierImageLayout(cmdBuf, m_depthTexture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

    genCmdBuf.submitAndWait(cmdBuf);
  }

  // Creating a renderpass for the offscreen
  if(!m_renderPass)
  {
    m_renderPass = nvvk::createRenderPass(m_device, {m_colorFormat}, m_depthFormat, 1, true, true,
                                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
  }

  // Creating the frame buffer for offscreen
  std::vector<VkImageView> attachments = {m_colorTexture.descriptor.imageView, m_depthTexture.descriptor.imageView};

  vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
  VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  info.renderPass      = m_renderPass;
  info.attachmentCount = 2;
  info.pAttachments    = attachments.data();
  info.width           = size.width;
  info.height          = size.height;
  info.layers          = 1;
  vkCreateFramebuffer(m_device, &info, nullptr, &m_framebuffer);
}

//--------------------------------------------------------------------------------------------------
// The pipeline is how things are rendered, which shaders, type of primitives, depth test and more
// The incoming render pass, is in which rendering pass it will be displayed (framebuffer)
//
void Offscreen::createPipeline(VkRenderPass& renderPass)
{
  // Push constants in the fragment shader
  VkPushConstantRange pushConstantRanges = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};

  // Creating the pipeline layout
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutCreateInfo.setLayoutCount         = 1;
  pipelineLayoutCreateInfo.pSetLayouts            = &m_dsetLayout;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
  pipelineLayoutCreateInfo.pPushConstantRanges    = &pushConstantRanges;
  vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout);

  // Pipeline: completely generic, no vertices
  nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_pipelineLayout, renderPass);
  pipelineGenerator.addShader(nvh::loadFile("spv/passthrough.vert.spv", true, defaultSearchPaths, true), VK_SHADER_STAGE_VERTEX_BIT);
  pipelineGenerator.addShader(nvh::loadFile("spv/post.frag.spv", true, defaultSearchPaths, true), VK_SHADER_STAGE_FRAGMENT_BIT);
  pipelineGenerator.rasterizationState.cullMode = VK_CULL_MODE_NONE;
  m_pipeline                                    = pipelineGenerator.createPipeline();
  m_debug.setObjectName(m_pipeline, "post");
}

//--------------------------------------------------------------------------------------------------
// The descriptor layout is the description of the data that is passed to the vertex or the
// fragment program.
//
void Offscreen::createDescriptor()
{
  m_dsetLayoutBinding.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_dsetLayout = m_dsetLayoutBinding.createLayout(m_device);
  m_descPool   = m_dsetLayoutBinding.createPool(m_device);
  m_dset       = nvvk::allocateDescriptorSet(m_device, m_descPool, m_dsetLayout);
}

//--------------------------------------------------------------------------------------------------
// Update the output
//
void Offscreen::updateDescriptorSet()
{
  VkWriteDescriptorSet writeDescriptorSets = m_dsetLayoutBinding.makeWrite(m_dset, 0, &m_colorTexture.descriptor);
  vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSets, 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Draw a full screen quad with the attached image
//
void Offscreen::draw(VkCommandBuffer cmdBuf, const VkExtent2D& size)
{
  m_debug.beginLabel(cmdBuf, "Post");

  VkViewport viewport{0, 0, (float)size.width, (float)size.height, 0, 1};
  vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
  VkRect2D scissor{{0, 0}, {size.width, size.height}};
  vkCmdSetScissor(cmdBuf, 0, 1, &scissor);


  auto aspectRatio = static_cast<float>(size.width) / static_cast<float>(size.height);
  vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &aspectRatio);
  vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_dset, 0, nullptr);
  vkCmdDraw(cmdBuf, 3, 1, 0, 0);

  m_debug.endLabel(cmdBuf);
}
