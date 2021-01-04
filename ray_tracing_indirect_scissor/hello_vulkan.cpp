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

#include <sstream>
#include <vulkan/vulkan.hpp>

extern std::vector<std::string> defaultSearchPaths;

#define STB_IMAGE_IMPLEMENTATION
#include "fileformats/stb_image.h"
#include "obj_loader.h"

#include "hello_vulkan.h"
#include "nvh/alignment.hpp"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvk/shaders_vk.hpp"


// Holding the camera matrices
struct CameraMatrices
{
  nvmath::mat4f view;
  nvmath::mat4f proj;
  nvmath::mat4f viewInverse;
  // #VKRay
  nvmath::mat4f projInverse;
};


//--------------------------------------------------------------------------------------------------
// Keep the handle on the device
// Initialize the tool to do all our allocations: buffers, images
//
void HelloVulkan::setup(const vk::Instance&       instance,
                        const vk::Device&         device,
                        const vk::PhysicalDevice& physicalDevice,
                        uint32_t                  queueFamily)
{
  AppBase::setup(instance, device, physicalDevice, queueFamily);
  m_alloc.init(device, physicalDevice);
  m_debug.setup(m_device);
}

//--------------------------------------------------------------------------------------------------
// Called at each frame to update the camera matrix
//
void HelloVulkan::updateUniformBuffer(const vk::CommandBuffer& cmdBuf)
{
  // Prepare new UBO contents on host.
  const float    aspectRatio = m_size.width / static_cast<float>(m_size.height);
  CameraMatrices hostUBO     = {};
  hostUBO.view               = CameraManip.getMatrix();
  hostUBO.proj = nvmath::perspectiveVK(CameraManip.getFov(), aspectRatio, 0.1f, 1000.0f);
  // hostUBO.proj[1][1] *= -1;  // Inverting Y for Vulkan (not needed with perspectiveVK).
  hostUBO.viewInverse = nvmath::invert(hostUBO.view);
  // #VKRay
  hostUBO.projInverse = nvmath::invert(hostUBO.proj);

  // UBO on the device, and what stages access it.
  vk::Buffer deviceUBO = m_cameraMat.buffer;
  auto       uboUsageStages =
      vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eRayTracingShaderKHR;

  // Ensure that the modified UBO is not visible to previous frames.
  vk::BufferMemoryBarrier beforeBarrier;
  beforeBarrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);
  beforeBarrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
  beforeBarrier.setBuffer(deviceUBO);
  beforeBarrier.setOffset(0);
  beforeBarrier.setSize(sizeof hostUBO);
  cmdBuf.pipelineBarrier(uboUsageStages, vk::PipelineStageFlagBits::eTransfer,
                         vk::DependencyFlagBits::eDeviceGroup, {}, {beforeBarrier}, {});

  // Schedule the host-to-device upload. (hostUBO is copied into the cmd
  // buffer so it is okay to deallocate when the function returns).
  cmdBuf.updateBuffer<CameraMatrices>(m_cameraMat.buffer, 0, hostUBO);

  // Making sure the updated UBO will be visible.
  vk::BufferMemoryBarrier afterBarrier;
  afterBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
  afterBarrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
  afterBarrier.setBuffer(deviceUBO);
  afterBarrier.setOffset(0);
  afterBarrier.setSize(sizeof hostUBO);
  cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, uboUsageStages,
                         vk::DependencyFlagBits::eDeviceGroup, {}, {afterBarrier}, {});
}


//--------------------------------------------------------------------------------------------------
// Describing the layout pushed when rendering
//
void HelloVulkan::createDescriptorSetLayout()
{
  using vkDS     = vk::DescriptorSetLayoutBinding;
  using vkDT     = vk::DescriptorType;
  using vkSS     = vk::ShaderStageFlagBits;
  uint32_t nbTxt = static_cast<uint32_t>(m_textures.size());
  uint32_t nbObj = static_cast<uint32_t>(m_objModel.size());

  // Camera matrices (binding = 0)
  m_descSetLayoutBind.addBinding(
      vkDS(0, vkDT::eUniformBuffer, 1, vkSS::eVertex | vkSS::eRaygenKHR));
  // Materials (binding = 1)
  m_descSetLayoutBind.addBinding(
      vkDS(1, vkDT::eStorageBuffer, nbObj, vkSS::eVertex | vkSS::eFragment | vkSS::eClosestHitKHR));
  // Scene description (binding = 2)
  m_descSetLayoutBind.addBinding(  //
      vkDS(2, vkDT::eStorageBuffer, 1, vkSS::eVertex | vkSS::eFragment | vkSS::eClosestHitKHR));
  // Textures (binding = 3)
  m_descSetLayoutBind.addBinding(
      vkDS(3, vkDT::eCombinedImageSampler, nbTxt, vkSS::eFragment | vkSS::eClosestHitKHR));
  // Materials (binding = 4)
  m_descSetLayoutBind.addBinding(
      vkDS(4, vkDT::eStorageBuffer, nbObj, vkSS::eFragment | vkSS::eClosestHitKHR));
  // Storing vertices (binding = 5)
  m_descSetLayoutBind.addBinding(  //
      vkDS(5, vkDT::eStorageBuffer, nbObj, vkSS::eClosestHitKHR));
  // Storing indices (binding = 6)
  m_descSetLayoutBind.addBinding(  //
      vkDS(6, vkDT::eStorageBuffer, nbObj, vkSS::eClosestHitKHR));


  m_descSetLayout = m_descSetLayoutBind.createLayout(m_device);
  m_descPool      = m_descSetLayoutBind.createPool(m_device, 1);
  m_descSet       = nvvk::allocateDescriptorSet(m_device, m_descPool, m_descSetLayout);
}

//--------------------------------------------------------------------------------------------------
// Setting up the buffers in the descriptor set
//
void HelloVulkan::updateDescriptorSet()
{
  std::vector<vk::WriteDescriptorSet> writes;

  // Camera matrices and scene description
  vk::DescriptorBufferInfo dbiUnif{m_cameraMat.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 0, &dbiUnif));
  vk::DescriptorBufferInfo dbiSceneDesc{m_sceneDesc.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 2, &dbiSceneDesc));

  // All material buffers, 1 buffer per OBJ
  std::vector<vk::DescriptorBufferInfo> dbiMat;
  std::vector<vk::DescriptorBufferInfo> dbiMatIdx;
  std::vector<vk::DescriptorBufferInfo> dbiVert;
  std::vector<vk::DescriptorBufferInfo> dbiIdx;
  for(auto& obj : m_objModel)
  {
    dbiMat.emplace_back(obj.matColorBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiMatIdx.emplace_back(obj.matIndexBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiVert.emplace_back(obj.vertexBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiIdx.emplace_back(obj.indexBuffer.buffer, 0, VK_WHOLE_SIZE);
  }
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 1, dbiMat.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 4, dbiMatIdx.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 5, dbiVert.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 6, dbiIdx.data()));

  // All texture samplers
  std::vector<vk::DescriptorImageInfo> diit;
  for(auto& texture : m_textures)
  {
    diit.emplace_back(texture.descriptor);
  }
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 3, diit.data()));

  // Writing the information
  m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Creating the pipeline layout
