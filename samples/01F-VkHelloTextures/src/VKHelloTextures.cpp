#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKHelloTextures.hpp"
#include "VKDebug.h"

VKHelloTextures::VKHelloTextures(uint32_t width, uint32_t height, std::string name) :
VKSample(width, height, name)
{
}

void VKHelloTextures::OnInit()
{
    InitVulkan();
    SetupPipeline();
}

void VKHelloTextures::InitVulkan()
{
    CreateInstance();
    CreateSurface();
    CreateDevice(VK_QUEUE_GRAPHICS_BIT);
    GetDeviceQueue(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.FamilyIndex, m_vulkanParams.GraphicsQueue.Handle);
    CreateSwapchain(&m_width, &m_height, VKApplication::settings.vsync);
    CreateRenderPass();
    CreateFrameBuffers();
    AllocateCommandBuffers();
    CreateSynchronizationObjects();
}

void VKHelloTextures::SetupPipeline()
{
    CreateVertexBuffer();
    CreateHostVisibleBuffers();
    CreateStagingBuffer();
    CreateTexture();
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    AllocateDescriptorSets();
    CreatePipelineLayout();
    CreatePipelineObjects();

    m_initialized = true;
}

// Update frame-based values.
void VKHelloTextures::OnUpdate()
{
    m_timer.Tick(nullptr);
    
    // Update FPS and frame count.
    snprintf(m_lastFPS, (size_t)32, "%u fps", m_timer.GetFramesPerSecond());
    m_frameCounter++;

    // Update buffer data.
    UpdateHostVisibleBufferData();
}

// Render the scene.
void VKHelloTextures::OnRender()
{
    // Ensure no more than MAX_FRAME_LAG frames are queued.
    VK_CHECK_RESULT(vkWaitForFences(m_vulkanParams.Device, 1, &m_sampleParams.FrameRes.Fences[m_frameIndex], VK_TRUE, UINT64_MAX));
    VK_CHECK_RESULT(vkResetFences(m_vulkanParams.Device, 1, &m_sampleParams.FrameRes.Fences[m_frameIndex]));

    // Get the index of the next available image in the swap chain
    uint32_t imageIndex;
    VkResult acquire = vkAcquireNextImageKHR(m_vulkanParams.Device, 
                                             m_vulkanParams.SwapChain.Handle, 
                                             UINT64_MAX, 
                                             m_sampleParams.FrameRes.ImageAvailableSemaphores[m_frameIndex], 
                                             nullptr, &imageIndex);
    if (!((acquire == VK_SUCCESS) || (acquire == VK_SUBOPTIMAL_KHR)))
    {
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
            WindowResize(m_width, m_height);
        else
            VK_CHECK_RESULT(acquire);
    }

    PopulateCommandBuffer(imageIndex);

    SubmitCommandBuffer();

    PresentImage(imageIndex);

    // Update command buffer index
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAME_LAG;
}

