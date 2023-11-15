#pragma once

struct QueueParameters {
    VkQueue                       Handle;
    uint32_t                      FamilyIndex;

    QueueParameters() :
        Handle(VK_NULL_HANDLE),
        FamilyIndex(UINT32_MAX) {
    }
};

struct ImageParameters {
    VkImage                       Handle;
    VkDeviceMemory                Memory;
    VkImageView                   View;
    void*                         MappedMemory;
    uint32_t                      Size;
    VkFormat                      Format;
    VkDescriptorImageInfo         Descriptor;

    ImageParameters() :
        Handle(VK_NULL_HANDLE),
        Memory(VK_NULL_HANDLE),
        View(VK_NULL_HANDLE),
        MappedMemory(nullptr),
        Descriptor(),
        Size(0) {
    }
};

struct BufferParameters {
    VkBuffer                      Handle;
    VkDeviceMemory                Memory;
    void*                         MappedMemory;
    size_t                        Size;
    VkDescriptorBufferInfo        Descriptor;

    BufferParameters() :
        Handle(VK_NULL_HANDLE),
        Memory(VK_NULL_HANDLE),
        MappedMemory(nullptr),
        Descriptor(),
        Size(0) {
    }
};

struct SwapChainParameters {
    VkSwapchainKHR                Handle;
    VkFormat                      Format;
    VkColorSpaceKHR               ColorSpace;
    std::vector<ImageParameters>  Images;
    VkExtent2D                    Extent;

    SwapChainParameters() :
        Handle(VK_NULL_HANDLE),
        Format(VK_FORMAT_UNDEFINED),
        ColorSpace(VK_COLORSPACE_SRGB_NONLINEAR_KHR),
        Images(),
        Extent() {
    }
};

struct VulkanCommonParameters {
    VkInstance                    Instance;
    std::vector<const char*>      InstanceExtensions;
    VkPhysicalDevice              PhysicalDevice;
    VkDevice                      Device;
    std::vector<const char*>      DeviceExtensions;
    VkPhysicalDeviceFeatures      EnabledFeatures;
    void*                         ExtFeatures;
    QueueParameters               GraphicsQueue;
    QueueParameters               ComputeQueue;
    QueueParameters               TransferQueue;
    VkSurfaceKHR                  PresentationSurface;
    SwapChainParameters           SwapChain;
    ImageParameters               DepthStencilImage;

    VulkanCommonParameters() :
        Instance(VK_NULL_HANDLE),
        InstanceExtensions{VK_KHR_SURFACE_EXTENSION_NAME}, // Add a generic surface extension, which specifies we want to render on the screen
        PhysicalDevice(VK_NULL_HANDLE),
        Device(VK_NULL_HANDLE),
        DeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME}, // Add swapchain extension
        EnabledFeatures(),
        ExtFeatures(),
        GraphicsQueue(),
        ComputeQueue(),
        TransferQueue(),
        PresentationSurface(VK_NULL_HANDLE),
        SwapChain() {
    }
};

struct FrameResources {
    std::vector<VkCommandBuffer>                        CommandBuffers;
    std::vector<BufferParameters>                       HostVisibleBuffers;
    std::vector<BufferParameters>                       HostVisibleDynamicBuffers;
    std::map<std::string, std::vector<VkDescriptorSet>> DescriptorSets;
    std::vector<VkSemaphore>                            ImageAvailableSemaphores;
    std::vector<VkSemaphore>                            RenderingCompleteSemaphores;
    std::map<std::string, std::vector<VkSemaphore>>     Semaphores;
    std::vector<VkFence>                                Fences;

    FrameResources() :
        CommandBuffers(),
        DescriptorSets(),
        HostVisibleBuffers(),
        HostVisibleDynamicBuffers(),
        ImageAvailableSemaphores(),
        RenderingCompleteSemaphores(),
        Fences() {
    }
};

struct SampleParameters {
    VkRenderPass                         RenderPass;
    std::vector<VkFramebuffer>           Framebuffers;
    std::map<std::string, VkPipeline>    Pipelines;
    VkDescriptorPool                     DescriptorPool;
    VkDescriptorSetLayout                DescriptorSetLayout;
    VkPipelineLayout                     PipelineLayout;
    VkCommandPool                        CommandPool;
    FrameResources                       FrameRes;


    SampleParameters() :
        RenderPass(VK_NULL_HANDLE),
        Framebuffers(),
        CommandPool(VK_NULL_HANDLE),
        PipelineLayout(VK_NULL_HANDLE),
        Pipelines(),
        DescriptorPool(VK_NULL_HANDLE),
        DescriptorSetLayout(VK_NULL_HANDLE),
        FrameRes() {
    }
};

#define VK_CHECK_RESULT(f)																			                          \
{																										                      \
    VkResult res = (f);																					                      \
    if (res != VK_SUCCESS)																				                      \
    {																									                      \
        std::cout << "Fatal : VkResult is \"" << errorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
        assert(res == VK_SUCCESS);																		                      \
    }																									                      \
}

void GetDeviceQueue(
    const VkDevice& device, 
    unsigned int queueFamilyIndex, 
    VkQueue& queue);

bool CheckPhysicalDeviceProperties(
    const VkPhysicalDevice& physicalDevice, 
    VkQueueFlags requestedQueueTypes,
    VulkanCommonParameters& param);

uint32_t GetMemoryTypeIndex(
    uint32_t typeBits, 
    VkMemoryPropertyFlags properties, 
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties);

void TransitionImageLayout(VkCommandBuffer cmd, 
                            VkImage image, VkImageAspectFlags aspectMask, 
                            VkImageLayout oldImageLayout, VkImageLayout newImageLayout, 
                            VkAccessFlagBits srcAccessMask, VkPipelineStageFlags srcStages, 
                            VkPipelineStageFlags dstStages);

void SetBufferMemoryBarrier(VkCommandBuffer cmd, 
                            VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset, 
                            VkAccessFlagBits srcAccessMask, VkPipelineStageFlags srcStages, 
                            VkAccessFlagBits dstAccessMask, VkPipelineStageFlags dstStages);

void CreateBuffer(VkDevice device, 
                    VkBufferCreateInfo bufferInfo, 
                    BufferParameters& bufParams, 
                    VkMemoryPropertyFlags memFlags,
                    VkPhysicalDeviceMemoryProperties devMemProps);

void CreateBuffer(VkDevice device, 
                    VkBufferCreateInfo bufferInfo, 
                    VkBuffer& buffer,
                    VkDeviceMemory& memory,
                    void *mappedMemory,
                    VkMemoryPropertyFlags memFlags,
                    VkPhysicalDeviceMemoryProperties devMemProps);

VkShaderModule LoadSPIRVShaderModule(VkDevice device, std::string filename);

void FlushInitCommandBuffer(VkDevice device, VkQueue queue, VkCommandBuffer cmd, VkFence fence);

void* AlignedAlloc(size_t size, size_t alignment);
void AlignedFree(void* ptr);

std::string errorString(VkResult errorCode);