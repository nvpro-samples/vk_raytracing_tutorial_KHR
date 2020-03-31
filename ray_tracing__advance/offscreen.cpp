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


#include "offscreen.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvkpp/commands_vkpp.hpp"
#include "nvvkpp/descriptorsets_vkpp.hpp"
#include "nvvkpp/pipeline_vkpp.hpp"
#include "nvvkpp/renderpass_vkpp.hpp"

extern std::vector<std::string> defaultSearchPaths;

//////////////////////////////////////////////////////////////////////////
// Post-processing
//////////////////////////////////////////////////////////////////////////

void Offscreen::setup(const vk::Device& device, nvvkMemAllocator& memAlloc, uint32_t queueFamily)
{
  m_device = device;
  m_alloc.init(device, &memAlloc);
  m_graphicsQueueIndex = queueFamily;
  m_debug.setup(m_device);
}

void Offscreen::destroy()
{
  m_device.destroy(m_pipeline);
  m_device.destroy(m_pipelineLayout);
  m_device.destroy(m_descPool);
  m_device.destroy(m_dsetLayout);
  m_alloc.destroy(m_colorTexture);
  m_alloc.destroy(m_depthTexture);
  m_device.destroy(m_renderPass);
  m_device.destroy(m_framebuffer);
}

//--------------------------------------------------------------------------------------------------
// Creating an offscreen frame buffer and the associated render pass
//
void Offscreen::createFramebuffer(VkExtent2D& size)
{
  m_alloc.destroy(m_colorTexture);
  m_alloc.destroy(m_depthTexture);

  // Creating the color image
  auto colorCreateInfo = nvvkpp::image::create2DInfo(size, m_colorFormat,
                                                     vk::ImageUsageFlagBits::eColorAttachment
                                                         | vk::ImageUsageFlagBits::eSampled
                                                         | vk::ImageUsageFlagBits::eStorage);
  m_colorTexture       = m_alloc.createImage(colorCreateInfo);

  m_colorTexture.descriptor =
      nvvkpp::image::create2DDescriptor(m_device, m_colorTexture.image, vk::SamplerCreateInfo{},
                                        m_colorFormat, vk::ImageLayout::eGeneral);

  // Creating the depth buffer
  auto depthCreateInfo =
      nvvkpp::image::create2DInfo(size, m_depthFormat,
                                  vk::ImageUsageFlagBits::eDepthStencilAttachment);
  m_depthTexture = m_alloc.createImage(depthCreateInfo);

  vk::ImageViewCreateInfo depthStencilView;
  depthStencilView.setViewType(vk::ImageViewType::e2D);
  depthStencilView.setFormat(m_depthFormat);
  depthStencilView.setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
  depthStencilView.setImage(m_depthTexture.image);
  m_depthTexture.descriptor.imageView = m_device.createImageView(depthStencilView);

  // Setting the image layout for both color and depth
  {
    nvvkpp::SingleCommandBuffer genCmdBuf(m_device, m_graphicsQueueIndex);
    auto                        cmdBuf = genCmdBuf.createCommandBuffer();
    nvvkpp::image::setImageLayout(cmdBuf, m_colorTexture.image, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eGeneral);
    nvvkpp::image::setImageLayout(cmdBuf, m_depthTexture.image, vk::ImageAspectFlagBits::eDepth,
                                  vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eDepthStencilAttachmentOptimal);

    genCmdBuf.flushCommandBuffer(cmdBuf);
  }

  // Creating a renderpass for the offscreen
  if(!m_renderPass)
  {
    m_renderPass =
        nvvkpp::util::createRenderPass(m_device, {m_colorFormat}, m_depthFormat, 1, true, true,
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
  std::vector<std::string> paths = defaultSearchPaths;

  nvvkpp::GraphicsPipelineGenerator pipelineGenerator(m_device, m_pipelineLayout, renderPass);
  pipelineGenerator.addShader(nvh::loadFile("shaders/passthrough.vert.spv", true, paths),
                              vk::ShaderStageFlagBits::eVertex);
  pipelineGenerator.addShader(nvh::loadFile("shaders/post.frag.spv", true, paths),
                              vk::ShaderStageFlagBits::eFragment);
  pipelineGenerator.rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
  m_pipeline = pipelineGenerator.create();
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

  m_dsetLayoutBinding.emplace_back(vkDS(0, vkDT::eCombinedImageSampler, 1, vkSS::eFragment));
  m_dsetLayout = nvvkpp::util::createDescriptorSetLayout(m_device, m_dsetLayoutBinding);
  m_descPool   = nvvkpp::util::createDescriptorPool(m_device, m_dsetLayoutBinding);
  m_dset       = nvvkpp::util::createDescriptorSet(m_device, m_descPool, m_dsetLayout);
}

//--------------------------------------------------------------------------------------------------
// Update the output
//
void Offscreen::updateDescriptorSet()
{
  vk::WriteDescriptorSet writeDescriptorSets =
      nvvkpp::util::createWrite(m_dset, m_dsetLayoutBinding[0], &m_colorTexture.descriptor);
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