//
void HelloVulkan::createGraphicsPipeline()
{
  using vkSS = vk::ShaderStageFlagBits;

  vk::PushConstantRange pushConstantRanges = {vkSS::eVertex | vkSS::eFragment, 0,
                                              sizeof(ObjPushConstant)};

  // Creating the Pipeline Layout
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
  vk::DescriptorSetLayout      descSetLayout(m_descSetLayout);
  pipelineLayoutCreateInfo.setSetLayoutCount(1);
  pipelineLayoutCreateInfo.setPSetLayouts(&descSetLayout);
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
  m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Creating the Pipeline
  std::vector<std::string>                paths = defaultSearchPaths;
  nvvk::GraphicsPipelineGeneratorCombined gpb(m_device, m_pipelineLayout, m_offscreenRenderPass);
  gpb.depthStencilState.depthTestEnable = true;
  gpb.addShader(nvh::loadFile("shaders/vert_shader.vert.spv", true, paths, true), vkSS::eVertex);
  gpb.addShader(nvh::loadFile("shaders/frag_shader.frag.spv", true, paths, true), vkSS::eFragment);
  gpb.addBindingDescription({0, sizeof(VertexObj)});
  gpb.addAttributeDescriptions(std::vector<vk::VertexInputAttributeDescription>{
      {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, pos)},
      {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, nrm)},
      {2, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, color)},
      {3, 0, vk::Format::eR32G32Sfloat, offsetof(VertexObj, texCoord)}});

  m_graphicsPipeline = gpb.createPipeline();
  m_debug.setObjectName(m_graphicsPipeline, "Graphics");
}

//--------------------------------------------------------------------------------------------------
// Loading the OBJ file and setting up all buffers
//
void HelloVulkan::loadModel(const std::string& filename, nvmath::mat4f transform)
{
  using vkBU = vk::BufferUsageFlagBits;

  LOGI("Loading File:  %s \n", filename.c_str());
  ObjLoader loader;
  loader.loadModel(filename);

  // Converting from Srgb to linear
  for(auto& m : loader.m_materials)
  {
    m.ambient  = nvmath::pow(m.ambient, 2.2f);
    m.diffuse  = nvmath::pow(m.diffuse, 2.2f);
    m.specular = nvmath::pow(m.specular, 2.2f);
  }

  ObjInstance instance;
  instance.objIndex    = static_cast<uint32_t>(m_objModel.size());
  instance.transform   = transform;
  instance.transformIT = nvmath::transpose(nvmath::invert(transform));
  instance.txtOffset   = static_cast<uint32_t>(m_textures.size());

  ObjModel model;
  model.nbIndices  = static_cast<uint32_t>(loader.m_indices.size());
  model.nbVertices = static_cast<uint32_t>(loader.m_vertices.size());

  // Create the buffers on Device and copy vertices, indices and materials
  nvvk::CommandPool cmdBufGet(m_device, m_graphicsQueueIndex);
  vk::CommandBuffer cmdBuf = cmdBufGet.createCommandBuffer();
  model.vertexBuffer =
      m_alloc.createBuffer(cmdBuf, loader.m_vertices,
                           vkBU::eVertexBuffer | vkBU::eStorageBuffer | vkBU::eShaderDeviceAddress
                               | vkBU::eAccelerationStructureBuildInputReadOnlyKHR);
  model.indexBuffer =
      m_alloc.createBuffer(cmdBuf, loader.m_indices,
                           vkBU::eIndexBuffer | vkBU::eStorageBuffer | vkBU::eShaderDeviceAddress
                               | vkBU::eAccelerationStructureBuildInputReadOnlyKHR);
  model.matColorBuffer = m_alloc.createBuffer(cmdBuf, loader.m_materials, vkBU::eStorageBuffer);
  model.matIndexBuffer = m_alloc.createBuffer(cmdBuf, loader.m_matIndx, vkBU::eStorageBuffer);
  // Creates all textures found
  createTextureImages(cmdBuf, loader.m_textures);
  cmdBufGet.submitAndWait(cmdBuf);
  m_alloc.finalizeAndReleaseStaging();

  std::string objNb = std::to_string(instance.objIndex);
  m_debug.setObjectName(model.vertexBuffer.buffer, (std::string("vertex_" + objNb).c_str()));
  m_debug.setObjectName(model.indexBuffer.buffer, (std::string("index_" + objNb).c_str()));
  m_debug.setObjectName(model.matColorBuffer.buffer, (std::string("mat_" + objNb).c_str()));
  m_debug.setObjectName(model.matIndexBuffer.buffer, (std::string("matIdx_" + objNb).c_str()));

  m_objModel.emplace_back(model);
  m_objInstance.emplace_back(instance);
}

// Add a light-emitting colored lantern to the scene. May only be called before TLAS build.
void HelloVulkan::addLantern(nvmath::vec3f pos, nvmath::vec3f color, float brightness, float radius)
{
  assert(m_lanternCount == 0);  // Indicates TLAS build has not happened yet.

  m_lanterns.push_back({pos, color, brightness, radius});
}

//--------------------------------------------------------------------------------------------------
// Creating the uniform buffer holding the camera matrices
// - Buffer is host visible
//
void HelloVulkan::createUniformBuffer()
{
  using vkBU = vk::BufferUsageFlagBits;
  using vkMP = vk::MemoryPropertyFlagBits;

  m_cameraMat = m_alloc.createBuffer(sizeof(CameraMatrices),
                                     vkBU::eUniformBuffer | vkBU::eTransferDst, vkMP::eDeviceLocal);
  m_debug.setObjectName(m_cameraMat.buffer, "cameraMat");
}

//--------------------------------------------------------------------------------------------------
// Create a storage buffer containing the description of the scene elements
// - Which geometry is used by which instance
// - Transformation
// - Offset for texture
//
void HelloVulkan::createSceneDescriptionBuffer()
{
  using vkBU = vk::BufferUsageFlagBits;
  nvvk::CommandPool cmdGen(m_device, m_graphicsQueueIndex);

  auto cmdBuf = cmdGen.createCommandBuffer();
  m_sceneDesc = m_alloc.createBuffer(cmdBuf, m_objInstance, vkBU::eStorageBuffer);
  cmdGen.submitAndWait(cmdBuf);
  m_alloc.finalizeAndReleaseStaging();
  m_debug.setObjectName(m_sceneDesc.buffer, "sceneDesc");
}

//--------------------------------------------------------------------------------------------------
// Creating all textures and samplers
//
void HelloVulkan::createTextureImages(const vk::CommandBuffer&        cmdBuf,
                                      const std::vector<std::string>& textures)
{
  using vkIU = vk::ImageUsageFlagBits;

  vk::SamplerCreateInfo samplerCreateInfo{
      {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear};
  samplerCreateInfo.setMaxLod(FLT_MAX);
  vk::Format format = vk::Format::eR8G8B8A8Srgb;

  // If no textures are present, create a dummy one to accommodate the pipeline layout
  if(textures.empty() && m_textures.empty())
  {
    nvvk::Texture texture;

    std::array<uint8_t, 4> color{255u, 255u, 255u, 255u};
    vk::DeviceSize         bufferSize      = sizeof(color);
    auto                   imgSize         = vk::Extent2D(1, 1);
    auto                   imageCreateInfo = nvvk::makeImage2DCreateInfo(imgSize, format);

    // Creating the dummy texture
    nvvk::Image image = m_alloc.createImage(cmdBuf, bufferSize, color.data(), imageCreateInfo);
    vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, imageCreateInfo);
    texture                        = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);

    // The image format must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    nvvk::cmdBarrierImageLayout(cmdBuf, texture.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eShaderReadOnlyOptimal);
    m_textures.push_back(texture);
  }
  else
  {
    // Uploading all images
    for(const auto& texture : textures)
    {
      std::stringstream o;
      int               texWidth, texHeight, texChannels;
      o << "media/textures/" << texture;
      std::string txtFile = nvh::findFile(o.str(), defaultSearchPaths, true);

      stbi_uc* stbi_pixels =
          stbi_load(txtFile.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

      std::array<stbi_uc, 4> color{255u, 0u, 255u, 255u};

      stbi_uc* pixels = stbi_pixels;
      // Handle failure
      if(!stbi_pixels)
      {
        texWidth = texHeight = 1;
        texChannels          = 4;
        pixels               = reinterpret_cast<stbi_uc*>(color.data());
      }

      vk::DeviceSize bufferSize = static_cast<uint64_t>(texWidth) * texHeight * sizeof(uint8_t) * 4;
      auto           imgSize    = vk::Extent2D(texWidth, texHeight);
      auto imageCreateInfo = nvvk::makeImage2DCreateInfo(imgSize, format, vkIU::eSampled, true);

      {
        nvvk::ImageDedicated image =
            m_alloc.createImage(cmdBuf, bufferSize, pixels, imageCreateInfo);
        nvvk::cmdGenerateMipmaps(cmdBuf, image.image, format, imgSize, imageCreateInfo.mipLevels);
        vk::ImageViewCreateInfo ivInfo =
            nvvk::makeImageViewCreateInfo(image.image, imageCreateInfo);
        nvvk::Texture texture = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);

        m_textures.push_back(texture);
      }

      stbi_image_free(stbi_pixels);
    }
  }
}