void VKHelloTextures::OnDestroy()
{
    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Destroy vertex buffer object and deallocate backing memory
    vkDestroyBuffer(m_vulkanParams.Device, m_vertices.buffer, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertices.memory, nullptr);

    // Destroy texture resources
    vkDestroyBuffer(m_vulkanParams.Device, m_texture.StagingBuffer.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_texture.StagingBuffer.Memory, nullptr);
    vkDestroyImageView(m_vulkanParams.Device, m_texture.TextureImage.Descriptor.imageView, nullptr);
    vkDestroyImage(m_vulkanParams.Device, m_texture.TextureImage.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_texture.TextureImage.Memory, nullptr);
    vkDestroySampler(m_vulkanParams.Device, m_texture.TextureImage.Descriptor.sampler, nullptr);

    // Destroy\Unmap frame resources
    for (uint32_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Unmap host-visible device memory
        vkUnmapMemory(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory);

        // Destroy buffer object and deallocate backing memory
        vkDestroyBuffer(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle, nullptr);
        vkFreeMemory(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory, nullptr);

        // Wait for fence before destroying it
        vkWaitForFences(m_vulkanParams.Device, 1, &m_sampleParams.FrameRes.Fences[i], VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_vulkanParams.Device, m_sampleParams.FrameRes.Fences[i], NULL);

        // Destroy semaphores
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.ImageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.RenderingFinishedSemaphores[i], NULL);
    }

    // Destroy descriptor pool
    vkDestroyDescriptorPool(m_vulkanParams.Device, m_sampleParams.DescriptorPool, nullptr);

    // Destroy descriptor set layout
    vkDestroyDescriptorSetLayout(m_vulkanParams.Device, m_sampleParams.DescriptorSetLayout, nullptr);

    // Destroy pipeline and pipeline layout objects
    vkDestroyPipelineLayout(m_vulkanParams.Device, m_sampleParams.PipelineLayout, nullptr);
    vkDestroyPipeline(m_vulkanParams.Device, m_sampleParams.GraphicsPipeline, nullptr);

    // Destroy frame buffers
    for (uint32_t i = 0; i < m_sampleParams.Framebuffers.size(); i++) {
        vkDestroyFramebuffer(m_vulkanParams.Device, m_sampleParams.Framebuffers[i], nullptr);
    }

    // Destroy swapchain and its images
    for (uint32_t i = 0; i < m_vulkanParams.SwapChain.Images.size(); i++)
    {
        vkDestroyImageView(m_vulkanParams.Device, m_vulkanParams.SwapChain.Images[i].View, nullptr);
    }
    vkDestroySwapchainKHR(m_vulkanParams.Device, m_vulkanParams.SwapChain.Handle, nullptr);

    // Free allocated command buffers
    vkFreeCommandBuffers(m_vulkanParams.Device, 
                         m_sampleParams.GraphicsCommandPool,
                          static_cast<uint32_t>(m_sampleParams.FrameRes.GraphicsCommandBuffers.size()), 
                          m_sampleParams.FrameRes.GraphicsCommandBuffers.data());

    vkDestroyRenderPass(m_vulkanParams.Device, m_sampleParams.RenderPass, NULL);

    // Destroy command pool
    vkDestroyCommandPool(m_vulkanParams.Device, m_sampleParams.GraphicsCommandPool, NULL);

    // Destroy device
    vkDestroyDevice(m_vulkanParams.Device, NULL);

    // Destroy surface
    vkDestroySurfaceKHR(m_vulkanParams.Instance, m_vulkanParams.PresentationSurface, NULL);

    // Destroy debug messanger
    if ((VKApplication::settings.validation)) 
    {
        pfnDestroyDebugUtilsMessengerEXT(m_vulkanParams.Instance, debugUtilsMessenger, NULL);
    }

#if defined(VK_USE_PLATFORM_XLIB_KHR)
    XDestroyWindow(VKApplication::winParams.DisplayPtr, VKApplication::winParams.Handle);
    XCloseDisplay(VKApplication::winParams.DisplayPtr);
#endif

    // Destroy Vulkan instance
    vkDestroyInstance(m_vulkanParams.Instance, NULL);
}

// Create a vertex buffer that describe a single triangle
void VKHelloTextures::CreateVertexBuffer()
{
    // While it's fine for an example application to request small individual memory allocations, that is not
    // what should be done a real-world application, where you should allocate large chunks of memory at once instead.
    
    // Setup vertices in the Z = 0 plane, and in counterclockwise order
    //
    //       Y
    //      |
    //      v0
    //     /|\
    //    / |_\_________ X
    //   /     \
    //  v1 ---- v2
    //
    // Triangle in texture space
    // 
    //   Y    v0
    //   |   /  \
    //   |  /    \
    //   | /      \
    //   |v1_______\v2_ X
    //
    std::vector<Vertex> vertexBuffer =
    {
        { { 0.0f, 0.25f * m_aspectRatio, 0.0f },    { 0.5f, 1.0f } },  // v0
        { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f } },  // v1
        { { 0.25f, -0.25f * m_aspectRatio, 0.0f },  { 1.0f, 0.0f } }   // v2
    };
    uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);

    //
    // Create the vertex buffer in host-visible device memory. 
    // This is not recommended as it can often result in lower rendering performance
    //
    
    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;
    
    // Pointer used to map host-visible device memory to the virtual address space of the application.
    // The application can copy data to host-visible device memory only using this pointer.
    void *data;    
    
    // Create the vertex buffer object
    VkBufferCreateInfo vertexBufferInfo = {};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexBufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;    
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &vertexBufferInfo, nullptr, &m_vertices.buffer));

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the vertex buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_vertices.buffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_vertices.memory));

    // Map the host-visible device memory and copy the vertex data. 
    // Once finished, we can unmap it since we no longer need to access the vertex buffer from the application.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, m_vertices.memory, 0, memAlloc.allocationSize, 0, &data));
    memcpy(data, vertexBuffer.data(), vertexBufferSize);
    vkUnmapMemory(m_vulkanParams.Device, m_vertices.memory);

    // Bind the vertex buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, m_vertices.buffer, m_vertices.memory, 0));
}

