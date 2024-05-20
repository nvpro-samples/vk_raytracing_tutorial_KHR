#include "VulkanRender.h"

extern std::vector<std::string> defaultSearchPaths;

int VulkanRender::setupWindow(int width, int height)
{
    // Setup GLFW window
    glfwSetErrorCallback(onErrorCallback);
    if(!glfwInit())
    {
    return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    this->window = glfwCreateWindow(width, height, PROJECT_NAME, nullptr, nullptr);


    // Setup camera
    CameraManip.setWindowSize(width, height);
    CameraManip.setLookat(glm::vec3(4, 4, 4), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));

    return 0;
}

int VulkanRender::setupVulkan()
{
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

    // #VKRay: Activate the ray tracing extension
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    this->contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    this->contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
    this->contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline

    // Vulkan version
    this->contextInfo.setVersion(1, 2);                 // Using Vulkan 1.2
    for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
    this->contextInfo.addInstanceExtension(reqExtensions[ext_id]);
    this->contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);        // FPS in titlebar
    this->contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
    this->contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);  // Enabling ability to present rendering

    return 0;
}

int VulkanRender::setupContext()
{
    this->vkctx.initInstance(contextInfo);
    // Find all compatible devices
    auto compatibleDevices = this->vkctx.getCompatibleDevices(contextInfo);
    assert(!compatibleDevices.empty());
    // Use a compatible device
    this->vkctx.initDevice(compatibleDevices[0], contextInfo);

    return 0;
}