//--------------------------------------------------------------------------------------------------
// Destroying all allocations
//
void HelloVulkan::destroyResources()
{
  m_device.destroy(m_graphicsPipeline);
  m_device.destroy(m_pipelineLayout);
  m_device.destroy(m_descPool);
  m_device.destroy(m_descSetLayout);
  m_alloc.destroy(m_cameraMat);
  m_alloc.destroy(m_sceneDesc);

  for(auto& m : m_objModel)
  {
    m_alloc.destroy(m.vertexBuffer);
    m_alloc.destroy(m.indexBuffer);
    m_alloc.destroy(m.matColorBuffer);
    m_alloc.destroy(m.matIndexBuffer);
  }

  for(auto& t : m_textures)
  {
    m_alloc.destroy(t);
  }

  //#Post
  m_device.destroy(m_postPipeline);
  m_device.destroy(m_postPipelineLayout);
  m_device.destroy(m_postDescPool);
  m_device.destroy(m_postDescSetLayout);
  m_alloc.destroy(m_offscreenColor);
  m_alloc.destroy(m_offscreenDepth);
  m_device.destroy(m_offscreenRenderPass);
  m_device.destroy(m_offscreenFramebuffer);

  // #VKRay
  m_rtBuilder.destroy();
  m_device.destroy(m_rtDescPool);
  m_device.destroy(m_rtDescSetLayout);
  m_device.destroy(m_rtPipeline);
  m_device.destroy(m_rtPipelineLayout);
  m_alloc.destroy(m_rtSBTBuffer);

  m_device.destroy(m_lanternIndirectDescPool);
  m_device.destroy(m_lanternIndirectDescSetLayout);
  m_device.destroy(m_lanternIndirectCompPipeline);
  m_device.destroy(m_lanternIndirectCompPipelineLayout);
  m_alloc.destroy(m_lanternIndirectBuffer);
  m_alloc.destroy(m_lanternVertexBuffer);
  m_alloc.destroy(m_lanternIndexBuffer);
}

//--------------------------------------------------------------------------------------------------
// Drawing the scene in raster mode
//
void HelloVulkan::rasterize(const vk::CommandBuffer& cmdBuf)
{
  using vkPBP = vk::PipelineBindPoint;
  using vkSS  = vk::ShaderStageFlagBits;
  vk::DeviceSize offset{0};

  m_debug.beginLabel(cmdBuf, "Rasterize");

  // Dynamic Viewport
  cmdBuf.setViewport(0, {vk::Viewport(0, 0, (float)m_size.width, (float)m_size.height, 0, 1)});
  cmdBuf.setScissor(0, {{{0, 0}, {m_size.width, m_size.height}}});

  // Drawing all triangles
  cmdBuf.bindPipeline(vkPBP::eGraphics, m_graphicsPipeline);
  cmdBuf.bindDescriptorSets(vkPBP::eGraphics, m_pipelineLayout, 0, {m_descSet}, {});
  for(int i = 0; i < m_objInstance.size(); ++i)
  {
    auto& inst                = m_objInstance[i];
    auto& model               = m_objModel[inst.objIndex];
    m_pushConstant.instanceId = i;  // Telling which instance is drawn
    cmdBuf.pushConstants<ObjPushConstant>(m_pipelineLayout, vkSS::eVertex | vkSS::eFragment, 0,
                                          m_pushConstant);

    cmdBuf.bindVertexBuffers(0, {model.vertexBuffer.buffer}, {offset});
    cmdBuf.bindIndexBuffer(model.indexBuffer.buffer, 0, vk::IndexType::eUint32);
    cmdBuf.drawIndexed(model.nbIndices, 1, 0, 0, 0);
  }
  m_debug.endLabel(cmdBuf);
}

//--------------------------------------------------------------------------------------------------
// Handling resize of the window
//
void HelloVulkan::onResize(int /*w*/, int /*h*/)
{
  createOffscreenRender();
  updatePostDescriptorSet();
  updateRtDescriptorSet();
}

//////////////////////////////////////////////////////////////////////////
// Post-processing
//////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------------------
// Creating an offscreen frame buffer and the associated render pass
//
void HelloVulkan::createOffscreenRender()
{
  m_alloc.destroy(m_offscreenColor);
  m_alloc.destroy(m_offscreenDepth);

  // Creating the color image
  {
    auto colorCreateInfo = nvvk::makeImage2DCreateInfo(m_size, m_offscreenColorFormat,
                                                       vk::ImageUsageFlagBits::eColorAttachment
                                                           | vk::ImageUsageFlagBits::eSampled
                                                           | vk::ImageUsageFlagBits::eStorage);


    nvvk::Image             image  = m_alloc.createImage(colorCreateInfo);
    vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
    m_offscreenColor               = m_alloc.createTexture(image, ivInfo, vk::SamplerCreateInfo());
    m_offscreenColor.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }

  // Creating the depth buffer
  auto depthCreateInfo =
      nvvk::makeImage2DCreateInfo(m_size, m_offscreenDepthFormat,
                                  vk::ImageUsageFlagBits::eDepthStencilAttachment);
  {
    nvvk::Image image = m_alloc.createImage(depthCreateInfo);

    vk::ImageViewCreateInfo depthStencilView;
    depthStencilView.setViewType(vk::ImageViewType::e2D);
    depthStencilView.setFormat(m_offscreenDepthFormat);
    depthStencilView.setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    depthStencilView.setImage(image.image);

    m_offscreenDepth = m_alloc.createTexture(image, depthStencilView);
  }

  // Setting the image layout for both color and depth
  {
    nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
    auto              cmdBuf = genCmdBuf.createCommandBuffer();
    nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenColor.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eGeneral);
    nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenDepth.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                vk::ImageAspectFlagBits::eDepth);

    genCmdBuf.submitAndWait(cmdBuf);
  }

  // Creating a renderpass for the offscreen
  if(!m_offscreenRenderPass)
  {
    m_offscreenRenderPass =
        nvvk::createRenderPass(m_device, {m_offscreenColorFormat}, m_offscreenDepthFormat, 1, true,
                               true, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
  }

  // Creating the frame buffer for offscreen
  std::vector<vk::ImageView> attachments = {m_offscreenColor.descriptor.imageView,
                                            m_offscreenDepth.descriptor.imageView};

  m_device.destroy(m_offscreenFramebuffer);
  vk::FramebufferCreateInfo info;
  info.setRenderPass(m_offscreenRenderPass);
  info.setAttachmentCount(2);
  info.setPAttachments(attachments.data());
  info.setWidth(m_size.width);
  info.setHeight(m_size.height);
  info.setLayers(1);
  m_offscreenFramebuffer = m_device.createFramebuffer(info);
}