void VKHelloTextures::CreateHostVisibleBuffers()
{
    //
    // Create buffers in host-visible device memory
    // since they needs to be updated from the CPU on a per-frame basis.
    //
    
    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;
    
    // Create the buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(uBufVS);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    m_sampleParams.FrameRes.HostVisibleBuffers.resize(MAX_FRAME_LAG);
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &bufferInfo, nullptr, &m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle));

        // Request a memory allocation from coherent, host-visible device memory that is large 
        // enough to hold the buffer.
        // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
        // will be directly visible to the device without requiring the explicit flushing of cached memory.
        vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
        VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory));

        // Map the host-visible device memory just allocated.
        // Leave it mapped so that we don't have to map and unmap it every time we want to update the buffer data.
        VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, 
                                    m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory, 
                                    0, memAlloc.allocationSize, 
                                    0, &m_sampleParams.FrameRes.HostVisibleBuffers[i].MappedMemory));

        // Bind the buffer object to the backing host-visible device memory just allocated.
        VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, 
                                           m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle, 
                                           m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory, 0));

        // Store information needed to write\update the corresponding descriptor (uniform buffer) in the descriptor set later.
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.buffer = m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle;
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.offset = 0;
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.range = sizeof(uBufVS);
    }
}

void VKHelloTextures::CreateStagingBuffer()
{
    // Create a buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_texture.TextureWidth * m_texture.TextureHeight * m_texture.TextureTexelSize,
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &bufferInfo, nullptr, &m_texture.StagingBuffer.Handle));

    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the staging buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_texture.StagingBuffer.Handle, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_texture.StagingBuffer.Memory));

    // Bind the buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, 
                                        m_texture.StagingBuffer.Handle, 
                                        m_texture.StagingBuffer.Memory, 0));

    // Map the host-visible device memory just allocated.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, 
                                m_texture.StagingBuffer.Memory, 
                                0, memAlloc.allocationSize, 
                                0, &m_texture.StagingBuffer.MappedMemory));

    // Copy texture data in the staging buffer
    std::vector<uint8_t> texData = GenerateTextureData();
    memcpy(m_texture.StagingBuffer.MappedMemory, texData.data(), texData.size());

    // Unmap staging buffer
    vkUnmapMemory(m_vulkanParams.Device, m_texture.StagingBuffer.Memory);
}

