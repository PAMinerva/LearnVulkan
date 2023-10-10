#pragma once

#include "VKSampleHelper.hpp"
#include "StepTimer.hpp"

// Max number of frames to queue
#define MAX_FRAME_LAG 2

class VKSample
{
public:
    VKSample(uint32_t width, uint32_t height, std::string name);
    virtual ~VKSample();

    virtual void OnInit() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnRender() = 0;
    virtual void OnDestroy() = 0;
    virtual void OnResize() = 0;

    virtual void WindowResize(uint32_t width, uint32_t height);

    // Samples override the event handlers to handle specific messages.
    virtual void OnKeyDown(uint8_t /*key*/) {}
    virtual void OnKeyUp(uint8_t /*key*/) {}

    // Accessors.
    uint32_t GetWidth() const { return m_width; }
    void SetWidth(uint32_t width) { m_width = width; }
    uint32_t GetHeight() const { return m_height; }
    void SetHeight(uint32_t height) { m_height = height; }
    const char* GetTitle() const { return m_title.c_str(); }
    const std::string GetStringTitle() const { return m_title; }
    const std::string GetAssetsPath() const { return m_assetsPath; };
    const std::string GetWindowTitle();
    void SetAssetsPath(std::string assetPath) { m_assetsPath = assetPath; }
    uint64_t GetFrameCounter() const{ return m_frameCounter; }
    bool IsInitialized() const { return m_initialized; }

protected:
    virtual void CreateInstance();
    virtual void CreateSurface();
    virtual void CreateSynchronizationObjects();
    virtual void CreateDevice(VkQueueFlags requestedQueueTypes);
    virtual void CreateSwapchain(uint32_t* width, uint32_t* height, bool vsync);
    virtual void CreateDepthStencilImage(uint32_t width, uint32_t height);
    virtual void CreateRenderPass();
    virtual void CreateFrameBuffers();
    virtual void AllocateCommandBuffers();
    
    virtual void EnableInstanceExtensions(std::vector<const char*>& instanceExtensions);
    virtual void EnableDeviceExtensions(std::vector<const char*>& deviceExtensions);
    virtual void EnableFeatures(VkPhysicalDeviceFeatures& features);

    // Viewport dimensions.
    uint32_t m_width;
    uint32_t m_height;
    float m_aspectRatio;

    // Sentinel variable to check sample initialization completion.
    bool m_initialized;

    // Vulkan and objects.
    VulkanCommonParameters  m_vulkanParams;
    SampleParameters m_sampleParams;

    // Stores physical device properties (for e.g. checking device limits)
    VkPhysicalDeviceProperties m_deviceProperties;
    // Stores all available memory (type) properties for the physical device
    VkPhysicalDeviceMemoryProperties m_deviceMemoryProperties;
    // Stores the features available on the selected physical device (for e.g. checking if a feature is available)
    VkPhysicalDeviceFeatures m_deviceFeatures;

    // Frame count
    StepTimer m_timer;
    uint64_t m_frameCounter;
    char m_lastFPS[32];

    // Index of the current frame
    uint32_t m_frameIndex = 0;

private:
    // Root assets path.
    std::string m_assetsPath;

    // Window title.
    std::string m_title;
};