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
    VkPhysicalDevice              PhysicalDevice;
    VkDevice                      Device;
    QueueParameters               GraphicsQueue;
    QueueParameters               ComputeQueue;
    QueueParameters               TransferQueue;
    VkSurfaceKHR                  PresentationSurface;
    SwapChainParameters           SwapChain;
    ImageParameters               DepthStencilImage;

    VulkanCommonParameters() :
        Instance(VK_NULL_HANDLE),
        PhysicalDevice(VK_NULL_HANDLE),
        Device(VK_NULL_HANDLE),
        GraphicsQueue(),
        ComputeQueue(),
        TransferQueue(),
        PresentationSurface(VK_NULL_HANDLE),
        SwapChain() {
    }
};

struct FrameResources {
    std::vector<VkCommandBuffer>         GraphicsCommandBuffers;
    std::vector<BufferParameters>        HostVisibleBuffers;
    std::vector<BufferParameters>        HostVisibleDynamicBuffers;
    std::vector<VkDescriptorSet>         DescriptorSets;
    std::vector<VkSemaphore>             ImageAvailableSemaphores;
    std::vector<VkSemaphore>             RenderingFinishedSemaphores;
    std::vector<VkFence>                 Fences;

    FrameResources() :
        GraphicsCommandBuffers(),
        DescriptorSets(),
        HostVisibleBuffers(),
        HostVisibleDynamicBuffers(),
        ImageAvailableSemaphores(),
        RenderingFinishedSemaphores(),
        Fences() {
    }
};

struct SampleParameters {
    VkRenderPass                         RenderPass;
    std::vector<VkFramebuffer>           Framebuffers;
    VkPipeline                           GraphicsPipeline;
    VkDescriptorPool                     DescriptorPool;
    VkDescriptorSetLayout                DescriptorSetLayout;
    VkPipelineLayout                     PipelineLayout;
    VkCommandPool                        GraphicsCommandPool;
    FrameResources                       FrameRes;


    SampleParameters() :
        RenderPass(VK_NULL_HANDLE),
        Framebuffers(),
        GraphicsCommandPool(VK_NULL_HANDLE),
        PipelineLayout(VK_NULL_HANDLE),
        GraphicsPipeline(VK_NULL_HANDLE),
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
    unsigned int graphicsQueueFamilyIndex, 
    VkQueue& graphicsQueue);

bool CheckPhysicalDeviceProperties(
    const VkPhysicalDevice& physicalDevice, 
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

void CreateBuffer(VkDevice device, 
                    VkBufferCreateInfo bufferInfo, 
                    BufferParameters& bufParams, 
                    VkMemoryPropertyFlags memFlags,
                    VkPhysicalDeviceMemoryProperties devMemProps);

VkShaderModule LoadSPIRVShaderModule(VkDevice device, std::string filename);

void FlushInitCommandBuffer(VkDevice device, VkQueue queue, VkCommandBuffer cmd, VkFence fence);

void* AlignedAlloc(size_t size, size_t alignment);
void AlignedFree(void* ptr);

std::string errorString(VkResult errorCode);