std::vector<uint8_t> VKHelloTextures::GenerateTextureData()
{
    const size_t rowPitch = m_texture.TextureWidth * m_texture.TextureTexelSize;
    const size_t cellPitch = rowPitch >> 3;                        // The width of a cell in the checkerboard texture.
    const size_t cellHeight = m_texture.TextureWidth >> 3;         // The height of a cell in the checkerboard texture.
    const size_t textureSize = rowPitch * m_texture.TextureHeight;
 
    std::vector<uint8_t> data(textureSize);
    uint8_t* pData = &data[0];
 
    for (size_t n = 0; n < textureSize; n += m_texture.TextureTexelSize)
    {
        size_t x = n % rowPitch;
        size_t y = n / rowPitch;
        size_t i = x / cellPitch;
        size_t j = y / cellHeight;
 
        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;        // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }
 
    return data;
}

void VKHelloTextures::CreateTexture()
{
    const VkFormat tex_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties props;

    vkGetPhysicalDeviceFormatProperties(m_vulkanParams.PhysicalDevice, tex_format, &props);

    // Check if the device can sample from R8G8B8A8 textures in local device memory
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
    {
        // Create a texture image
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = tex_format;
        imageCreateInfo.extent = {m_texture.TextureWidth, m_texture.TextureHeight, 1};
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateImage(m_vulkanParams.Device, &imageCreateInfo, nullptr, &m_texture.TextureImage.Handle));

        // Used to request an allocation of a specific size from a certain memory type.
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Request a memory allocation from local device memory that is large 
        // enough to hold the texture image.
        vkGetImageMemoryRequirements(m_vulkanParams.Device, m_texture.TextureImage.Handle, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_deviceMemoryProperties);
        VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_texture.TextureImage.Memory));

        // Bind the image object to the backing local device memory just allocated.
        VK_CHECK_RESULT(vkBindImageMemory(m_vulkanParams.Device, 
                                            m_texture.TextureImage.Handle, 
                                            m_texture.TextureImage.Memory, 0));

        // We need to record some commands to copy staging buffer data to texture image in local device memory
        VkCommandBufferBeginInfo cmdBufferInfo = {};
        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[0], &cmdBufferInfo);

        // Transition the image layout to provide optimal performance for transfering operations that use the image as a destination
        TransitionImageLayout(m_sampleParams.FrameRes.GraphicsCommandBuffers[0],
                                m_texture.TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_ACCESS_NONE, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                                VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy from staging buffer to texture in local device memory
        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {m_texture.TextureWidth, m_texture.TextureHeight, 1};

        vkCmdCopyBufferToImage(m_sampleParams.FrameRes.GraphicsCommandBuffers[0], 
                                m_texture.StagingBuffer.Handle, m_texture.TextureImage.Handle, 
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition the image layout to provide optimal performance for reading by shaders
        TransitionImageLayout(m_sampleParams.FrameRes.GraphicsCommandBuffers[0],
                        m_texture.TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        // Save the last image layout
        m_texture.TextureImage.Descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        //
        // Create a sampler and a view
        //

        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_texture.TextureImage.Handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = tex_format;
        viewInfo.components =
            {
                VK_COMPONENT_SWIZZLE_IDENTITY,  // R
                VK_COMPONENT_SWIZZLE_IDENTITY,  // G
                VK_COMPONENT_SWIZZLE_IDENTITY,  // B
                VK_COMPONENT_SWIZZLE_IDENTITY,  // A
            };
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; // The texture stores colors and includes one mipmap level (index 0) and one array layer (index 0)

        // Create a sampler
        VK_CHECK_RESULT(vkCreateSampler(m_vulkanParams.Device, &samplerInfo, NULL, &m_texture.TextureImage.Descriptor.sampler));

        // Create an image view
        VK_CHECK_RESULT(vkCreateImageView(m_vulkanParams.Device, &viewInfo, NULL, &m_texture.TextureImage.Descriptor.imageView));
    }
    else {
        /* Can't support VK_FORMAT_R8G8B8A8_UNORM !? */
        assert(!"No support for R8G8B8A8_UNORM as texture image format");
    }

    // Flush the command buffer
    FlushInitCommandBuffer(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.Handle, m_sampleParams.FrameRes.GraphicsCommandBuffers[0], m_sampleParams.FrameRes.Fences[0]);
}

void VKHelloTextures::UpdateHostVisibleBufferData()
{
    const float translationSpeed = 0.8f;    // speed
    const float offsetBounds = 0.75f;       // bound the displacement within the range [-1, +1]; see CreateVertexBuffer
    static float direction = 1.0f;          // direction
 
    // From speed = (displacement / time) you can derive that: displacement = (speed * time)
    uBufVS.displacement[0] += direction * translationSpeed * m_timer.GetElapsedSeconds();
    if (uBufVS.displacement[0] > offsetBounds)
    {
        uBufVS.displacement[0] = 0.75f;
        direction *= -1.0f;
    }
    else if (uBufVS.displacement[0] < -offsetBounds)
    {
        uBufVS.displacement[0] = -0.75f;
        direction *= -1.0f;
    }

    // Update uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    memcpy(m_sampleParams.FrameRes.HostVisibleBuffers[m_frameIndex].MappedMemory, &uBufVS, sizeof(uBufVS));
}

void VKHelloTextures::CreateDescriptorPool()
{
    //
    // To calculate the amount of memory required for a descriptor pool, the implementation needs to know
    // the max numbers of descriptor sets we will request from the pool, and the number of descriptors 
    // per type we will include in those descriptor sets.
    //

    // Describe the number of descriptors per type.
    // This sample uses two descriptor types (uniform buffer and combined image sampler)
    VkDescriptorPoolSize typeCounts[2];
    typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    typeCounts[0].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    typeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    typeCounts[1].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);;

    // Create a global descriptor pool
    // All descriptors set used in this sample will be allocated from this pool
    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.pNext = nullptr;
    descriptorPoolInfo.poolSizeCount = 2;
    descriptorPoolInfo.pPoolSizes = typeCounts;
    // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
    descriptorPoolInfo.maxSets = static_cast<uint32_t>(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkCreateDescriptorPool(m_vulkanParams.Device, &descriptorPoolInfo, nullptr, &m_sampleParams.DescriptorPool));
}

