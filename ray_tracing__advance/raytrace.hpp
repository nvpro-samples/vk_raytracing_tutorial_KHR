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

#include "nvmath/nvmath.h"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/raytraceKHR_vk.hpp"
#include "nvvk/sbtwrapper_vk.hpp"
#include "obj.hpp"

class Raytracer
{
public:
  void setup(const vk::Device&         device,
             const vk::PhysicalDevice& physicalDevice,
             nvvk::ResourceAllocator*  allocator,
             uint32_t                  queueFamily);
  void destroy();

  auto objectToVkGeometryKHR(const ObjModel& model);
  auto implicitToVkGeometryKHR(const ImplInst& implicitObj);
  void createBottomLevelAS(std::vector<ObjModel>& models, ImplInst& implicitObj);
  void createTopLevelAS(std::vector<ObjInstance>& instances, ImplInst& implicitObj);
  void createRtDescriptorSet(const vk::ImageView& outputImage);
  void updateRtDescriptorSet(const vk::ImageView& outputImage);
  void createRtPipeline(vk::DescriptorSetLayout& sceneDescLayout);
  void raytrace(const vk::CommandBuffer& cmdBuf,
                const nvmath::vec4f&     clearColor,
                vk::DescriptorSet&       sceneDescSet,
                vk::Extent2D&            size,
                ObjPushConstants&        sceneConstants);

private:
  nvvk::ResourceAllocator* m_alloc{
      nullptr};  // Allocator for buffer, images, acceleration structures
  vk::PhysicalDevice m_physicalDevice;
  vk::Device         m_device;
  int                m_graphicsQueueIndex{0};
  nvvk::DebugUtil    m_debug;  // Utility to name objects
  nvvk::SBTWrapper   m_sbtWrapper;

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