//--------------------------------------------------------------------------------------------------
// The pipeline is how things are rendered, which shaders, type of primitives, depth test and more
//
void HelloVulkan::createPostPipeline()
{
  // Push constants in the fragment shader
  vk::PushConstantRange pushConstantRanges = {vk::ShaderStageFlagBits::eFragment, 0, sizeof(float)};

  // Creating the pipeline layout
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
  pipelineLayoutCreateInfo.setSetLayoutCount(1);
  pipelineLayoutCreateInfo.setPSetLayouts(&m_postDescSetLayout);
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
  m_postPipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Pipeline: completely generic, no vertices
  std::vector<std::string> paths = defaultSearchPaths;

  nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_postPipelineLayout,
                                                            m_renderPass);
  pipelineGenerator.addShader(nvh::loadFile("shaders/passthrough.vert.spv", true, paths, true),
                              vk::ShaderStageFlagBits::eVertex);
  pipelineGenerator.addShader(nvh::loadFile("shaders/post.frag.spv", true, paths, true),
                              vk::ShaderStageFlagBits::eFragment);
  pipelineGenerator.rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
  m_postPipeline = pipelineGenerator.createPipeline();
  m_debug.setObjectName(m_postPipeline, "post");
}

//--------------------------------------------------------------------------------------------------
// The descriptor layout is the description of the data that is passed to the vertex or the
// fragment program.
//
void HelloVulkan::createPostDescriptor()
{
  using vkDS = vk::DescriptorSetLayoutBinding;
  using vkDT = vk::DescriptorType;
  using vkSS = vk::ShaderStageFlagBits;

  m_postDescSetLayoutBind.addBinding(vkDS(0, vkDT::eCombinedImageSampler, 1, vkSS::eFragment));
  m_postDescSetLayout = m_postDescSetLayoutBind.createLayout(m_device);
  m_postDescPool      = m_postDescSetLayoutBind.createPool(m_device);
  m_postDescSet       = nvvk::allocateDescriptorSet(m_device, m_postDescPool, m_postDescSetLayout);
}

//--------------------------------------------------------------------------------------------------
// Update the output
//
void HelloVulkan::updatePostDescriptorSet()
{
  vk::WriteDescriptorSet writeDescriptorSets =
      m_postDescSetLayoutBind.makeWrite(m_postDescSet, 0, &m_offscreenColor.descriptor);
  m_device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Draw a full screen quad with the attached image
//
void HelloVulkan::drawPost(vk::CommandBuffer cmdBuf)
{
  m_debug.beginLabel(cmdBuf, "Post");

  cmdBuf.setViewport(0, {vk::Viewport(0, 0, (float)m_size.width, (float)m_size.height, 0, 1)});
  cmdBuf.setScissor(0, {{{0, 0}, {m_size.width, m_size.height}}});

  auto aspectRatio = static_cast<float>(m_size.width) / static_cast<float>(m_size.height);
  cmdBuf.pushConstants<float>(m_postPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
                              aspectRatio);
  cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, m_postPipeline);
  cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_postPipelineLayout, 0,
                            m_postDescSet, {});
  cmdBuf.draw(3, 1, 0, 0);

  m_debug.endLabel(cmdBuf);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------------------
// Initialize Vulkan ray tracing
// #VKRay
void HelloVulkan::initRayTracing()
{
  // Requesting ray tracing properties
  auto properties =
      m_physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                      vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
  m_rtProperties = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
  m_rtBuilder.setup(m_device, &m_alloc, m_graphicsQueueIndex);
}

//--------------------------------------------------------------------------------------------------
// Convert an OBJ model into the ray tracing geometry used to build the BLAS
//
nvvk::RaytracingBuilderKHR::BlasInput HelloVulkan::objectToVkGeometryKHR(const ObjModel& model)
{
  // BLAS builder requires raw device addresses.
  vk::DeviceAddress vertexAddress = m_device.getBufferAddress({model.vertexBuffer.buffer});
  vk::DeviceAddress indexAddress  = m_device.getBufferAddress({model.indexBuffer.buffer});

  uint32_t maxPrimitiveCount = model.nbIndices / 3;

  // Describe buffer as array of VertexObj.
  vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
  triangles.setVertexFormat(vk::Format::eR32G32B32Sfloat);  // vec3 vertex position data.
  triangles.setVertexData(vertexAddress);
  triangles.setVertexStride(sizeof(VertexObj));
  // Describe index data (32-bit unsigned int)
  triangles.setIndexType(vk::IndexType::eUint32);
  triangles.setIndexData(indexAddress);
  // Indicate identity transform by setting transformData to null device pointer.
  triangles.setTransformData({});
  triangles.setMaxVertex(model.nbVertices);

  // Identify the above data as containing opaque triangles.
  vk::AccelerationStructureGeometryKHR asGeom;
  asGeom.setGeometryType(vk::GeometryTypeKHR::eTriangles);
  asGeom.setFlags(vk::GeometryFlagBitsKHR::eOpaque);
  asGeom.geometry.setTriangles(triangles);

  // The entire array will be used to build the BLAS.
  vk::AccelerationStructureBuildRangeInfoKHR offset;
  offset.setFirstVertex(0);
  offset.setPrimitiveCount(maxPrimitiveCount);
  offset.setPrimitiveOffset(0);
  offset.setTransformOffset(0);

  // Our blas is made from only one geometry, but could be made of many geometries
  nvvk::RaytracingBuilderKHR::BlasInput input;
  input.asGeometry.emplace_back(asGeom);
  input.asBuildOffsetInfo.emplace_back(offset);

  return input;
}

// Tesselate a sphere as a list of triangles; return its
// vertices and indices as reference arguments.
void HelloVulkan::fillLanternVerts(std::vector<nvmath::vec3f>& vertices,
                                   std::vector<uint32_t>&      indices)
{
  // Create a spherical lantern model by recursively tesselating an octahedron.
  struct VertexIndex
  {
    nvmath::vec3f vertex;
    uint32_t      index;  // Keep track of this vert's _eventual_ index in vertices.
  };
  struct Triangle
  {
    VertexIndex vert0, vert1, vert2;
  };

  VertexIndex posX{{m_lanternModelRadius, 0, 0}, 0};
  VertexIndex negX{{-m_lanternModelRadius, 0, 0}, 1};
  VertexIndex posY{{0, m_lanternModelRadius, 0}, 2};
  VertexIndex negY{{0, -m_lanternModelRadius, 0}, 3};
  VertexIndex posZ{{0, 0, m_lanternModelRadius}, 4};
  VertexIndex negZ{{0, 0, -m_lanternModelRadius}, 5};
  uint32_t    vertexCount = 6;

  // Initial triangle list is octahedron.
  std::vector<Triangle> triangles{{posX, posY, posZ}, {posX, posY, negZ}, {posX, negY, posZ},
                                  {posX, negY, negZ}, {negX, posY, posZ}, {negX, posY, negZ},
                                  {negX, negY, posZ}, {negX, negY, negZ}};

  // Recursion: every iteration, convert the current model to a new
  // model by breaking each triangle into 4 triangles.
  for(int recursions = 0; recursions < 3; ++recursions)
  {
    std::vector<Triangle> new_triangles;
    for(Triangle t : triangles)
    {
      // Split each of three edges in half, then fixup the
      // length of the midpoint to match m_lanternModelRadius.
      // Record the index the new vertices will eventually have in vertices.
      VertexIndex v01{m_lanternModelRadius * nvmath::normalize(t.vert0.vertex + t.vert1.vertex),
                      vertexCount++};
      VertexIndex v12{m_lanternModelRadius * nvmath::normalize(t.vert1.vertex + t.vert2.vertex),
                      vertexCount++};
      VertexIndex v02{m_lanternModelRadius * nvmath::normalize(t.vert0.vertex + t.vert2.vertex),
                      vertexCount++};

      // Old triangle becomes 4 new triangles.
      new_triangles.push_back({t.vert0, v01, v02});
      new_triangles.push_back({t.vert1, v01, v12});
      new_triangles.push_back({t.vert2, v02, v12});
      new_triangles.push_back({v01, v02, v12});
    }
    triangles = std::move(new_triangles);
  }

  vertices.resize(vertexCount);
  indices.clear();
  indices.reserve(triangles.size() * 3);

  // Write out the vertices to the vertices vector, and
  // connect the tesselated triangles with indices in the indices vector.
  for(Triangle t : triangles)
  {
    vertices[t.vert0.index] = t.vert0.vertex;
    vertices[t.vert1.index] = t.vert1.vertex;
    vertices[t.vert2.index] = t.vert2.vertex;
    indices.push_back(t.vert0.index);
    indices.push_back(t.vert1.index);
    indices.push_back(t.vert2.index);
  }
}

