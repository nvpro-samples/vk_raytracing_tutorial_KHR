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

#pragma once

#include <array>
#include <random>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "imgui/imgui_helper.h"

#include "hello_vulkan.h"
#include "imgui/imgui_camera_widget.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvpsystem.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/context_vk.hpp"
#include "SPHFluid.h"

//////////////////////////////////////////////////////////////////////////
#define UNUSED(x) (void)(x)
//////////////////////////////////////////////////////////////////////////

using nlohmann::json;

// Default search path for shaders
std::vector<std::string> defaultSearchPaths;
bool                     resumeSimulation;


// GLFW Callback functions
static void onErrorCallback(int error, const char* description)
{
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Extra UI
void renderUI(HelloVulkan& helloVk)
{
  bool changed = false;

  changed |= ImGuiH::CameraWidget();
  if(ImGui::CollapsingHeader("Light"))
  {
    auto& pc = helloVk.m_pcRaster;
    changed |= ImGui::RadioButton("Point", &pc.lightType, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Infinite", &pc.lightType, 1);

    changed |= ImGui::SliderFloat3("Position", &pc.lightPosition.x, -20.f, 20.f);
    changed |= ImGui::SliderFloat("Intensity", &pc.lightIntensity, 0.f, 150.f);
    changed |= ImGui::SliderInt("Max Frames", &helloVk.m_maxFrames, 1, 100);
  }

  if(ImGui::Button("[R]"))
    resumeSimulation = true;
  ImGui::SameLine();
  ImGui::Text("to resume");

  if(ImGui::Button("[P]"))
    resumeSimulation = false;
  ImGui::SameLine();
  ImGui::Text("to pause");

  if(changed)
    helloVk.resetFrame();
}

template <typename T>
void getJsonValue(const json& j, const std::string& name, T& value)
{
  auto fieldIt = j.find(name);
  if(fieldIt != j.end())
  {
    value = (*fieldIt);
  }
  LOGE("Could not find JSON field %s", name.c_str());
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int const SAMPLE_WIDTH  = 1280;
static int const SAMPLE_HEIGHT = 720;


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
  CameraManip.setLookat(glm::vec3(4, 4, 4), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));

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

  // #VKRay: Activate the ray tracing extension
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
  contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline

  // Vulkan version
  contextInfo.setVersion(1, 2);                       // Using Vulkan 1.2
  for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
    contextInfo.addInstanceExtension(reqExtensions[ext_id]);
  contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);              // FPS in titlebar
  contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
  contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);            // Enabling ability to present rendering

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
  helloVk.createSwapchain(surface, SAMPLE_WIDTH, SAMPLE_HEIGHT);
  helloVk.createDepthBuffer();
  helloVk.createRenderPass();
  helloVk.createFrameBuffers();

  // Setup Imgui
  helloVk.initGUI(0);  // Using sub-pass 0


  // --------------------------------------SPH Initialization--------------------------------------
  int numParticles = NULL;

  double minx;
  double maxx;
  double miny;
  double maxy;
  double minz;
  double maxz;

  try
  {
    nlohmann::json sceneConfig = getJSONPartitionFromFile("C:\\Users\\Alejandro\\Documents\\TFG\\SPH_Fluid-Vulkan_RTX\\RTX_SPH_Fluid\\ray_tracing__before\\simConfig.json"
                                                    ,"scene-config");
    
    numParticles = sceneConfig["num-particles"].get<int>();
    minx         = sceneConfig["spawn-volume"]["min-x"].get<double>();
    maxx         = sceneConfig["spawn-volume"]["max-x"].get<double>();
    miny         = sceneConfig["spawn-volume"]["min-y"].get<double>();
    maxy         = sceneConfig["spawn-volume"]["max-y"].get<double>();
    minz         = sceneConfig["spawn-volume"]["min-z"].get<double>();
    maxz         = sceneConfig["spawn-volume"]["max-z"].get<double>();
  }
  catch(const std::exception& e)
  {
    std::cout << "Error: " << e.what() << std::endl;
  }

  // SPHFluid Class
  auto                   fluidSim = SPHFluid();
  std::vector<glm::vec3> points;
  auto const             particles = new Particle;

  double rx = 0.2;
  double ry = 0.2;
  double rz = 0.2;

  for(int i = 0; i < numParticles; i++)
  {
    float x = ((float)rand() / RAND_MAX) * (maxx - minx);
    float y = ((float)rand() / RAND_MAX) * (maxy - miny);
    float z = ((float)rand() / RAND_MAX) * (maxz - minz);

    auto p = glm::vec3(x, y, z);
    points.push_back(p);
  }

  fluidSim.addFluidParticles(points);
  fluidSim.configurationShow();

  Particle*               fluidParticles    = fluidSim.getFluidParticles();
  std::vector<glm::vec3>* particlesPos      = &fluidParticles->pos_list;
  std::vector<double>*    particlePressures = &fluidParticles->pressure_list;
  std::vector<glm::vec3>* particleSpeeds    = &fluidParticles->vel_list;
  float                   particlescale     = 0.01f;

  // Variables to control SPH simulation
  bool       simulationThreadRunning = false;
  bool       isCalculatingPhysics    = false;
  std::mutex fluidSimMutex;  //Mutex to guarantee simulation
  int        stepsWithOutSimulation = 0;

  // Timing
  double deltaTime;
  double lastTimeSim = glfwGetTime();  // first time for deltaTime calculation
  double lastTimeFPS = lastTimeSim;
  double accumTime   = 0;

  // -------Create the particle instances in the TLAS-------
  helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths, true),
                    glm::translate(glm::mat4(1), glm::vec3(0.0f, (float)fluidSim.getYLimitMin(), 0.0f)));
  helloVk.loadModel(nvh::findFile("media/scenes/cube.obj", defaultSearchPaths, true),
                    glm::scale(glm::mat4(1.f), glm::vec3(0.25f)) * glm::translate(glm::mat4(1), glm::vec3(0.5f, 0.25f, 0.5f)));
  helloVk.loadModel(nvh::findFile("media/scenes/sphere.obj", defaultSearchPaths, true),
                    glm::scale(glm::mat4(1.f), glm::vec3(particlescale)) * glm::translate(glm::mat4(1), points[0]));
  
  for(uint32_t n = 1; n < points.size(); n++)
  {
    glm::mat4 mat = glm::translate(glm::mat4(1), points[n]);
    mat = mat * glm::scale(glm::mat4(1.f), glm::vec3(particlescale));
    helloVk.m_instances.push_back({mat,2});
  }

  if(fluidSim.cudaMode_ == "physics" || fluidSim.cudaMode_ == "full")
  {
    fluidSim.gpuCudaMalloc();
    fluidSim.gpuCudaCpyFromHost();
  }

  helloVk.createOffscreenRender();
  helloVk.createDescriptorSetLayout();
  helloVk.createGraphicsPipeline();
  helloVk.createUniformBuffer();
  helloVk.createObjDescriptionBuffer();
  helloVk.updateDescriptorSet();

  //INITIALIZE RAY TRACING AND ACCELERATION STRUCTURES
  helloVk.initRayTracing();
  helloVk.createBottomLevelAS();         // Create BLASes
  helloVk.createTopLevelAS();            // Create TLAS
  helloVk.createRtDescriptorSet();       // Create Descriptor Set
  helloVk.createRtPipeline();            // Create the pipeline
  helloVk.createRtShaderBindingTable();  // Create the Shader Bindng Table

  bool useRaytracer = true;

  helloVk.createPostDescriptor();
  helloVk.createPostPipeline();
  helloVk.updatePostDescriptorSet();
  glm::vec4 clearColor = glm::vec4(0.170, 0.346, 0.891, 1.00f);


  helloVk.setupGlfwCallbacks(window);
  ImGui_ImplGlfw_InitForVulkan(window, true);
  int bucleCount = 0;
  // Main loop
  while(!glfwWindowShouldClose(window))
  {
    
    glfwPollEvents();
    if(helloVk.isMinimized())
      continue;

    // Calcular deltaTime
    double currentTime = glfwGetTime();
    deltaTime          = currentTime - lastTimeSim;
    lastTimeSim        = currentTime;
    accumTime += deltaTime;

    stepsWithOutSimulation++;

    // Start the Dear ImGui frame
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Show UI window.
    if(helloVk.showGui())
    {
      ImGuiH::Panel::Begin();

      bool changed = false;
      // Edit 3 floats representing a color
      changed |= ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clearColor));
      // Switch between raster and ray tracing
      changed |= ImGui::Checkbox("Ray Tracer mode", &useRaytracer);
      if(changed)
        helloVk.resetFrame();

      renderUI(helloVk);

      // Max rays taht can be shot by the RayGen shader to generate reflections
      changed |= ImGui::SliderInt("Max Depth", &helloVk.m_pcRay.maxDepth, 1, 50);
      if(changed)
        helloVk.resetFrame();
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
    //Exec SPH simulation
    if(resumeSimulation && !simulationThreadRunning)
    {
      simulationThreadRunning = true;


      std::cout << "---------------------[BUCLE_EXEC_COUNT " << bucleCount << " ]--: " << stepsWithOutSimulation << std::endl;
      stepsWithOutSimulation = 0;

      std::thread simThread([&]() {
        fluidSimMutex.lock();  // Bloquear el mutex antes de acceder a fluidSim
        fluidSim.update(deltaTime);
        fluidSimMutex.unlock();           // Desbloquear el mutex despues de la actualizacion
        simulationThreadRunning = false;  // Indicar que el hilo de simulacion ha terminado
      });
      simThread.detach();  // Desconectar el hilo para que se ejecute de forma independiente

      bucleCount++;
    }

    if(fluidSimMutex.try_lock())
    {
      //double lastTimeSim = glfwGetTime();
      helloVk.updateParticlesInstances(*particlesPos);
      //double lastTimeFPS = glfwGetTime();
      //std::cout << "-----FINISHED UPDATING PARTICLES POSITIONS IN: " << lastTimeFPS - lastTimeSim << " seconds-----" << std::endl;
      if(resumeSimulation) helloVk.resetFrame();
      fluidSimMutex.unlock();
      
    }

    // Offscreen render pass
    {
      VkRenderPassBeginInfo offscreenRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      offscreenRenderPassBeginInfo.clearValueCount = 2;
      offscreenRenderPassBeginInfo.pClearValues    = clearValues.data();
      offscreenRenderPassBeginInfo.renderPass      = helloVk.m_offscreenRenderPass;
      offscreenRenderPassBeginInfo.framebuffer     = helloVk.m_offscreenFramebuffer;
      offscreenRenderPassBeginInfo.renderArea      = {{0, 0}, helloVk.getSize()};

      if(/* fluidSimMutex.try_lock() !isCalculatingPhysics*/ true)
      {
        //std::cout << "ENTERING RENDER BUCLE\n";
        double lastTimeSimVk = glfwGetTime();  // first time for deltaTime calculation
        // Rendering Scene
        if(useRaytracer)
        {
          //std::thread raytraceThread([&] {
          helloVk.raytrace(cmdBuf, clearColor /*, &fluidSimMutex*/);
          //simulationThreadRunning = false;  // Indicar que el hilo de simulacion ha terminado
          //});
          //raytraceThread.detach();
        }
        else
        {
          vkCmdBeginRenderPass(cmdBuf, &offscreenRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
          helloVk.rasterize(cmdBuf);
          vkCmdEndRenderPass(cmdBuf);
        }

        //fluidSimMutex.unlock();

        double lastTimeFPSVk = glfwGetTime();
      }
      else
        std::cout << "PHYSICS CALCULATING, NO RENDER...\n";
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

  if(fluidSim.cudaMode_ == "physics" || fluidSim.cudaMode_ == "full")
  {
    fluidSim.gpuCudaFreeMem();
  }

  helloVk.destroyResources();
  helloVk.destroy();
  vkctx.deinit();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
