#pragma once

#include "nvvkhl/appbase_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/context_vk.hpp"
#include "shaders/host_device.h"
#include "nvpsystem.hpp"

//Include the helper that act as a container for one TLAS referrencing the array of BLAS
#include "nvvk/raytraceKHR_vk.hpp"

// GLFW Callback functions
static void onErrorCallback(int error, const char* description)
{
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

class VulkanRender : public nvvkhl::AppBaseVk
{
public:
	GLFWwindow* window;
	// Requesting Vulkan extensions and layers
	nvvk::ContextCreateInfo contextInfo;
	// Creating Vulkan base application
	nvvk::Context vkctx{};

	// Structuring functions
	int setupWindow(int width, int height);
	int setupVulkan();
	int setupContext();
};