// Create the BLAS storing triangles for the spherical lantern model. This fills in
// m_lanternVertexBuffer: packed VkBuffer of vec3
// m_lanternIndexBuffer:  packed VkBuffer of 32-bit indices
// m_lanternBlasInput:    BLAS for spherical lantern
//
// NOTE: A more elegant way to do this is to use a procedular hit group instead,
// then, this BLAS can just be one AABB. I wanted to avoid introducing the new
// concept of intersection shaders here.
void HelloVulkan::createLanternModel()
{
  std::vector<nvmath::vec3f> vertices;
  std::vector<uint32_t>      indices;
  fillLanternVerts(vertices, indices);

  // Upload vertex and index data to buffers.
  auto usageFlags = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                    | vk::BufferUsageFlagBits::eShaderDeviceAddress;
  auto memFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

  auto vertexBytes      = vertices.size() * sizeof vertices[0];
  m_lanternVertexBuffer = m_alloc.createBuffer(vertexBytes, usageFlags, memFlags);
  void* map             = m_alloc.map(m_lanternVertexBuffer);
  memcpy(map, vertices.data(), vertexBytes);
  m_alloc.unmap(m_lanternVertexBuffer);

  auto indexBytes      = indices.size() * sizeof indices[0];
  m_lanternIndexBuffer = m_alloc.createBuffer(indexBytes, usageFlags, memFlags);
  map                  = m_alloc.map(m_lanternIndexBuffer);
  memcpy(map, indices.data(), indexBytes);
  m_alloc.unmap(m_lanternIndexBuffer);

  // Package vertex and index buffers as BlasInput.
  vk::DeviceAddress vertexAddress = m_device.getBufferAddress({m_lanternVertexBuffer.buffer});
  vk::DeviceAddress indexAddress  = m_device.getBufferAddress({m_lanternIndexBuffer.buffer});

  uint32_t maxPrimitiveCount = uint32_t(indices.size() / 3);

  // Describe buffer as packed array of float vec3.
  vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
  triangles.setVertexFormat(vk::Format::eR32G32B32Sfloat);  // vec3 vertex position data.
  triangles.setVertexData(vertexAddress);
  triangles.setVertexStride(sizeof(nvmath::vec3f));
  // Describe index data (32-bit unsigned int)
  triangles.setIndexType(vk::IndexType::eUint32);
  triangles.setIndexData(indexAddress);
  // Indicate identity transform by setting transformData to null device pointer.
  triangles.setTransformData({});
  triangles.setMaxVertex(uint32_t(vertices.size()));

  // Identify the above data as containing opaque triangles.
  vk::AccelerationStructureGeometryKHR asGeom;
  asGeom.setGeometryType(vk::GeometryTypeKHR::eTriangles);
  asGeom.setFlags(vk::GeometryFlagBitsKHR::eOpaque);
  asGeom.geometry.setTriangles(triangles);

  // The entire array will be used to build the BLAS.
  vk::AccelerationStructureBuildRangeInfoKHR offset;
  offset.setFirstVertex(0);
  offset.setPrimitiveCount(maxPrimitiveCount);
  offset.setPrimitiveOffset(0);
  offset.setTransformOffset(0);

  // Our blas is made from only one geometry, but could be made of many geometries
  m_lanternBlasInput.asGeometry.emplace_back(asGeom);
  m_lanternBlasInput.asBuildOffsetInfo.emplace_back(offset);
}

