#include "stdafx.h"
#include "VKSampleHelper.hpp"

void GetDeviceQueue(const VkDevice& device, unsigned int graphicsQueueFamilyIndex, VkQueue& graphicsQueue)
{
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
}

bool CheckPhysicalDeviceProperties(
    const VkPhysicalDevice& physicalDevice, 
    VulkanCommonParameters& vulkan_param)
{
    // Get list of supported device extensions
    uint32_t extCount = 0;
    std::vector<std::string> extensionNames;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            for (const VkExtensionProperties& ext : extensions)
            {
                extensionNames.push_back(ext.extensionName);
            }
        }
    }

    std::vector<const char*> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Check that the device extensions we want to enable are supported
    if (deviceExtensions.size() > 0)
    {
        for (const char* deviceExt : deviceExtensions)
        {
            // Output message if requested extension is not available
            if (std::find(extensionNames.begin(), extensionNames.end(), deviceExt) == extensionNames.end())
            {
                printf("Device extension not present!\n");
                assert(0);
            }
        }
    }

    // Get device queue family properties
    unsigned int queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0)
    {
        printf("Physical device doesn't have any queue families!\n");
        assert(0);
    }

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    std::vector<VkBool32> queuePresentSupport(queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    // for each queue family...
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {

        // Query if presentation is supported on a specific surface
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, vulkan_param.PresentationSurface, &queuePresentSupport[i]);

        if ((queueFamilyProperties[i].queueCount > 0) && (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            // If the queue family supports both graphics operations and presentation on our surface - prefer it
            if (queuePresentSupport[i])
            {
                vulkan_param.GraphicsQueue.FamilyIndex = i;
                return true;
            }
        }
    }

    return false;
}

std::string errorString(VkResult errorCode)
{
    switch (errorCode)
    {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}