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


// ImGui - standalone example application for Glfw + Vulkan, using programmable
// pipeline If you are new to ImGui, see examples/README.txt and documentation
// at the top of imgui.cpp.

#include <array>

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"

#include "hello_vulkan.h"
#include "imgui/imgui_camera_widget.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvpsystem.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/context_vk.hpp"


//////////////////////////////////////////////////////////////////////////
#define UNUSED(x) (void)(x)
//////////////////////////////////////////////////////////////////////////

// Default search path for shaders
std::vector<std::string> defaultSearchPaths;


// GLFW Callback functions
static void onErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Extra UI
void renderUI(HelloVulkan& helloVk, bool useRaytracer, uint32_t& numPhotons, uint32_t& numBeams)
{
    const uint32_t minValBeam   = 1;
    const uint32_t maxValBeam   = helloVk.maxNumBeamSamples;
    const uint32_t minValPhoton = 4 * 4;
    const uint32_t maxValPhoton = helloVk.maxNumPhotonSamples;

    ImGuiH::CameraWidget();
    bool isCollapsed = ImGui::CollapsingHeader("Light");
    if(isCollapsed)
        return;

    ImGui::SliderFloat3("Position", &helloVk.m_pcRaster.lightPosition.x, -20.f, 20.f);

    if(!useRaytracer)
    {
        ImGui::RadioButton("Point", &helloVk.m_pcRaster.lightType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Infinite", &helloVk.m_pcRaster.lightType, 1);
        ImGui::SliderFloat("Intensity", &helloVk.m_pcRaster.lightIntensity, 0.f, 20.f);
        return;
    }


    ImGui::ColorEdit3("Near Color", reinterpret_cast<float*>(&helloVk.m_beamNearColor), ImGuiColorEditFlags_NoSmallPreview);
    ImGuiH::Control::Color(
        std::string("Near Color"), "Air color near the light source, seen at the eye position",
        reinterpret_cast<float*>(&(helloVk.m_beamNearColor))
    );

    ImGui::ColorEdit3("Distant Color", reinterpret_cast<float*>(&helloVk.m_beamUnitDistantColor), ImGuiColorEditFlags_NoSmallPreview);
    ImGuiH::Control::Color(
        std::string("Distant Color"),
        "Air color one unit distance away from the light source, at direction orthogonal from the "
        "line between eye and the light source, seen at eye position.\n"
        "Each color channel will be adjusted to fit between 0.1% to 100% of the value in the same "
        "channel of Near Color\n",
        reinterpret_cast<float*>(&(helloVk.m_beamUnitDistantColor))
    );

    ImGui::SliderFloat("Air Albedo", &helloVk.m_airAlbedo, 0.0f, 1.0f);

    //ImGui::SliderFloat("Color Intensity", &helloVk.m_beamColorIntensity, 1.f, 150.f);
    //ImGui::SliderFloat("Beam Radius", &helloVk.m_beamRadius, 0.05f, 5.0f);
    //ImGui::SliderFloat("Surface Photon Radius", &helloVk.m_photonRadius, 0.05f, 5.0f);
    //ImGui::SliderFloat("HG Assymetric Factor", &helloVk.m_hgAssymFactor, -0.99f, 0.99f);

    ImGui::SliderFloat("Light Intensity", &helloVk.m_beamIntensity, 0.0f, 300.f);

    ImGui::Checkbox("Light Motion", &helloVk.m_lightMotion);
    ImGui::Checkbox("Light Variation On", &helloVk.m_lightVariation);
    ImGui::SliderFloat("Air Albedo", &helloVk.m_lightVariationInterval, 1.0f, 100.0f);


    ImGuiH::Control::Custom(
        "Air Scatter", 
        "Light Scattering Coffiecient in Air",
        [&] { return ImGui::InputFloat3("##Eye", &helloVk.m_pcRay.airScatterCoff.x, "%.5f"); },
        ImGuiH::Control::Flags::Disabled
    );

    ImGuiH::Control::Custom(
        "Air Extinction", 
        "Light Extinction Coffiecient in Air",
        [&] { return ImGui::InputFloat3("##Eye", &helloVk.m_pcRay.airExtinctCoff.x, "%.5f"); },
        ImGuiH::Control::Flags::Disabled
    );

    ImGuiH::Control::Custom(
        "Light Power", 
        "Source Light Power",
        [&] { return ImGui::InputFloat3("##Eye", &helloVk.m_pcRay.sourceLight.x, "%.5f"); },
        ImGuiH::Control::Flags::Disabled
    );



    ImGuiH::Control::Slider(
        std::string("Beam Radius"), "Sampling radius for beams", 
        &helloVk.m_beamRadius, 
        nullptr,
        ImGuiH::Control::Flags::Normal, 
        0.05f, 5.0f
    );

    ImGuiH::Control::Slider(
        std::string("Photon Radius"),  // Name of the parameter
        "Sampling radius for surface photons", 
        &helloVk.m_photonRadius, 
        nullptr,
        ImGuiH::Control::Flags::Normal, 
        0.05f, 5.0f
    );

    ImGuiH::Control::Slider(
        std::string("HG Assymetric Factor"),  // Name of the parameter
        "Henyey and Greenstein Assymetric Factor for air.\n"
        "Positive: more front light scattering.\n"
        "Negative: more back light scattering.",
        &helloVk.m_hgAssymFactor, 
        nullptr, 
        ImGuiH::Control::Flags::Normal, 
        -0.99f, 0.99f
    );

    ImGui::Checkbox("Surface Photon", &helloVk.m_usePhotonMapping);
    ImGui::Checkbox("Photon Beam", &helloVk.m_usePhotonBeam);
    ImGui::Checkbox("Show Solid Beam/Surface Color", &helloVk.m_showDirectColor);

    ImGui::SliderScalar("Sample Beams", ImGuiDataType_U32, &numBeams, &minValBeam, &maxValBeam, nullptr, ImGuiSliderFlags_None);
    ImGui::SliderScalar("Sample Photons", ImGuiDataType_U32, &numPhotons, &minValPhoton, &maxValPhoton, nullptr, ImGuiSliderFlags_None);

    if(ImGui::SmallButton("Set Defaults"))
        helloVk.setDefaults();
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int const SAMPLE_WIDTH  = 1600;
static int const SAMPLE_HEIGHT = 900;


//--------------------------------------------------------------------------------------------------
// Application Entry
//
int main(int argc, char** argv)
{
    UNUSED(argc);

    // Setup GLFW window
    glfwSetErrorCallback(onErrorCallback);
    if(!glfwInit())
    {
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(SAMPLE_WIDTH, SAMPLE_HEIGHT, PROJECT_NAME, nullptr, nullptr);

    // Setup camera
    CameraManip.setWindowSize(SAMPLE_WIDTH, SAMPLE_HEIGHT);
    CameraManip.setLookat(nvmath::vec3f(0, 0, 15), nvmath::vec3f(0, 0, 0), nvmath::vec3f(0, 1, 0));

    // Setup Vulkan
    if(!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

    // setup some basic things for the sample, logging file for example
    NVPSystem system(PROJECT_NAME);

    // Search path for shaders and other media
    defaultSearchPaths = {
        NVPSystem::exePath() + PROJECT_RELDIRECTORY,
        NVPSystem::exePath() + PROJECT_RELDIRECTORY "..",
        std::string(PROJECT_NAME),
    };

    // Vulkan required extensions
    assert(glfwVulkanSupported() == 1);
    uint32_t count{0};
    auto     reqExtensions = glfwGetRequiredInstanceExtensions(&count);

    // Requesting Vulkan extensions and layers
    nvvk::ContextCreateInfo contextInfo;
    contextInfo.setVersion(1, 2);                       // Using Vulkan 1.2
    for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
    contextInfo.addInstanceExtension(reqExtensions[ext_id]);
    contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
    contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);            // Enabling ability to present rendering

    // #VKRay: Activate the ray tracing extension
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
    contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline
    contextInfo.addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    VkPhysicalDeviceShaderClockFeaturesKHR clockFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
    contextInfo.addDeviceExtension(VK_KHR_SHADER_CLOCK_EXTENSION_NAME, false, &clockFeature);

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    contextInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayQueryFeatures);

    /*
    contextInfo.addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
    VkValidationFeaturesEXT      validationInfo{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    VkValidationFeatureEnableEXT validationFeatureToEnable = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
    validationInfo.enabledValidationFeatureCount           = 1;
    validationInfo.pEnabledValidationFeatures              = &validationFeatureToEnable;
    contextInfo.instanceCreateInfoExt                      = &validationInfo;
    #ifdef _WIN32
    _putenv_s("DEBUG_PRINTF_TO_STDOUT", "1");
    #else   // If not _WIN32
    putenv("DEBUG_PRINTF_TO_STDOUT=1");
    #endif  // _WIN32
    */
    // Creating Vulkan base application
    nvvk::Context vkctx{};
    vkctx.initInstance(contextInfo);
    // Find all compatible devices
    auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
    assert(!compatibleDevices.empty());
    // Use a compatible device
    vkctx.initDevice(compatibleDevices[0], contextInfo);

    // Create example
    HelloVulkan helloVk;

    // Window need to be opened to get the surface on which to draw
    const VkSurfaceKHR surface = helloVk.getVkSurface(vkctx.m_instance, window);
    vkctx.setGCTQueueWithPresent(surface);

    helloVk.setup(vkctx.m_instance, vkctx.m_device, vkctx.m_physicalDevice, vkctx.m_queueGCT.familyIndex);
    helloVk.setDefaults();
    uint32_t newNumBeams   = helloVk.m_numBeamSamples;
    uint32_t newNumPhotons = helloVk.m_numPhotonSamples;
    helloVk.createSwapchain(surface, SAMPLE_WIDTH, SAMPLE_HEIGHT);
    helloVk.createDepthBuffer();
    helloVk.createRenderPass();
    helloVk.createFrameBuffers();

    // Setup Imgui
    helloVk.initGUI(0);  // Using sub-pass 0

    helloVk.createBeamBoundingBox();

    // Creation of the example
    helloVk.loadScene(nvh::findFile("media/scenes/cornellBox.gltf", defaultSearchPaths, true));


    helloVk.createOffscreenRender();
    helloVk.createDescriptorSetLayout();
    helloVk.createGraphicsPipeline();
    helloVk.createUniformBuffer();
    helloVk.updateDescriptorSet();

    // #VKRay
    helloVk.initRayTracing();

    helloVk.createBottomLevelAS();
    helloVk.createTopLevelAS();

    helloVk.createPbDescriptorSet();
    helloVk.createPbPipeline();

    helloVk.createBeamASResources();

    nvmath::vec4f clearColor   = nvmath::vec4f(0.52, 0.81, 0.92, 1.00f);
    bool          useRaytracer = true;

    helloVk.createRtDescriptorSet();
    helloVk.createRtPipeline();
    helloVk.updateRtDescriptorSetBeamTlas();

    helloVk.createPostDescriptor();
    helloVk.createPostPipeline();
    helloVk.updatePostDescriptorSet();


    helloVk.setupGlfwCallbacks(window);
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Main loop
    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if(helloVk.isMinimized())
            continue;

        // Start the Dear ImGui frame
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        // Show UI window.
        if(helloVk.showGui())
        {
            ImGuiH::Panel::Begin();
            ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clearColor));
            ImGui::Checkbox("Ray Tracer mode", &useRaytracer);  // Switch between raster and ray tracing

            renderUI(helloVk, useRaytracer, newNumPhotons, newNumBeams);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGuiH::Control::Info("", "", "(F10) Toggle Pane", ImGuiH::Control::Flags::Disabled);
            ImGuiH::Panel::End();
        }

        // Start rendering the scene
        helloVk.prepareFrame();

        // Start command buffer of this frame
        auto                   curFrame = helloVk.getCurFrame();
        const VkCommandBuffer& cmdBuf   = helloVk.getCommandBuffers()[curFrame];

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuf, &beginInfo);

        // Updating camera buffer
        helloVk.updateUniformBuffer(cmdBuf);

        // Clearing screen
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color        = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
        clearValues[1].depthStencil = {1.0f, 0};

        // Offscreen render pass
        {
            VkRenderPassBeginInfo offscreenRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            offscreenRenderPassBeginInfo.clearValueCount = 2;
            offscreenRenderPassBeginInfo.pClearValues    = clearValues.data();
            offscreenRenderPassBeginInfo.renderPass      = helloVk.m_offscreenRenderPass;
            offscreenRenderPassBeginInfo.framebuffer     = helloVk.m_offscreenFramebuffer;
            offscreenRenderPassBeginInfo.renderArea      = {{0, 0}, helloVk.getSize()};

            if(useRaytracer)
            {
                helloVk.setBeamPushConstants(clearColor);
                helloVk.m_numBeamSamples   = newNumBeams;
                helloVk.m_numPhotonSamples = newNumPhotons;
                helloVk.buildPbTlas(clearColor, cmdBuf);
                helloVk.raytrace(cmdBuf);
            }
            else
            {
                vkCmdBeginRenderPass(cmdBuf, &offscreenRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
                helloVk.rasterize(cmdBuf);
                vkCmdEndRenderPass(cmdBuf);
            }
        }

        // 2nd rendering pass: tone mapper, UI
        {
            VkRenderPassBeginInfo postRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            postRenderPassBeginInfo.clearValueCount = 2;
            postRenderPassBeginInfo.pClearValues    = clearValues.data();
            postRenderPassBeginInfo.renderPass      = helloVk.getRenderPass();
            postRenderPassBeginInfo.framebuffer     = helloVk.getFramebuffers()[curFrame];
            postRenderPassBeginInfo.renderArea      = {{0, 0}, helloVk.getSize()};

            // Rendering tonemapper
            vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            helloVk.drawPost(cmdBuf);
            // Rendering UI
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
            vkCmdEndRenderPass(cmdBuf);
        }

        // Submit for display
        vkEndCommandBuffer(cmdBuf);
        helloVk.submitFrame();
    }

        // Cleanup
        vkDeviceWaitIdle(helloVk.getDevice());

        helloVk.destroyResources();
        helloVk.destroy();
        vkctx.deinit();

        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }
