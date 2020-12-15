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
#include <vulkan/vulkan.hpp>

#define NVVK_ALLOC_DEDICATED
#include "nvvk/allocator_vk.hpp"
#include "nvvk/appbase_vkpp.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"

// #VKRay
#include "nvvk/raytraceKHR_vk.hpp"

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
  void loadModel(const std::string& filename, nvmath::mat4f transform = nvmath::mat4f(1));
  void addLantern(nvmath::vec3f pos, nvmath::vec3f color, float brightness, float radius);
  void updateDescriptorSet();
  void createUniformBuffer();
  void createSceneDescriptionBuffer();
  void createTextureImages(const vk::CommandBuffer&        cmdBuf,
                           const std::vector<std::string>& textures);

  nvmath::mat4 getViewMatrix()
  {
    return CameraManip.getMatrix();
  }

  static constexpr float nearZ = 0.1f;
  nvmath::mat4 getProjMatrix()
  {
    const float aspectRatio = m_size.width / static_cast<float>(m_size.height);
    return nvmath::perspectiveVK(CameraManip.getFov(), aspectRatio, nearZ, 1000.0f);
  }

  void updateUniformBuffer(const vk::CommandBuffer&);
  void onResize(int /*w*/, int /*h*/) override;
  void destroyResources();
  void rasterize(const vk::CommandBuffer& cmdBuff);

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
    float         lightIntensity{40.f};
    int           lightType{0};  // 0: point, 1: infinite
  };
  ObjPushConstant m_pushConstant;

  // Information on each colored lantern illuminating the scene.
  struct Lantern
  {
    nvmath::vec3f position;
    nvmath::vec3f color;
    float         brightness;
    float         radius;     // Max world-space distance that light illuminates.
  };

  // Information on each colored lantern, plus the info needed for dispatching the
  // indirect ray trace command used to add its brightness effect.
  // The dispatched ray trace covers pixels (offsetX, offsetY) to
  // (offsetX + indirectCommand.width - 1, offsetY + indirectCommand.height - 1).
  struct LanternIndirectEntry
  {
    // Filled in by the device using a compute shader.
    // NOTE: I rely on indirectCommand being the first member.
    VkTraceRaysIndirectCommandKHR indirectCommand;
    int32_t                       offsetX;
    int32_t                       offsetY;

    // Filled in by the host.
    Lantern                       lantern;
  };

  // Array of objects and instances in the scene. Not modifiable after acceleration structure build.
  std::vector<ObjModel>    m_objModel;
  std::vector<ObjInstance> m_objInstance;

  // Array of lanterns in scene. Not modifiable after acceleration structure build.
  std::vector<Lantern> m_lanterns;

  // Graphic pipeline
  vk::PipelineLayout          m_pipelineLayout;
  vk::Pipeline                m_graphicsPipeline;
  nvvk::DescriptorSetBindings m_descSetLayoutBind;
  vk::DescriptorPool          m_descPool;
  vk::DescriptorSetLayout     m_descSetLayout;
  vk::DescriptorSet           m_descSet;

  nvvk::Buffer               m_cameraMat;  // Device-Host of the camera matrices
  nvvk::Buffer               m_sceneDesc;  // Device buffer of the OBJ instances
  std::vector<nvvk::Texture> m_textures;   // vector of all textures of the scene

  nvvk::AllocatorDedicated m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvk::DebugUtil          m_debug;  // Utility to name objects

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
  vk::Format                  m_offscreenDepthFormat{vk::Format::eD32Sfloat};

  // #VKRay
  void                                  initRayTracing();
  nvvk::RaytracingBuilderKHR::BlasInput objectToVkGeometryKHR(const ObjModel& model);

private:
  void fillLanternVerts(std::vector<nvmath::vec3f>& vertices, std::vector<uint32_t>& indices);
  void                                  createLanternModel();