void VKHelloTextures::CreateDescriptorSetLayout()
{
    //
    // Create a Descriptor Set Layout to connect binding points (resource declarations)
    // in the shader code to descriptors within descriptor sets.
    //
    // Binding 0: Uniform buffer (Vertex shader)
    VkDescriptorSetLayoutBinding layoutBinding[2] = {};
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // Binding 1: Combined image sampler (Fragment shader)
    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = 2;
    descriptorLayout.pBindings = layoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vulkanParams.Device, &descriptorLayout, nullptr, &m_sampleParams.DescriptorSetLayout));
}

void VKHelloTextures::AllocateDescriptorSets()
{
    // Allocate MAX_FRAME_LAG descriptor sets from the global descriptor pool.
    // Use the descriptor set layout to calculate the amount on memory required to store the descriptor sets.
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_sampleParams.DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    std::vector<VkDescriptorSetLayout> DescriptorSetLayouts(MAX_FRAME_LAG, m_sampleParams.DescriptorSetLayout);
    allocInfo.pSetLayouts = DescriptorSetLayouts.data();

    m_sampleParams.FrameRes.DescriptorSets.resize(MAX_FRAME_LAG);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vulkanParams.Device, &allocInfo, m_sampleParams.FrameRes.DescriptorSets.data()));

    //
    // Write the descriptors updating the corresponding descriptor sets.
    // For every binding point used in a shader code there needs to be at least a descriptor 
    // in a descriptor set matching that binding point.
    //
    VkWriteDescriptorSet writeDescriptorSet[2] = {};

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Write the descriptor of the uniform buffer.
        // We need to pass the descriptor set where it is store and 
        // the binding point associated with descriptor in the descriptor set.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleParams.FrameRes.DescriptorSets[i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet[0].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the combined image sampler.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleParams.FrameRes.DescriptorSets[i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet[1].pImageInfo = &m_texture.TextureImage.Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 2, writeDescriptorSet, 0, nullptr);
    }
}

void VKHelloTextures::CreatePipelineLayout()
{
    // Create a pipeline layout that will be used to create one or more pipeline objects.
    // In this case we have a pipeline layout with a single descriptor set layout.
    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
    pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineLayoutCreateInfo.pNext = nullptr;
    pPipelineLayoutCreateInfo.setLayoutCount = 1;
    pPipelineLayoutCreateInfo.pSetLayouts = &m_sampleParams.DescriptorSetLayout;
    
    VK_CHECK_RESULT(vkCreatePipelineLayout(m_vulkanParams.Device, &pPipelineLayoutCreateInfo, nullptr, &m_sampleParams.PipelineLayout));
}