//--------------------------------------------------------------------------------------------------
//
// Build the array of BLAS in m_rtBuilder. There are `m_objModel.size() + 1`-many BLASes.
// The first `m_objModel.size()` are used for OBJ model BLASes, and the last one
// is used for the lanterns (model generated at runtime).
void HelloVulkan::createBottomLevelAS()
{
  // BLAS - Storing each primitive in a geometry
  std::vector<nvvk::RaytracingBuilderKHR::BlasInput> allBlas;
  allBlas.reserve(m_objModel.size() + 1);

  // Add OBJ models.
  for(const auto& obj : m_objModel)
  {
    auto blas = objectToVkGeometryKHR(obj);

    // We could add more geometry in each BLAS, but we add only one for now
    allBlas.emplace_back(blas);
  }

  // Add lantern model.
  createLanternModel();
  m_lanternBlasId = allBlas.size();
  allBlas.emplace_back(m_lanternBlasInput);

  m_rtBuilder.buildBlas(allBlas, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
}

// Build the TLAS in m_rtBuilder. Requires that the BLASes were already built and
// that all ObjInstance and lanterns have been added. One instance with hitGroupId=0
// is created for every OBJ instance, and one instance with hitGroupId=1 for each lantern.
//
// gl_InstanceCustomIndexEXT will be the index of the instance or lantern in m_objInstance or
// m_lanterns respectively.
void HelloVulkan::createTopLevelAS()
{
  assert(m_lanternCount == 0);
  m_lanternCount = m_lanterns.size();

  std::vector<nvvk::RaytracingBuilderKHR::Instance> tlas;
  tlas.reserve(m_objInstance.size() + m_lanternCount);

  // Add the OBJ instances.
  for(int i = 0; i < static_cast<int>(m_objInstance.size()); i++)
  {
    nvvk::RaytracingBuilderKHR::Instance rayInst;
    rayInst.transform        = m_objInstance[i].transform;  // Position of the instance
    rayInst.instanceCustomId = i;                           // gl_InstanceCustomIndexEXT
    rayInst.blasId           = m_objInstance[i].objIndex;
    rayInst.hitGroupId       = 0;  // We will use the same hit group for all OBJ
    rayInst.flags            = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    tlas.emplace_back(rayInst);
  }

  // Add lantern instances.
  for(int i = 0; i < static_cast<int>(m_lanterns.size()); ++i)
  {
    nvvk::RaytracingBuilderKHR::Instance lanternInstance;
    lanternInstance.transform        = nvmath::translation_mat4(m_lanterns[i].position);
    lanternInstance.instanceCustomId = i;
    lanternInstance.blasId           = uint32_t(m_lanternBlasId);
    lanternInstance.hitGroupId       = 1;  // Next hit group is for lanterns.
    lanternInstance.flags            = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    tlas.emplace_back(lanternInstance);
  }

  m_rtBuilder.buildTlas(tlas, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
}

//--------------------------------------------------------------------------------------------------
// This descriptor set holds the Acceleration structure, output image, and lanterns array buffer.
//
void HelloVulkan::createRtDescriptorSet()
{
  using vkDT   = vk::DescriptorType;
  using vkSS   = vk::ShaderStageFlagBits;
  using vkDSLB = vk::DescriptorSetLayoutBinding;

  // TLAS (binding = 0)
  m_rtDescSetLayoutBind.addBinding(
      vkDSLB(0, vkDT::eAccelerationStructureKHR, 1, vkSS::eRaygenKHR | vkSS::eClosestHitKHR));
  // Output image (binding = 1)
  m_rtDescSetLayoutBind.addBinding(
      vkDSLB(1, vkDT::eStorageImage, 1, vkSS::eRaygenKHR));  // Output image

  // Lantern buffer (binding = 2)
  m_rtDescSetLayoutBind.addBinding(  //
      vkDSLB(2, vkDT::eStorageBuffer, 1, vkSS::eRaygenKHR | vkSS::eClosestHitKHR));
  assert(m_lanternCount > 0);

  m_rtDescPool      = m_rtDescSetLayoutBind.createPool(m_device);
  m_rtDescSetLayout = m_rtDescSetLayoutBind.createLayout(m_device);
  m_rtDescSet       = m_device.allocateDescriptorSets({m_rtDescPool, 1, &m_rtDescSetLayout})[0];

  vk::AccelerationStructureKHR                   tlas = m_rtBuilder.getAccelerationStructure();
  vk::WriteDescriptorSetAccelerationStructureKHR descASInfo;
  descASInfo.setAccelerationStructureCount(1);
  descASInfo.setPAccelerationStructures(&tlas);
  vk::DescriptorImageInfo imageInfo{
      {}, m_offscreenColor.descriptor.imageView, vk::ImageLayout::eGeneral};
  vk::DescriptorBufferInfo lanternBufferInfo{m_lanternIndirectBuffer.buffer, 0,
                                             m_lanternCount * sizeof(LanternIndirectEntry)};

  std::vector<vk::WriteDescriptorSet> writes;
  writes.emplace_back(m_rtDescSetLayoutBind.makeWrite(m_rtDescSet, 0, &descASInfo));
  writes.emplace_back(m_rtDescSetLayoutBind.makeWrite(m_rtDescSet, 1, &imageInfo));
  writes.emplace_back(m_rtDescSetLayoutBind.makeWrite(m_rtDescSet, 2, &lanternBufferInfo));
  m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}


//--------------------------------------------------------------------------------------------------
// Writes the output image to the descriptor set
// - Required when changing resolution
//
void HelloVulkan::updateRtDescriptorSet()
{
  using vkDT = vk::DescriptorType;

  // (1) Output buffer
  vk::DescriptorImageInfo imageInfo{
      {}, m_offscreenColor.descriptor.imageView, vk::ImageLayout::eGeneral};
  vk::WriteDescriptorSet wds{m_rtDescSet, 1, 0, 1, vkDT::eStorageImage, &imageInfo};
  m_device.updateDescriptorSets(wds, nullptr);
}


//--------------------------------------------------------------------------------------------------
// Pipeline for the ray tracer: all shaders, raygen, chit, miss
//
// Shader list:
//
// 0 ======  Ray Generation Shaders  =====================================================
//
//    Raygen shader: Ray generation shader. Casts primary rays from camera to scene.
//
// 1 ======  Miss Shaders  ===============================================================
//
//    Miss shader 0: Miss shader when casting primary rays. Fill in clear color.
//
// 2 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//    Miss shader 1: Miss shader when casting shadow rays towards main light.
//                   Reports no shadow.
//
// 3 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//    Miss shader 2: Miss shader when casting shadow rays towards a lantern.
//                   Reports no lantern hit (-1).
//
// 4 ======  Hit Groups for Primary Rays (sbtRecordOffset=0)  ============================
//
//    chit shader 0: Closest hit shader for primary rays hitting OBJ instances
//                   (hitGroupId=0). Casts shadow ray (to sky light or to lantern,
//                   depending on pass number) and returns specular
//                   and diffuse light to add to output image.
//
// 5 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//    chit shader 1: Closest hit shader for primary rays hitting lantern instances
//                   (hitGroupId=1). Returns color value to replace the current
//                   image pixel color with (lanterns are self-illuminating).
//
// 6 - - - -  Hit Groups for Lantern Shadow Rays (sbtRecordOffset=2) - - - - - - - - - - -
//
//    chit shader 2: Closest hit shader for OBJ instances hit when casting shadow
//                   rays to a lantern. Returns -1 to report that the shadow ray
//                   failed to reach the targetted lantern.
//
// 7 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//    chit shader 3: Closest hit shader for lantern instances hit when casting shadow
//                   rays to a lantern. Returns the gl_CustomInstanceIndexEXT [lantern
//                   number] of the lantern hit.
//
// 8 =====================================================================================
void HelloVulkan::createRtPipeline()
{
  std::vector<std::string> paths = defaultSearchPaths;

  vk::ShaderModule raygenSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/raytrace.rgen.spv", true, paths, true));

  // Miss shader 0 invoked when a primary ray doesn't hit geometry. Fills in clear color.
  vk::ShaderModule missSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/raytrace.rmiss.spv", true, paths, true));

  // Miss shader 1 is invoked when a shadow ray (for the main scene light)
  // misses the geometry. It simply indicates that no occlusion has been found.
  vk::ShaderModule shadowmissSM = nvvk::createShaderModule(
      m_device, nvh::loadFile("shaders/raytraceShadow.rmiss.spv", true, paths, true));

  // Miss shader 2 is invoked when a shadow ray for lantern lighting misses the
  // lantern. It shouldn't be invoked, but I include it just in case.
  vk::ShaderModule lanternmissSM =
      nvvk::createShaderModule(m_device,
                               nvh::loadFile("shaders/lanternShadow.rmiss.spv", true, paths, true));

  std::vector<vk::PipelineShaderStageCreateInfo> stages;

  // Raygen
  vk::RayTracingShaderGroupCreateInfoKHR rg{vk::RayTracingShaderGroupTypeKHR::eGeneral,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back({{}, vk::ShaderStageFlagBits::eRaygenKHR, raygenSM, "main"});
  rg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(rg);
  // Miss
  vk::RayTracingShaderGroupCreateInfoKHR mg{vk::RayTracingShaderGroupTypeKHR::eGeneral,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back({{}, vk::ShaderStageFlagBits::eMissKHR, missSM, "main"});
  mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(mg);

  // Shadow Miss
  stages.push_back({{}, vk::ShaderStageFlagBits::eMissKHR, shadowmissSM, "main"});
  mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(mg);

  // Lantern Miss
  stages.push_back({{}, vk::ShaderStageFlagBits::eMissKHR, lanternmissSM, "main"});
  mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(mg);

  // OBJ Primary Ray Hit Group - Closest Hit + AnyHit (not used)
  vk::ShaderModule chitSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/raytrace.rchit.spv", true, paths, true));

  vk::RayTracingShaderGroupCreateInfoKHR hg{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
                                            VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back({{}, vk::ShaderStageFlagBits::eClosestHitKHR, chitSM, "main"});
  hg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(hg);

  // Lantern Primary Ray Hit Group
  vk::ShaderModule lanternChitSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/lantern.rchit.spv", true, paths, true));

  vk::RayTracingShaderGroupCreateInfoKHR lanternHg{
      vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back({{}, vk::ShaderStageFlagBits::eClosestHitKHR, lanternChitSM, "main"});
  lanternHg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(lanternHg);

  // OBJ Lantern Shadow Ray Hit Group
  vk::ShaderModule lanternShadowObjChitSM = nvvk::createShaderModule(
      m_device,  //
      nvh::loadFile("shaders/lanternShadowObj.rchit.spv", true, paths, true));

  vk::RayTracingShaderGroupCreateInfoKHR lanternShadowObjHg{
      vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back({{}, vk::ShaderStageFlagBits::eClosestHitKHR, lanternShadowObjChitSM, "main"});
  lanternShadowObjHg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(lanternShadowObjHg);

  // Lantern Lantern Shadow Ray Hit Group
  vk::ShaderModule lanternShadowLanternChitSM = nvvk::createShaderModule(
      m_device,  //
      nvh::loadFile("shaders/lanternShadowLantern.rchit.spv", true, paths, true));

  vk::RayTracingShaderGroupCreateInfoKHR lanternShadowLanternHg{
      vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
  stages.push_back(
      {{}, vk::ShaderStageFlagBits::eClosestHitKHR, lanternShadowLanternChitSM, "main"});
  lanternShadowLanternHg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
  m_rtShaderGroups.push_back(lanternShadowLanternHg);

  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;

  // Push constant: we want to be able to update constants used by the shaders
  vk::PushConstantRange pushConstant{vk::ShaderStageFlagBits::eRaygenKHR
                                         | vk::ShaderStageFlagBits::eClosestHitKHR
                                         | vk::ShaderStageFlagBits::eMissKHR,
                                     0, sizeof(RtPushConstant)};
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstant);

  // Descriptor sets: one specific to ray tracing, and one shared with the rasterization pipeline
  std::vector<vk::DescriptorSetLayout> rtDescSetLayouts = {m_rtDescSetLayout, m_descSetLayout};
  pipelineLayoutCreateInfo.setSetLayoutCount(static_cast<uint32_t>(rtDescSetLayouts.size()));
  pipelineLayoutCreateInfo.setPSetLayouts(rtDescSetLayouts.data());

  m_rtPipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Assemble the shader stages and recursion depth info into the ray tracing pipeline

  vk::RayTracingPipelineCreateInfoKHR rayPipelineInfo;
  rayPipelineInfo.setStageCount(static_cast<uint32_t>(stages.size()));  // Stages are shaders
  rayPipelineInfo.setPStages(stages.data());

  // In this case, m_rtShaderGroups.size() == 8: we have one raygen group,
  // three miss shader groups, and four hit groups.
  rayPipelineInfo.setGroupCount(static_cast<uint32_t>(m_rtShaderGroups.size()));
  rayPipelineInfo.setPGroups(m_rtShaderGroups.data());

  rayPipelineInfo.setMaxPipelineRayRecursionDepth(2);  // Ray depth
  rayPipelineInfo.setLayout(m_rtPipelineLayout);
  m_rtPipeline = static_cast<const vk::Pipeline&>(
      m_device.createRayTracingPipelineKHR({}, {}, rayPipelineInfo));

  m_device.destroy(raygenSM);
  m_device.destroy(missSM);
  m_device.destroy(shadowmissSM);
  m_device.destroy(lanternmissSM);
  m_device.destroy(chitSM);
  m_device.destroy(lanternChitSM);
  m_device.destroy(lanternShadowObjChitSM);
  m_device.destroy(lanternShadowLanternChitSM);
}

//--------------------------------------------------------------------------------------------------
// The Shader Binding Table (SBT)
// - getting all shader handles and write them in a SBT buffer
// - Besides exception, this could be always done like this
//   See how the SBT buffer is used in run()
//
void HelloVulkan::createRtShaderBindingTable()
{
  auto groupCount = static_cast<uint32_t>(m_rtShaderGroups.size());
  assert(groupCount == 8 && "Update Comment");  // 8 shaders: raygen, 3 miss, 4 chit

  uint32_t groupHandleSize = m_rtProperties.shaderGroupHandleSize;  // Size of a program identifier
  // Compute the actual size needed per SBT entry (round-up to alignment needed).
  uint32_t groupSizeAligned =
      nvh::align_up(groupHandleSize, m_rtProperties.shaderGroupBaseAlignment);
  // Bytes needed for the SBT.
  uint32_t sbtSize = groupCount * groupSizeAligned;

  // Fetch all the shader handles used in the pipeline. This is opaque data,
  // so we store it in a vector of bytes.
  std::vector<uint8_t> shaderHandleStorage(sbtSize);
  auto result = m_device.getRayTracingShaderGroupHandlesKHR(m_rtPipeline, 0, groupCount, sbtSize,
                                                            shaderHandleStorage.data());
  assert(result == vk::Result::eSuccess);

  // Allocate a buffer for storing the SBT. Give it a debug name for NSight.
  m_rtSBTBuffer = m_alloc.createBuffer(
      sbtSize,
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress
          | vk::BufferUsageFlagBits::eShaderBindingTableKHR,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_debug.setObjectName(m_rtSBTBuffer.buffer, std::string("SBT").c_str());

  // Map the SBT buffer and write in the handles.
  void* mapped = m_alloc.map(m_rtSBTBuffer);
  auto* pData  = reinterpret_cast<uint8_t*>(mapped);
  for(uint32_t g = 0; g < groupCount; g++)
  {
    memcpy(pData, shaderHandleStorage.data() + g * groupHandleSize, groupHandleSize);
    pData += groupSizeAligned;
  }
  m_alloc.unmap(m_rtSBTBuffer);
  m_alloc.finalizeAndReleaseStaging();
}


//--------------------------------------------------------------------------------------------------
// The compute shader just needs read/write access to the buffer of LanternIndirectEntry.
void HelloVulkan::createLanternIndirectDescriptorSet()
{
  using vkDT   = vk::DescriptorType;
  using vkSS   = vk::ShaderStageFlagBits;
  using vkDSLB = vk::DescriptorSetLayoutBinding;

  // Lantern buffer (binding = 0)
  m_lanternIndirectDescSetLayoutBind.addBinding(  //
      vkDSLB(0, vkDT::eStorageBuffer, 1, vkSS::eCompute));

  m_lanternIndirectDescPool      = m_lanternIndirectDescSetLayoutBind.createPool(m_device);
  m_lanternIndirectDescSetLayout = m_lanternIndirectDescSetLayoutBind.createLayout(m_device);
  m_lanternIndirectDescSet       = m_device.allocateDescriptorSets(
      {m_lanternIndirectDescPool, 1, &m_lanternIndirectDescSetLayout})[0];

  assert(m_lanternIndirectBuffer.buffer);
  vk::DescriptorBufferInfo lanternBufferInfo{m_lanternIndirectBuffer.buffer, 0,
                                             m_lanternCount * sizeof(LanternIndirectEntry)};

  std::vector<vk::WriteDescriptorSet> writes;
  writes.emplace_back(m_lanternIndirectDescSetLayoutBind.makeWrite(m_lanternIndirectDescSet, 0,
                                                                   &lanternBufferInfo));
  m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

// Create compute pipeline used to fill m_lanternIndirectBuffer with parameters
// for dispatching the correct number of ray traces.
void HelloVulkan::createLanternIndirectCompPipeline()
{
  // Compile compute shader and package as stage.
  vk::ShaderModule computeShader = nvvk::createShaderModule(
      m_device,  //
      nvh::loadFile("shaders/lanternIndirect.comp.spv", true, defaultSearchPaths, true));
  vk::PipelineShaderStageCreateInfo stageInfo;
  stageInfo.setStage(vk::ShaderStageFlagBits::eCompute);
  stageInfo.setModule(computeShader);
  stageInfo.setPName("main");

  // Set up push constant and pipeline layout.
  constexpr auto        pushSize   = static_cast<uint32_t>(sizeof(m_lanternIndirectPushConstants));
  vk::PushConstantRange pushCRange = {vk::ShaderStageFlagBits::eCompute, 0, pushSize};
  static_assert(pushSize <= 128, "Spec guarantees only 128 byte push constant");
  vk::PipelineLayoutCreateInfo layoutInfo;
  layoutInfo.setSetLayoutCount(1);
  layoutInfo.setPSetLayouts(&m_lanternIndirectDescSetLayout);
  layoutInfo.setPushConstantRangeCount(1);
  layoutInfo.setPPushConstantRanges(&pushCRange);
  m_lanternIndirectCompPipelineLayout = m_device.createPipelineLayout(layoutInfo);

  // Create compute pipeline.
  vk::ComputePipelineCreateInfo pipelineInfo;
  pipelineInfo.setStage(stageInfo);
  pipelineInfo.setLayout(m_lanternIndirectCompPipelineLayout);
  m_lanternIndirectCompPipeline =
      static_cast<const vk::Pipeline&>(m_device.createComputePipeline({}, pipelineInfo));

  m_device.destroy(computeShader);
}

// Allocate the buffer used to pass lantern info + ray trace indirect parameters to ray tracer.
// Fill in the lantern info from m_lanterns (indirect info is filled per-frame on device
// using a compute shader). Must be called only after TLAS build.
//
// The buffer is an array of LanternIndirectEntry, entry i is for m_lanterns[i].
void HelloVulkan::createLanternIndirectBuffer()
{
  assert(m_lanternCount > 0);
  assert(m_lanternCount == m_lanterns.size());

  // m_alloc behind the scenes uses cmdBuf to transfer data to the buffer.
  nvvk::CommandPool cmdBufGet(m_device, m_graphicsQueueIndex);
  vk::CommandBuffer cmdBuf = cmdBufGet.createCommandBuffer();

  using Usage = vk::BufferUsageFlagBits;
  m_lanternIndirectBuffer =
      m_alloc.createBuffer(sizeof(LanternIndirectEntry) * m_lanternCount,
                           Usage::eIndirectBuffer | Usage::eTransferDst
                               | Usage::eShaderDeviceAddress | Usage::eStorageBuffer,
                           vk::MemoryPropertyFlagBits::eDeviceLocal);

  std::vector<LanternIndirectEntry> entries(m_lanternCount);
  for(size_t i = 0; i < m_lanternCount; ++i)
    entries[i].lantern = m_lanterns[i];
  cmdBuf.updateBuffer(m_lanternIndirectBuffer.buffer, 0, entries.size() * sizeof entries[0],
                      entries.data());

  cmdBufGet.submitAndWait(cmdBuf);
}

//--------------------------------------------------------------------------------------------------
// Ray Tracing the scene
//
// The raytracing is split into multiple passes:
//
// First pass fills in the initial values for every pixel in the output image.
// Illumination and shadow rays come from the main light.
//
// Subsequently, one lantern pass is run for each lantern in the scene. We run
// a compute shader to calculate a bounding scissor rectangle for each lantern's light
// effect. This is stored in m_lanternIndirectBuffer. Then an indirect trace rays command
// is run for every lantern within its scissor rectangle. The lanterns' light
// contribution is additively blended into the output image.
void HelloVulkan::raytrace(const vk::CommandBuffer& cmdBuf, const nvmath::vec4f& clearColor)
{
  // Before tracing rays, we need to dispatch the compute shaders that
  // fill in the ray trace indirect parameters for each lantern pass.

  // First, barrier before, ensure writes aren't visible to previous frame.
  vk::BufferMemoryBarrier bufferBarrier;
  bufferBarrier.setSrcAccessMask(vk::AccessFlagBits::eIndirectCommandRead);
  bufferBarrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
  bufferBarrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
  bufferBarrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
  bufferBarrier.setBuffer(m_lanternIndirectBuffer.buffer);
  bufferBarrier.offset = 0;
  bufferBarrier.size   = m_lanternCount * sizeof m_lanterns[0];
  cmdBuf.pipelineBarrier(                         //
      vk::PipelineStageFlagBits::eDrawIndirect,   //
      vk::PipelineStageFlagBits::eComputeShader,  //
      vk::DependencyFlags(0),                     //
      {}, {bufferBarrier}, {});

  // Bind compute shader, update push constant and descriptors, dispatch compute.
  cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, m_lanternIndirectCompPipeline);
  nvmath::mat4 view                           = getViewMatrix();
  m_lanternIndirectPushConstants.viewRowX     = view.row(0);
  m_lanternIndirectPushConstants.viewRowY     = view.row(1);
  m_lanternIndirectPushConstants.viewRowZ     = view.row(2);
  m_lanternIndirectPushConstants.proj         = getProjMatrix();
  m_lanternIndirectPushConstants.nearZ        = nearZ;
  m_lanternIndirectPushConstants.screenX      = m_size.width;
  m_lanternIndirectPushConstants.screenY      = m_size.height;
  m_lanternIndirectPushConstants.lanternCount = int32_t(m_lanternCount);
  cmdBuf.pushConstants<LanternIndirectPushConstants>(m_lanternIndirectCompPipelineLayout,
                                                     vk::ShaderStageFlagBits::eCompute, 0,
                                                     m_lanternIndirectPushConstants);
  cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_lanternIndirectCompPipelineLayout, 0,
                            {m_lanternIndirectDescSet}, {});
  cmdBuf.dispatch(1, 1, 1);

  // Ensure compute results are visible when doing indirect ray trace.
  bufferBarrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
  bufferBarrier.setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead);
  cmdBuf.pipelineBarrier(                         //
      vk::PipelineStageFlagBits::eComputeShader,  //
      vk::PipelineStageFlagBits::eDrawIndirect,   //
      vk::DependencyFlags(0),                     //
      {}, {bufferBarrier}, {});


  // Now move on to the actual ray tracing.
  m_debug.beginLabel(cmdBuf, "Ray trace");

  // Initialize push constant values
  m_rtPushConstants.clearColor        = clearColor;
  m_rtPushConstants.lightPosition     = m_pushConstant.lightPosition;
  m_rtPushConstants.lightIntensity    = m_pushConstant.lightIntensity;
  m_rtPushConstants.lightType         = m_pushConstant.lightType;
  m_rtPushConstants.lanternPassNumber = -1;  // Global non-lantern pass
  m_rtPushConstants.screenX           = m_size.width;
  m_rtPushConstants.screenY           = m_size.height;
  m_rtPushConstants.lanternDebug      = m_lanternDebug;

  cmdBuf.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rtPipeline);
  cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, m_rtPipelineLayout, 0,
                            {m_rtDescSet, m_descSet}, {});
  cmdBuf.pushConstants<RtPushConstant>(m_rtPipelineLayout,
                                       vk::ShaderStageFlagBits::eRaygenKHR
                                           | vk::ShaderStageFlagBits::eClosestHitKHR
                                           | vk::ShaderStageFlagBits::eMissKHR,
                                       0, m_rtPushConstants);

  // Size of a program identifier
  uint32_t groupSize =
      nvh::align_up(m_rtProperties.shaderGroupHandleSize, m_rtProperties.shaderGroupBaseAlignment);
  uint32_t          groupStride = groupSize;
  vk::DeviceAddress sbtAddress  = m_device.getBufferAddress({m_rtSBTBuffer.buffer});

  using Stride = vk::StridedDeviceAddressRegionKHR;
  std::array<Stride, 4> strideAddresses{
      Stride{sbtAddress + 0u * groupSize, groupStride, groupSize * 1},  // raygen
      Stride{sbtAddress + 1u * groupSize, groupStride, groupSize * 3},  // miss
      Stride{sbtAddress + 4u * groupSize, groupStride, groupSize * 4},  // hit
      Stride{0u, 0u, 0u}};                                              // callable

  // First pass, illuminate scene with global light.
  cmdBuf.traceRaysKHR(&strideAddresses[0], &strideAddresses[1],  //
                      &strideAddresses[2], &strideAddresses[3],  //
                      m_size.width, m_size.height, 1);

  // Lantern passes, ensure previous pass completed, then add light contribution from each lantern.
  for(int i = 0; i < static_cast<int>(m_lanternCount); ++i)
  {
    // Barrier to ensure previous pass finished.
    vk::Image                 offscreenImage{m_offscreenColor.image};
    vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0,
                                         VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS);
    vk::ImageMemoryBarrier    imageBarrier;
    imageBarrier.setOldLayout(vk::ImageLayout::eGeneral);
    imageBarrier.setNewLayout(vk::ImageLayout::eGeneral);
    imageBarrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    imageBarrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    imageBarrier.setImage(offscreenImage);
    imageBarrier.setSubresourceRange(colorRange);
    imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
    imageBarrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,  //
                           vk::PipelineStageFlagBits::eRayTracingShaderKHR,  //
                           vk::DependencyFlags(0),                           //
                           {}, {}, {imageBarrier});

    // Set lantern pass number.
    m_rtPushConstants.lanternPassNumber = i;
    cmdBuf.pushConstants<RtPushConstant>(m_rtPipelineLayout,
                                         vk::ShaderStageFlagBits::eRaygenKHR
                                             | vk::ShaderStageFlagBits::eClosestHitKHR
                                             | vk::ShaderStageFlagBits::eMissKHR,
                                         0, m_rtPushConstants);

    // Execute lantern pass.
    cmdBuf.traceRaysIndirectKHR(&strideAddresses[0], &strideAddresses[1],  //
                                &strideAddresses[2], &strideAddresses[3],  //
                                m_device.getBufferAddress({m_lanternIndirectBuffer.buffer})
                                    + i * sizeof(LanternIndirectEntry));
  }

  m_debug.endLabel(cmdBuf);
}