public:
  void                                  createBottomLevelAS();
  void                                  createTopLevelAS();
  void                                  createRtDescriptorSet();
  void                                  updateRtDescriptorSet();
  void                                  createRtPipeline();
  void                                  createLanternIndirectDescriptorSet();
  void                                  createLanternIndirectCompPipeline();
  void                                  createRtShaderBindingTable();
  void                                  createLanternIndirectBuffer();

  void raytrace(const vk::CommandBuffer& cmdBuf, const nvmath::vec4f& clearColor);

  // Used to store lantern model, generated at runtime.
  const float                           m_lanternModelRadius = 0.125;
  nvvk::Buffer                          m_lanternVertexBuffer;
  nvvk::Buffer                          m_lanternIndexBuffer;
  nvvk::RaytracingBuilderKHR::BlasInput m_lanternBlasInput{};

  // Index of lantern's BLAS in the BLAS array stored in m_rtBuilder.
  size_t                                m_lanternBlasId;

  vk::PhysicalDeviceRayTracingPipelinePropertiesKHR   m_rtProperties;
  nvvk::RaytracingBuilderKHR                          m_rtBuilder;
  nvvk::DescriptorSetBindings                         m_rtDescSetLayoutBind;
  vk::DescriptorPool                                  m_rtDescPool;
  vk::DescriptorSetLayout                             m_rtDescSetLayout;
  vk::DescriptorSet                                   m_rtDescSet;
  std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  vk::PipelineLayout                                  m_rtPipelineLayout;
  vk::Pipeline                                        m_rtPipeline;
  nvvk::DescriptorSetBindings                         m_lanternIndirectDescSetLayoutBind;
  vk::DescriptorPool                                  m_lanternIndirectDescPool;
  vk::DescriptorSetLayout                             m_lanternIndirectDescSetLayout;
  vk::DescriptorSet                                   m_lanternIndirectDescSet;
  vk::PipelineLayout                                  m_lanternIndirectCompPipelineLayout;
  vk::Pipeline                                        m_lanternIndirectCompPipeline;
  nvvk::Buffer                                        m_rtSBTBuffer;

  // Buffer to source vkCmdTraceRaysIndirectKHR indirect parameters and lantern color,
  // position, etc. from when doing lantern lighting passes.
  nvvk::Buffer m_lanternIndirectBuffer;
  VkDeviceSize m_lanternCount = 0; // Set to actual lantern count after TLAS build, as
                                   // that is the point no more lanterns may be added.

  // Push constant for ray trace pipeline.
  struct RtPushConstant
  {
    // Background color
    nvmath::vec4f clearColor;

    // Information on the light in the sky used when lanternPassNumber = -1.
    nvmath::vec3f lightPosition;
    float         lightIntensity;
    int32_t       lightType;

    // -1 if this is the full-screen pass. Otherwise, this pass is to add light
    // from lantern number lanternPassNumber. We use this to lookup trace indirect
    // parameters in m_lanternIndirectBuffer.
    int32_t       lanternPassNumber;

    // Pixel dimensions of the output image.
    int32_t       screenX;
    int32_t       screenY;

    // See m_lanternDebug.
    int32_t       lanternDebug;
  } m_rtPushConstants;

  // Copied to RtPushConstant::lanternDebug. If true,
  // make lantern produce constant light regardless of distance
  // so that I can see the screen rectangle coverage.
  bool m_lanternDebug = false;


  // Push constant for compute shader filling lantern indirect buffer.
  // Barely fits in 128-byte push constant limit guaranteed by spec.
  struct LanternIndirectPushConstants
  {
    nvmath::vec4 viewRowX; // First 3 rows of view matrix.
    nvmath::vec4 viewRowY; // Set w=1 implicitly in shader.
    nvmath::vec4 viewRowZ;

    nvmath::mat4 proj;     // Perspective matrix
    float nearZ;           // Near plane used to create projection matrix.

    // Pixel dimensions of output image (needed to scale NDC to screen coordinates).
    int32_t screenX;
    int32_t screenY;

    // Length of the LanternIndirectEntry array.
    int32_t lanternCount;
  } m_lanternIndirectPushConstants;
};