void VKHelloTextures::CreatePipelineObjects()
{
    //
    // Construct the different states making up the only graphics pipeline needed by this sample
    //

    //
    // Input assembler state
    //    
    // Vertex binding descriptions describe the input assembler binding points where vertex buffers will be bound.
    // This sample uses a single vertex buffer at binding point 0 (see vkCmdBindVertexBuffers).
    VkVertexInputBindingDescription vertexInputBinding = {};
    vertexInputBinding.binding = 0;
    vertexInputBinding.stride = sizeof(Vertex);
    vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    // Vertex attribute descriptions describe the vertex shader attribute locations and memory layouts, 
    // as well as the binding points from which the input assembler should retrieve data to pass to the 
    // corresponding vertex shader input attributes.
    std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs;
    // These match the following shader layout (see triangle.vert):
    //	layout (location = 0) in vec3 inPos;
    //	layout (location = 1) in vec2 inTexCoord;
    //
    // Attribute location 0: Position from vertex buffer at binding point 0
    vertexInputAttributs[0].binding = 0;
    vertexInputAttributs[0].location = 0;
    // Position attribute is three 32-bit signed (SFLOAT) floats (R32 G32 B32)
    vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[0].offset = offsetof(Vertex, position);
    // Attribute location 1: TexCoord from vertex buffer at binding point 0
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    // TexCoord attribute is two 32-bit signed (SFLOAT) floats (R32 G32)
    vertexInputAttributs[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, texCoord);
    
    // Vertex input state used for pipeline creation.
    // The Vulkan specification uses it to specify the input of the entire pipeline, 
    // but since the first stage is almost always the input assembler, we can consider it as 
    // part of the input assembler state.
    VkPipelineVertexInputStateCreateInfo vertexInputState = {};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.vertexBindingDescriptionCount = 1;
    vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputState.vertexAttributeDescriptionCount = 2;
    vertexInputState.pVertexAttributeDescriptions = vertexInputAttributs.data();
    
    // Input assembly state describes how primitives are assembled by the input assembler.
    // This pipeline will assemble vertex data as a triangle lists (though we only use one triangle).
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    //
    // Rasterization state
    //
    VkPipelineRasterizationStateCreateInfo rasterizationState = {};
    rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationState.lineWidth = 1.0f;
    
    //
    // Per-Fragment Operations state
    //
    // Color blend state describes how blend factors are calculated (if used)
    // We need a blend state per color attachment (even if blending is not used)
    // because the pipeline needs to know the components\channels of the pixels in the color
    // attachemnts that can be written to.
    VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
    blendAttachmentState[0].colorWriteMask = 0xf;
    blendAttachmentState[0].blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlendState = {};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = 1;
    colorBlendState.pAttachments = blendAttachmentState;

    // Depth and stencil state containing depth and stencil information (compare and write 
    // operations; more on this in a later tutorial).
    // This sample won't make use of depth and stencil tests, and we could pass a 
    // nullptr to the graphics pipeline. However, we can also explicitly define a 
    // state indicating that the depth and stencil tests are disabled.
    VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable = VK_FALSE;
    depthStencilState.depthWriteEnable = VK_FALSE;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
    depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
    depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilState.stencilTestEnable = VK_FALSE;
    depthStencilState.front = depthStencilState.back;
    
    //
    // Enable dynamic states
    //
    // Most states are stored into the pipeline, but there are still a few dynamic states 
    // that can be changed within a command buffer.
    // To be able to change these state dynamically we need to specify which ones in the pipeline object. 
    // At that point, we can set the actual states later on in the command buffer.
    //
    // For this example we will set the viewport and scissor using dynamic states
    std::vector<VkDynamicState> dynamicStateEnables;
    dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pDynamicStates = dynamicStateEnables.data();
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    // Viewport state sets the number of viewports and scissor used in this pipeline.
    // We still need to set this information statically in the pipeline object.
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    //
    // Multi sampling state
    //
    // This example does not make use of multi sampling (for anti-aliasing), 
    // but the state must still be set and passed to the pipeline.
    VkPipelineMultisampleStateCreateInfo multisampleState = {};
    multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleState.pSampleMask = nullptr;
    
    //
    // Shaders
    //
    // This sample will only use two programmable stage: Vertex and Fragment shaders
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    
    // Vertex shader
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    // Load binary SPIR-V shader module
    shaderStages[0].module = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/triangle.vert.spv");
    // Main entry point for the shader
    shaderStages[0].pName = "main";
    assert(shaderStages[0].module != VK_NULL_HANDLE);
    
    // Fragment shader
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Load binary SPIR-V shader module
    shaderStages[1].module = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/triangle.frag.spv");
    // Main entry point for the shader
    shaderStages[1].pName = "main";
    assert(shaderStages[1].module != VK_NULL_HANDLE);

    //
    // Create the graphics pipeline used in this sample
    //
    
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    // The pipeline layout used for this pipeline (can be shared among multiple pipelines using the same layout)
    pipelineCreateInfo.layout = m_sampleParams.PipelineLayout;
    // Render pass object defining what render pass instances the pipeline will be compatible with
    pipelineCreateInfo.renderPass = m_sampleParams.RenderPass;
    
    // Set pipeline shader stage info
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();
    
    // Assign the pipeline states to the pipeline creation info structure
    pipelineCreateInfo.pVertexInputState = &vertexInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    
    // Create a graphics pipeline using the specified states
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_sampleParams.GraphicsPipeline));
    
    // SPIR-V shader modules are no longer needed once the graphics pipeline has been created
    // since the SPIR-V modules are compiled during pipeline creation.
    vkDestroyShaderModule(m_vulkanParams.Device, shaderStages[0].module, nullptr);
    vkDestroyShaderModule(m_vulkanParams.Device, shaderStages[1].module, nullptr);
}

