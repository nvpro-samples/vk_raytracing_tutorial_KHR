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

#include "nvvkhl/appbase_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"
#include "shaders/host_device.h"

// #VKRay
#include "nvvk/raytraceKHR_vk.hpp"

//--------------------------------------------------------------------------------------------------
// Simple rasterizer of OBJ objects
// - Each OBJ loaded are stored in an `ObjModel` and referenced by a `ObjInstance`
// - It is possible to have many `ObjInstance` referencing the same `ObjModel`
// - Rendering is done in an offscreen framebuffer
// - The image of the framebuffer is displayed in post-process in a full-screen quad
//
class HelloVulkan : public nvvkhl::AppBaseVk
{
public:
  void setup(const VkInstance& instance, const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t queueFamily) override;
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  void loadModel(const std::string& filename, glm::mat4 transform = glm::mat4(1));
  void addLantern(glm::vec3 pos, glm::vec3 color, float brightness, float radius);
  void updateDescriptorSet();
  void createUniformBuffer();
  void createObjDescriptionBuffer();
  void createTextureImages(const VkCommandBuffer& cmdBuf, const std::vector<std::string>& textures);

  glm::mat4 getViewMatrix() { return CameraManip.getMatrix(); }

  static constexpr float nearZ = 0.1f;
  glm::mat4              getProjMatrix()
  {
    const float aspectRatio = m_size.width / static_cast<float>(m_size.height);
    glm::mat4   proj        = glm::perspectiveRH_ZO(glm::radians(CameraManip.getFov()), aspectRatio, nearZ, 1000.0f);
    proj[1][1] *= -1;
    return proj;
  }

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

  struct ObjInstance
  {
    glm::mat4 transform;    // Matrix of the instance
    uint32_t  objIndex{0};  // Model index reference
  };


  // Information pushed at each draw call
  PushConstantRaster m_pcRaster{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},  // Identity matrix
      {10.f, 15.f, 8.f},                                 // light position
      0,                                                 // instance Id
      100.f,                                             // light intensity
      0                                                  // light type
  };

  // Information on each colored lantern illuminating the scene.
  struct Lantern
  {
    glm::vec3 position;
    glm::vec3 color;
    float     brightness{0};
    float     radius{0};  // Max world-space distance that light illuminates.
  };

  // Information on each colored lantern, plus the info needed for dispatching the
  // indirect ray trace command used to add its brightness effect.
  // The dispatched ray trace covers pixels (offsetX, offsetY) to
  // (offsetX + indirectCommand.width - 1, offsetY + indirectCommand.height - 1).
  struct LanternIndirectEntry
  {
    // Filled in by the device using a compute shader.
    // NOTE: I rely on indirectCommand being the first member.
    VkTraceRaysIndirectCommandKHR indirectCommand{};
    int32_t                       offsetX{0};
    int32_t                       offsetY{0};

    // Filled in by the host.
    Lantern lantern{};
  };

  // Array of objects and instances in the scene. Not modifiable after acceleration structure build.
  std::vector<ObjModel>    m_objModel;   // Model on host
  std::vector<ObjDesc>     m_objDesc;    // Model description for device access
  std::vector<ObjInstance> m_instances;  // Scene model instances

  // Array of lanterns in scene. Not modifiable after acceleration structure build.
  std::vector<Lantern> m_lanterns;

  // Graphic pipeline
  VkPipelineLayout            m_pipelineLayout;
  VkPipeline                  m_graphicsPipeline;
  nvvk::DescriptorSetBindings m_descSetLayoutBind;
  VkDescriptorPool            m_descPool;
  VkDescriptorSetLayout       m_descSetLayout;
  VkDescriptorSet             m_descSet;

  nvvk::Buffer m_bGlobals;  // Device-Host of the camera matrices
  nvvk::Buffer m_bObjDesc;  // Device buffer of the OBJ descriptions

  std::vector<nvvk::Texture> m_textures;  // vector of all textures of the scene


  nvvk::ResourceAllocatorDma m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvk::DebugUtil            m_debug;  // Utility to name objects


  // #Post - Draw the rendered image on a quad using a tonemapper
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

