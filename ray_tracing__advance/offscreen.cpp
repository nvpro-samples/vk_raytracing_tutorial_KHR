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

void Offscreen::setup(const vk::Device&         device,
                      const vk::PhysicalDevice& physicalDevice,
                      nvvk::ResourceAllocator*  allocator,
                      uint32_t                  queueFamily)
{
  m_device             = device;
  m_alloc              = allocator;
  m_graphicsQueueIndex = queueFamily;
  m_debug.setup(m_device);
  m_depthFormat = nvvk::findDepthFormat(physicalDevice);
}

void Offscreen::destroy()
{
  m_device.destroy(m_pipeline);
  m_device.destroy(m_pipelineLayout);
  m_device.destroy(m_descPool);
  m_device.destroy(m_dsetLayout);
  m_alloc->destroy(m_colorTexture);
  m_alloc->destroy(m_depthTexture);
  m_device.destroy(m_renderPass);
  m_device.destroy(m_framebuffer);
}

//--------------------------------------------------------------------------------------------------
// Creating an offscreen frame buffer and the associated render pass
//
void Offscreen::createFramebuffer(VkExtent2D& size)
{
  m_alloc->destroy(m_colorTexture);
  m_alloc->destroy(m_depthTexture);

  // Creating the color image
  {
    auto colorCreateInfo = nvvk::makeImage2DCreateInfo(size, m_colorFormat,
                                                       vk::ImageUsageFlagBits::eColorAttachment
                                                           | vk::ImageUsageFlagBits::eSampled
                                                           | vk::ImageUsageFlagBits::eStorage);

    nvvk::Image             image  = m_alloc->createImage(colorCreateInfo);
    vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
    m_colorTexture                 = m_alloc->createTexture(image, ivInfo, vk::SamplerCreateInfo());
    m_colorTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }


  // Creating the depth buffer
  {
    auto depthCreateInfo =
        nvvk::makeImage2DCreateInfo(size, m_depthFormat,
                                    vk::ImageUsageFlagBits::eDepthStencilAttachment);
    nvvk::Image image = m_alloc->createImage(depthCreateInfo);

    vk::ImageViewCreateInfo depthStencilView;
    depthStencilView.setViewType(vk::ImageViewType::e2D);
    depthStencilView.setFormat(m_depthFormat);
    depthStencilView.setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    depthStencilView.setImage(image.image);

    m_depthTexture = m_alloc->createTexture(image, depthStencilView);
  }

  // Setting the image layout for both color and depth
  {
    nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
    auto              cmdBuf = genCmdBuf.createCommandBuffer();
    nvvk::cmdBarrierImageLayout(cmdBuf, m_colorTexture.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eGeneral);
    nvvk::cmdBarrierImageLayout(cmdBuf, m_depthTexture.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                vk::ImageAspectFlagBits::eDepth);

    genCmdBuf.submitAndWait(cmdBuf);
  }

  // Creating a renderpass for the offscreen
  if(!m_renderPass)
  {
    m_renderPass = nvvk::createRenderPass(m_device, {m_colorFormat}, m_depthFormat, 1, true, true,
                                          vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
  }

  // Creating the frame buffer for offscreen
  std::vector<vk::ImageView> attachments = {m_colorTexture.descriptor.imageView,
                                            m_depthTexture.descriptor.imageView};

  m_device.destroy(m_framebuffer);
  vk::FramebufferCreateInfo info;
  info.setRenderPass(m_renderPass);
  info.setAttachmentCount(2);
  info.setPAttachments(attachments.data());
  info.setWidth(size.width);
  info.setHeight(size.height);
  info.setLayers(1);
  m_framebuffer = m_device.createFramebuffer(info);
}

//--------------------------------------------------------------------------------------------------
// The pipeline is how things are rendered, which shaders, type of primitives, depth test and more
// The incoming render pass, is in which rendering pass it will be displayed (framebuffer)
//
void Offscreen::createPipeline(vk::RenderPass& renderPass)
{
  // Push constants in the fragment shader
  vk::PushConstantRange pushConstantRanges = {vk::ShaderStageFlagBits::eFragment, 0, sizeof(float)};

  // Creating the pipeline layout
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
  pipelineLayoutCreateInfo.setSetLayoutCount(1);
  pipelineLayoutCreateInfo.setPSetLayouts(&m_dsetLayout);
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
  m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Pipeline: completely generic, no vertices
  nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_pipelineLayout, renderPass);
  pipelineGenerator.addShader(nvh::loadFile("spv/passthrough.vert.spv", true, defaultSearchPaths,
                                            true),
                              vk::ShaderStageFlagBits::eVertex);
  pipelineGenerator.addShader(nvh::loadFile("spv/post.frag.spv", true, defaultSearchPaths, true),
                              vk::ShaderStageFlagBits::eFragment);
  pipelineGenerator.rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
  m_pipeline = pipelineGenerator.createPipeline();
  m_debug.setObjectName(m_pipeline, "post");
}

//--------------------------------------------------------------------------------------------------
// The descriptor layout is the description of the data that is passed to the vertex or the
// fragment program.
//
void Offscreen::createDescriptor()
{
  using vkDS = vk::DescriptorSetLayoutBinding;
  using vkDT = vk::DescriptorType;
  using vkSS = vk::ShaderStageFlagBits;

  m_dsetLayoutBinding.addBinding(vkDS(0, vkDT::eCombinedImageSampler, 1, vkSS::eFragment));
  m_dsetLayout = m_dsetLayoutBinding.createLayout(m_device);
  m_descPool   = m_dsetLayoutBinding.createPool(m_device);
  m_dset       = nvvk::allocateDescriptorSet(m_device, m_descPool, m_dsetLayout);
}

//--------------------------------------------------------------------------------------------------
// Update the output
//
void Offscreen::updateDescriptorSet()
{
  vk::WriteDescriptorSet writeDescriptorSets =
      m_dsetLayoutBinding.makeWrite(m_dset, 0, &m_colorTexture.descriptor);
  m_device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Draw a full screen quad with the attached image
//
void Offscreen::draw(vk::CommandBuffer cmdBuf, VkExtent2D& size)
{
  m_debug.beginLabel(cmdBuf, "Post");

  cmdBuf.setViewport(0, {vk::Viewport(0, 0, (float)size.width, (float)size.height, 0, 1)});
  cmdBuf.setScissor(0, {{{0, 0}, {size.width, size.height}}});

  auto aspectRatio = static_cast<float>(size.width) / static_cast<float>(size.height);
  cmdBuf.pushConstants<float>(m_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, aspectRatio);
  cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
  cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, m_dset, {});
  cmdBuf.draw(3, 1, 0, 0);

  m_debug.endLabel(cmdBuf);
}