void VKHelloTextures::PopulateCommandBuffer(uint32_t currentImageIndex)
{
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // We use a single color attachment that is cleared at the start of the subpass.
    VkClearValue clearValues[1];
    clearValues[0].color = { { 1.0f, 0.5f, 2.0f, 1.0f } };

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // Set the render area that is affected by the render pass instance.
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = m_width;
    renderPassBeginInfo.renderArea.extent.height = m_height;
    // Set clear values for all framebuffer attachments with loadOp set to clear.
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;
    // Set the render pass object used to begin an instance of.
    renderPassBeginInfo.renderPass = m_sampleParams.RenderPass;
    // Set the frame buffer to specify the color attachment (render target) where to draw the current frame.
    renderPassBeginInfo.framebuffer = m_sampleParams.Framebuffers[currentImageIndex];

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], &cmdBufInfo));

    // Begin the render pass instance.
    // This will clear the color attachment.
    vkCmdBeginRenderPass(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Update dynamic viewport state
    VkViewport viewport = {};
    viewport.height = (float)m_height;
    viewport.width = (float)m_width;
    viewport.minDepth = (float)0.0f;
    viewport.maxDepth = (float)1.0f;
    vkCmdSetViewport(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &viewport);

    // Update dynamic scissor state
    VkRect2D scissor = {};
    scissor.extent.width = m_width;
    scissor.extent.height = m_height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &scissor);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            0, nullptr);

    // Bind the graphics pipeline.
    // The pipeline object contains all states of the graphics pipeline, 
    // binding it will set all the states specified at pipeline creation time
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                      VK_PIPELINE_BIND_POINT_GRAPHICS, 
                      m_sampleParams.GraphicsPipeline);
    
    // Bind triangle vertex buffer (contains positions and texture coordinates)
    VkDeviceSize offsets[1] = { 0 };
    vkCmdBindVertexBuffers(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &m_vertices.buffer, offsets);
    
    // Draw triangle
    vkCmdDraw(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 3, 1, 0, 0);
    
    // Ending the render pass will add an implicit barrier, transitioning the frame buffer color attachment to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex]);
    
     VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex]));
}

void VKHelloTextures::SubmitCommandBuffer()
{
    // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // The submit info structure specifies a command buffer queue submission batch
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &waitStageMask;                                                // Pointer to the list of pipeline stages that the semaphore waits will occur at
    submitInfo.waitSemaphoreCount = 1;                                                            // One wait semaphore
    submitInfo.signalSemaphoreCount = 1;                                                          // One signal semaphore
    submitInfo.pCommandBuffers = &m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex];   // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;                                                            // One command buffer

    submitInfo.pWaitSemaphores = &m_sampleParams.FrameRes.ImageAvailableSemaphores[m_frameIndex];        // Semaphore(s) to wait upon before the submitted command buffers start executing
    submitInfo.pSignalSemaphores = &m_sampleParams.FrameRes.RenderingFinishedSemaphores[m_frameIndex];   // Semaphore(s) to be signaled when command buffers have completed

    VK_CHECK_RESULT(vkQueueSubmit(m_vulkanParams.GraphicsQueue.Handle, 1, &submitInfo, m_sampleParams.FrameRes.Fences[m_frameIndex]));
}

void VKHelloTextures::PresentImage(uint32_t currentImageIndex)
{
    // Present the current buffer to the swap chain.
    // Pass the semaphore from the submit info as the wait semaphore for swap chain presentation.
    // This ensures that the image is not presented to the windowing system until all commands have been executed.
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = NULL;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_vulkanParams.SwapChain.Handle;
    presentInfo.pImageIndices = &currentImageIndex;
    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (m_sampleParams.FrameRes.RenderingFinishedSemaphores[m_frameIndex] != VK_NULL_HANDLE)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_sampleParams.FrameRes.RenderingFinishedSemaphores[m_frameIndex];
    }

    VkResult present = vkQueuePresentKHR(m_vulkanParams.GraphicsQueue.Handle, &presentInfo);
    if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) 
    {
        if (present == VK_ERROR_OUT_OF_DATE_KHR)
            WindowResize(m_width, m_height);
        else
            VK_CHECK_RESULT(present);
    }
}