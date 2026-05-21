/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

//
// This sample shows how to render an anti-aliased wireframe in a ray tracing
// pipeline using the helpers in nvshaders/wireframe.h.slang. It reuses the
// ray-differentials concept from sample 19, but because the wireframe is
// only needed on primary-ray hits, the differentials are recomputed locally
// in the closest-hit shader rather than carried in the payload.
//


// #define USE_NSIGHT_AFTERMATH

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
  {                                                                                                                    \
    printf((format), __VA_ARGS__);                                                                                     \
    printf("\n");                                                                                                      \
  }

#include "shaders/shaderio.h"

// Pre-compiled shaders
#include "_autogen/sky_simple.slang.h"
#include "_autogen/tonemapper.slang.h"
#include "_autogen/rtwireframe.slang.h"

// Common base class (see 02_basic)
#include "common/rt_base.hpp"


// Wireframe style presets - mirrors WIREFRAME_PRESETS in nvshaders/wireframe.h.slang.
// Used to populate the UI preset selector and apply one-shot defaults.
namespace {
struct WireframePresetEntry
{
  const char*                   name;
  shaderio::WireframeSettingsIO settings;
};

static const WireframePresetEntry kWireframePresets[] = {
    // Standard wireframe with uniform thickness
    {"Default", {0.3f, glm::vec2(.0f, .0f), 2.5f, 0, 0.5f, 5.0f}},
    // Varying thickness, creates star-like appearance
    {"Star", {3.0f, glm::vec2(0.0f, 1.3f), 0.0f, 0, 0.5f, 5.0f}},
    // Thin varying lines with stipple
    {"Flake", {1.3f, glm::vec2(0.7f, 0.0f), 0.1f, 1, 0.8f, 10.0f}},
    // Thin smooth wireframe with heavy smoothing
    {"Thicker", {0.3f, glm::vec2(1.0f, 1.0f), 2.0f, 0, 0.4f, 8.0f}},
    // Classic stippled / dashed pattern
    {"Stipple", {0.5f, glm::vec2(1.0f, 1.0f), 1.0f, 1, 0.5f, 20.0f}},
};
constexpr int kWireframePresetCount = int(sizeof(kWireframePresets) / sizeof(kWireframePresets[0]));
}  // namespace


class RtWireframe : public RtBase
{

public:
  RtWireframe()           = default;
  ~RtWireframe() override = default;

  //-------------------------------------------------------------------------------
  // Override virtual methods from RtBase
  //-------------------------------------------------------------------------------

