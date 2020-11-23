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

#include "nvvk/descriptorsets_vk.hpp"
#include "vkalloc.hpp"

#include "nvmath/nvmath.h"
#include "nvvk/raytraceKHR_vk.hpp"
#include "obj.hpp"

class Raytracer
{
public:
  void setup(const vk::Device&         device,
             const vk::PhysicalDevice& physicalDevice,
             nvvk::Allocator*          allocator,
             uint32_t                  queueFamily);
  void destroy();

  nvvk::RaytracingBuilderKHR::BlasInput objectToVkGeometryKHR(const ObjModel& model);
  nvvk::RaytracingBuilderKHR::BlasInput implicitToVkGeometryKHR(const ImplInst& implicitObj);
  void createBottomLevelAS(std::vector<ObjModel>& models, ImplInst& implicitObj);
  void createTopLevelAS(std::vector<ObjInstance>& instances, ImplInst& implicitObj);
  void createRtDescriptorSet(const vk::ImageView& outputImage);
  void updateRtDescriptorSet(const vk::ImageView& outputImage);
  void createRtPipeline(vk::DescriptorSetLayout& sceneDescLayout);
  void createRtShaderBindingTable();
  void raytrace(const vk::CommandBuffer& cmdBuf,
                const nvmath::vec4f&     clearColor,
                vk::DescriptorSet&       sceneDescSet,
                vk::Extent2D&            size,
                ObjPushConstants&        sceneConstants);

private:
  nvvk::Allocator*   m_alloc{nullptr};  // Allocator for buffer, images, acceleration structures
  vk::PhysicalDevice m_physicalDevice;
  vk::Device         m_device;
  int                m_graphicsQueueIndex{0};
  nvvk::DebugUtil    m_debug;  // Utility to name objects


  vk::PhysicalDeviceRayTracingPipelinePropertiesKHR   m_rtProperties;
  nvvk::RaytracingBuilderKHR                          m_rtBuilder;
  nvvk::DescriptorSetBindings                         m_rtDescSetLayoutBind;
  vk::DescriptorPool                                  m_rtDescPool;
  vk::DescriptorSetLayout                             m_rtDescSetLayout;
  vk::DescriptorSet                                   m_rtDescSet;
  std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  vk::PipelineLayout                                  m_rtPipelineLayout;
  vk::Pipeline                                        m_rtPipeline;
  nvvk::Buffer                                        m_rtSBTBuffer;

  struct RtPushConstants
  {
    nvmath::vec4f clearColor;
    nvmath::vec3f lightPosition;
    float         lightIntensity;
    nvmath::vec3f lightDirection{-1, -1, -1};
    float         lightSpotCutoff{deg2rad(12.5f)};
    float         lightSpotOuterCutoff{deg2rad(17.5f)};
    int           lightType{0};
    int           frame{0};
  } m_rtPushConstants;
};