private:
  void fillLanternVerts(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices);
  void createLanternModel();

public:
  void createBottomLevelAS();
  void createTopLevelAS();
  void createRtDescriptorSet();
  void updateRtDescriptorSet();
  void createRtPipeline();
  void createLanternIndirectDescriptorSet();
  void createLanternIndirectCompPipeline();
  void createRtShaderBindingTable();
  void createLanternIndirectBuffer();

  void raytrace(const VkCommandBuffer& cmdBuf, const glm::vec4& clearColor);

  // Used to store lantern model, generated at runtime.
  const float                           m_lanternModelRadius = 0.125;
  nvvk::Buffer                          m_lanternVertexBuffer;
  nvvk::Buffer                          m_lanternIndexBuffer;
  nvvk::RaytracingBuilderKHR::BlasInput m_lanternBlasInput{};

  // Index of lantern's BLAS in the BLAS array stored in m_rtBuilder.
  size_t m_lanternBlasId;

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
  nvvk::RaytracingBuilderKHR                        m_rtBuilder;
  nvvk::DescriptorSetBindings                       m_rtDescSetLayoutBind;
  VkDescriptorPool                                  m_rtDescPool;
  VkDescriptorSetLayout                             m_rtDescSetLayout;
  VkDescriptorSet                                   m_rtDescSet;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  VkPipelineLayout                                  m_rtPipelineLayout;
  VkPipeline                                        m_rtPipeline;
  nvvk::DescriptorSetBindings                       m_lanternIndirectDescSetLayoutBind;
  VkDescriptorPool                                  m_lanternIndirectDescPool;
  VkDescriptorSetLayout                             m_lanternIndirectDescSetLayout;
  VkDescriptorSet                                   m_lanternIndirectDescSet;
  VkPipelineLayout                                  m_lanternIndirectCompPipelineLayout;
  VkPipeline                                        m_lanternIndirectCompPipeline;

  nvvk::Buffer                    m_rtSBTBuffer;
  VkStridedDeviceAddressRegionKHR m_rgenRegion{};
  VkStridedDeviceAddressRegionKHR m_missRegion{};
  VkStridedDeviceAddressRegionKHR m_hitRegion{};
  VkStridedDeviceAddressRegionKHR m_callRegion{};

  // Buffer to source vkCmdTraceRaysIndirectKHR indirect parameters and lantern color,
  // position, etc. from when doing lantern lighting passes.
  nvvk::Buffer m_lanternIndirectBuffer;
  VkDeviceSize m_lanternCount = 0;  // Set to actual lantern count after TLAS build, as
                                    // that is the point no more lanterns may be added.

  // Push constant for ray tracer.
  PushConstantRay m_pcRay{};


  // Copied to RtPushConstant::lanternDebug. If true,
  // make lantern produce constant light regardless of distance
  // so that I can see the screen rectangle coverage.
  bool m_lanternDebug = false;


  // Push constant for compute shader filling lantern indirect buffer.
  // Barely fits in 128-byte push constant limit guaranteed by spec.
  struct LanternIndirectPushConstants
  {
    glm::vec4 viewRowX;  // First 3 rows of view matrix.
    glm::vec4 viewRowY;  // Set w=1 implicitly in shader.
    glm::vec4 viewRowZ;

    glm::mat4 proj{};   // Perspective matrix
    float     nearZ{};  // Near plane used to create projection matrix.

    // Pixel dimensions of output image (needed to scale NDC to screen coordinates).
    int32_t screenX{};
    int32_t screenY{};

    // Length of the LanternIndirectEntry array.
    int32_t lanternCount{};
  } m_lanternIndirectPushConstants;
};