  void onUIRender() override
  {
    if(ImGui::Begin("Settings"))
    {
      ImGui::SeparatorText("Wireframe");

      // Preset selector - applies a full reset when changed.
      const char* previewName =
          (m_presetIndex >= 0 && m_presetIndex < kWireframePresetCount) ? kWireframePresets[m_presetIndex].name : "Custom";
      if(ImGui::BeginCombo("Preset", previewName))
      {
        for(int i = 0; i < kWireframePresetCount; ++i)
        {
          const bool selected = (i == m_presetIndex);
          if(ImGui::Selectable(kWireframePresets[i].name, selected))
          {
            m_presetIndex   = i;
            m_pushValues.wf = kWireframePresets[i].settings;
          }
          if(selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::ColorEdit3("Wireframe Color", glm::value_ptr(m_pushValues.wireframeColor));
      ImGui::SliderFloat("Thickness", &m_pushValues.wf.thickness, 0.0f, 5.0f);
      ImGui::SliderFloat("Smoothing", &m_pushValues.wf.smoothing, 0.0f, 5.0f);
      ImGui::SliderFloat2("Thickness Var", glm::value_ptr(m_pushValues.wf.thicknessVar), 0.0f, 2.0f);

      bool stipple = (m_pushValues.wf.stipple != 0);
      if(ImGui::Checkbox("Stipple", &stipple))
        m_pushValues.wf.stipple = stipple ? 1 : 0;

      if(m_pushValues.wf.stipple != 0)
      {
        ImGui::SliderFloat("Stipple Length", &m_pushValues.wf.stippleLength, 0.0f, 1.0f);
        ImGui::SliderFloat("Stipple Repeats", &m_pushValues.wf.stippleRepeats, 1.0f, 30.0f);
      }
    }
    ImGui::End();
    RtBase::onUIRender();
  }

  void createScene() override
  {
    SCOPED_TIMER(__FUNCTION__);

    VkCommandBuffer cmd = m_app->createTempCmdBuffer();

    // Load the GLTF resources: plane (large floor) + teapot (the star of the show).
    {
      tinygltf::Model planeModel = nvsamples::loadGltfResources(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));
      tinygltf::Model teapotModel =
          nvsamples::loadGltfResources(nvutils::findFile("teapot.gltf", nvsamples::getResourcesDirs()));
      nvsamples::importGltfData(m_sceneResource, planeModel, m_stagingUploader);
      nvsamples::importGltfData(m_sceneResource, teapotModel, m_stagingUploader);
    }

    // Two simple materials - only baseColorFactor is consulted by the wireframe
    // shader (and only indirectly, since this sample blends background <-> wire colour).
    m_sceneResource.materials = {
        // Plane
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 0.0f, .roughnessFactor = 1.0f},
        // Teapot
        {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), .metallicFactor = 1.0f, .roughnessFactor = 0.4f},
    };

    m_sceneResource.instances = {
        // Plane at origin
        {.transform = glm::mat4(1), .materialIndex = 0, .meshIndex = 0},
        // Teapot sitting on the plane, scaled down so it fits in the frame.
        {.transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, .5f, 0)), glm::vec3(0.25f)), .materialIndex = 1, .meshIndex = 1},
    };

    // Camera framing - close enough to see triangle density on the teapot.
    m_cameraManip->setLookat({1., 0.8, 0.8}, {0.1, 0.4, -0.1}, {0, 1, 0});

    // Light grey background so the default black wireframe is visible.
    m_sceneResource.sceneInfo.backgroundColor = glm::vec3(0.85f, 0.85f, 0.85f);
    m_sceneResource.sceneInfo.useSky          = 0;

    // Default wireframe look = first preset, applied to the push-constant defaults.
    m_pushValues.wf             = kWireframePresets[0].settings;
    m_pushValues.wireframeColor = glm::vec3(0.9f, 0.9f, 0.9f);

    // Create and upload the scene buffers
    nvsamples::createGltfSceneInfoBuffer(m_sceneResource, m_stagingUploader);
    m_stagingUploader.cmdUploadAppended(cmd);
    m_app->submitAndWaitTempCmdBuffer(cmd);
  }

  void createRayTracingPipeline() override
  {
    // For re-creation
    destroyRayTracingPipeline();

    // Compile shader, and if failed, use pre-compiled shaders
    VkShaderModuleCreateInfo shaderCode = compileSlangShader("rtwireframe.slang", rtwireframe_slang);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for(auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    stages[eRaygen].pNext     = &shaderCode;
    stages[eRaygen].pName     = "rgenMain";
    stages[eRaygen].stage     = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext       = &shaderCode;
    stages[eMiss].pName       = "rmissMain";
    stages[eMiss].stage       = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext = &shaderCode;
    stages[eClosestHit].pName = "rchitMain";
    stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Shader groups
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader       = VK_SHADER_UNUSED_KHR;
    group.closestHitShader   = VK_SHADER_UNUSED_KHR;
    group.generalShader      = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    // Raygen
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shaderGroups.push_back(group);

    // Miss
    group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shaderGroups.push_back(group);

    // closest hit shader
    group.type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader    = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shaderGroups.push_back(group);

    // Create the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = createRayTracingPipelineCreateInfo(stages, shaderGroups, 2);
    vkCreateRayTracingPipelinesKHR(m_app->getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Creating the SBT
    createShaderBindingTable(rtPipelineInfo);
  }

private:
  int m_presetIndex = 0;  // Currently selected preset index (UI state only).
};

//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  nvutils::ParameterParser   cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Setting up the Vulkan context, instance and device extensions
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},     // To build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},  // To use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},                  // Required by ray tracing pipeline
          },
  };

  if(!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // Create Vulkan context using the new method
  auto vkContext = RtBase::createVulkanContext(vkSetup);
  if(!vkContext)
  {
    return 1;
  }

  // Setting up the application
  appInfo.name           = "Ray Tracing Tutorial - 20 Wireframe";
  appInfo.instance       = vkContext->getInstance();
  appInfo.device         = vkContext->getDevice();
  appInfo.physicalDevice = vkContext->getPhysicalDevice();
  appInfo.queues         = vkContext->getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial    = std::make_shared<RtWireframe>();
  auto elemCamera  = std::make_shared<nvapp::ElementCamera>();
  auto windowTitle = std::make_shared<nvapp::ElementDefaultWindowTitle>();
  auto windowMenu  = std::make_shared<nvapp::ElementDefaultMenu>();
  auto camManip    = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Add elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();
  application.deinit();
  vkContext->deinit();

  return 0;
}
