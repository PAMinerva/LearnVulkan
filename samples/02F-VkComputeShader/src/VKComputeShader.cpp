#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKComputeShader.hpp"
#include "VKDebug.hpp"
#include "MathHelper.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/ext/scalar_constants.hpp"

#define MESH_QUAD "MeshQuad"
#define DESC_SET_PRE_COMPUTE "DescSetPreCompute"
#define DESC_SET_POST_COMPUTE "DescSetPostCompute"
#define DESC_SET_COMPUTE "DescSetCompute"
#define PIPELINE_RENDER "PipelineRender"
#define PIPELINE_LUMINANCE "PipelineLuminance"
#define SEMAPHORE_GRAPH_COMPLETE "SemaphoreGraphicsComplete"
#define SEMAPHORE_COMP_COMPLETE "SemaphoreComputeComplete"

VKComputeShader::VKComputeShader(uint32_t width, uint32_t height, std::string name) :
VKSample(width, height, name),
m_dynamicUBOAlignment(0)
{
    // Initialize mesh objects
    m_meshObjects[MESH_QUAD] = {};

    // Initialize the pointer to the memory region that will store the array of mesh info.
    dynUBufVS.meshInfo = nullptr;

    // Initialize the view matrix
    glm::vec3 c_pos = { 0.0f, -3.0f, 0.0f };
    glm::vec3 c_at =  { 0.0f, 0.0f, 0.0f };
    glm::vec3 c_down =  { 0.0f, 0.0f, -1.0f };
    uBufVS.viewMatrix = glm::lookAtLH(c_pos, c_at, c_down);

    // Initialize the projection matrix by setting the frustum information
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)width*0.5f/height, 0.01f, 100.0f);
}

VKComputeShader::~VKComputeShader()
{
    if (dynUBufVS.meshInfo)
        AlignedFree(dynUBufVS.meshInfo);
}

void VKComputeShader::OnInit()
{
    InitVulkan();
    SetupPipeline();

    // Update buffer data (view and projection matrices)
    UpdateHostVisibleBufferData();
}

void VKComputeShader::InitVulkan()
{
    CreateInstance();
    CreateSurface();
    CreateDevice(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT); // Check for a queue family supporting both graphics and compute operations
    GetDeviceQueue(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.FamilyIndex, m_vulkanParams.GraphicsQueue.Handle);
    CreateSwapchain(&m_width, &m_height, VKApplication::settings.vsync);
    CreateDepthStencilImage(m_width, m_height);
    CreateRenderPass();
    CreateFrameBuffers();
    AllocateCommandBuffers();
    CreateSynchronizationObjects();
}

void VKComputeShader::SetupPipeline()
{
    CreateVertexBuffer();
    CreateStagingBuffer();
    CreateInputTexture();
    CreateOutputTextures();
    CreateHostVisibleBuffers();
    CreateHostVisibleDynamicBuffers();
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    AllocateDescriptorSets();
    CreatePipelineLayout();
    CreatePipelineObjects();
    PrepareCompute();

    m_initialized = true;
}

void VKComputeShader::EnableInstanceExtensions(std::vector<const char*>& instanceExtensions)
{ }

void VKComputeShader::EnableDeviceExtensions(std::vector<const char*>& deviceExtensions)
{ }

void VKComputeShader::EnableFeatures(VkPhysicalDeviceFeatures& features)
{ }

// Update frame-based values.
void VKComputeShader::OnUpdate()
{
    m_timer.Tick(nullptr);
    
    // Update FPS and frame count.
    snprintf(m_lastFPS, (size_t)32, "%u fps", m_timer.GetFramesPerSecond());
    m_frameCounter++;

    // Update dynamic buffer data (world matrices and solid colors)
    UpdateHostVisibleDynamicBufferData();
}

// Render the scene.
void VKComputeShader::OnRender()
{
    //
    // Compute work
    //

    // Ensure no more than MAX_FRAME_LAG command buffers are queued to the compute queue.
    VK_CHECK_RESULT(vkWaitForFences(m_vulkanParams.Device, 1, &m_sampleComputeParams.FrameRes.Fences[m_frameIndex], VK_TRUE, UINT64_MAX));
    VK_CHECK_RESULT(vkResetFences(m_vulkanParams.Device, 1, &m_sampleComputeParams.FrameRes.Fences[m_frameIndex]));

    PopulateComputeCommandBuffer();
    SubmitComputeCommandBuffer();

    //
    // Graphics work
    //

    // Ensure no more than MAX_FRAME_LAG frames are queued to the graphics queue.
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

void VKComputeShader::OnDestroy()
{
    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Destroy vertex and index buffer objects and deallocate backing memory
    vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffers.VBbuffer, nullptr);
    vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffers.IBbuffer, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffers.VBmemory, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffers.IBmemory, nullptr);

    // Destroy\Unmap per-frame resources
    for (uint32_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Unmap host-visible device memory
        vkUnmapMemory(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory);

        // Destroy buffer object and deallocate backing memory
        vkDestroyBuffer(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle, nullptr);
        vkFreeMemory(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleBuffers[i].Memory, nullptr);

        // Destroy dynamic buffer object and deallocate backing memory
        vkDestroyBuffer(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Handle, nullptr);
        vkFreeMemory(m_vulkanParams.Device, m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Memory, nullptr);

        // Wait for fences before destroying them
        vkWaitForFences(m_vulkanParams.Device, 1, &m_sampleParams.FrameRes.Fences[i], VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_vulkanParams.Device, m_sampleParams.FrameRes.Fences[i], NULL);
        vkWaitForFences(m_vulkanParams.Device, 1, &m_sampleComputeParams.FrameRes.Fences[i], VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_vulkanParams.Device, m_sampleComputeParams.FrameRes.Fences[i], NULL);

        // Destroy semaphores
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.ImageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.RenderingCompleteSemaphores[i], NULL);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE][i], NULL);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE][i], NULL);

        // Destroy output images and samplers
        vkDestroyImageView(m_vulkanParams.Device, m_outputTextures[i].TextureImage.Descriptor.imageView, nullptr);
        vkDestroyImage(m_vulkanParams.Device, m_outputTextures[i].TextureImage.Handle, nullptr);
        vkFreeMemory(m_vulkanParams.Device, m_outputTextures[i].TextureImage.Memory, nullptr);
        vkDestroySampler(m_vulkanParams.Device, m_outputTextures[i].TextureImage.Descriptor.sampler, nullptr);
    }

    // Destroy staging buffer
    vkDestroyBuffer(m_vulkanParams.Device, m_inputTexture.StagingBuffer.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_inputTexture.StagingBuffer.Memory, nullptr);

    // Destroy input image and sampler
    vkDestroyImageView(m_vulkanParams.Device, m_inputTexture.TextureImage.Descriptor.imageView, nullptr);
    vkDestroyImage(m_vulkanParams.Device, m_inputTexture.TextureImage.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_inputTexture.TextureImage.Memory, nullptr);
    vkDestroySampler(m_vulkanParams.Device, m_inputTexture.TextureImage.Descriptor.sampler, nullptr);

    // Destroy descriptor set layout for compute pipeline
    vkDestroyDescriptorSetLayout(m_vulkanParams.Device, m_sampleComputeParams.DescriptorSetLayout, nullptr);

    // Destroy compute pipeline and pipeline layout objects
    vkDestroyPipelineLayout(m_vulkanParams.Device, m_sampleComputeParams.PipelineLayout, nullptr);
    for (auto const& pl : m_sampleComputeParams.Pipelines)
        vkDestroyPipeline(m_vulkanParams.Device, pl.second, nullptr);

    // Destroy descriptor pool
    vkDestroyDescriptorPool(m_vulkanParams.Device, m_sampleParams.DescriptorPool, nullptr);

    // Destroy descriptor set layout for graphics pipeline
    vkDestroyDescriptorSetLayout(m_vulkanParams.Device, m_sampleParams.DescriptorSetLayout, nullptr);

    // Destroy pipeline and pipeline layout objects
    vkDestroyPipelineLayout(m_vulkanParams.Device, m_sampleParams.PipelineLayout, nullptr);
    for (auto const& pl : m_sampleParams.Pipelines)
        vkDestroyPipeline(m_vulkanParams.Device, pl.second, nullptr);

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

    // Destroy depth-stencil image
    vkDestroyImageView(m_vulkanParams.Device, m_vulkanParams.DepthStencilImage.View, nullptr);
    vkDestroyImage(m_vulkanParams.Device, m_vulkanParams.DepthStencilImage.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vulkanParams.DepthStencilImage.Memory, nullptr);

    // Free allocated command buffers
    vkFreeCommandBuffers(m_vulkanParams.Device, 
                         m_sampleParams.CommandPool,
                          static_cast<uint32_t>(m_sampleParams.FrameRes.CommandBuffers.size()), 
                          m_sampleParams.FrameRes.CommandBuffers.data());

    vkDestroyRenderPass(m_vulkanParams.Device, m_sampleParams.RenderPass, NULL);

    // Destroy command pool
    vkDestroyCommandPool(m_vulkanParams.Device, m_sampleParams.CommandPool, NULL);

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

void VKComputeShader::OnResize()
{
    // Recreate the projection matrix
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)m_width*0.5f/m_height, 0.01f, 100.0f);

    // Update buffer data (light direction and color, and view and projection matrices)
    UpdateHostVisibleBufferData();
}

// Create vertex and index buffers describing all mesh geometries
void VKComputeShader::CreateVertexBuffer()
{
    // While it's fine for an example application to request small individual memory allocations, that is not
    // what should be done a real-world application, where you should allocate large chunks of memory at once instead.

    //
    // Create the vertex and index buffers.
    //

    // The four vertices of a quad
    std::vector<Vertex> quadVertices =
    {
        { { -1.0f, 0.0f, -1.0f }, {0.0f, 0.0f} },
        { { 1.0f, 0.0f, -1.0f },  {1.0f, 0.0f} },
        { { 1.0f, 0.0f, 1.0f },   {1.0f, 1.0f} },
        { { -1.0f, 0.0f, 1.0f },  {0.0f, 1.0f} }
    };

    size_t vertexBufferSize = static_cast<size_t>(quadVertices.size()) * sizeof(Vertex);
    m_meshObjects[MESH_QUAD].vertexCount = static_cast<uint32_t>(quadVertices.size());

    //
    //  3 ______ 2
    //   |    / |
    //   |  /   |
    //   |/_____|
    //  0       1
    //
    // The indices of the two triangles composing the quad.
    std::vector<uint16_t> indices =
    {
		0, 1, 2,
        0, 2, 3
    };

    size_t indexBufferSize = static_cast<size_t>(indices.size()) * sizeof(uint16_t);
    m_meshObjects[MESH_QUAD].indexCount = indices.size();

    //
    // Create the vertex and index buffers in host-visible device memory for convenience. 
    // This is not recommended as it can result in lower rendering performance.
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
    vertexBufferInfo.size = static_cast<size_t>(quadVertices.size()) * sizeof(Vertex);
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;    
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &vertexBufferInfo, nullptr, &m_vertexindexBuffers.VBbuffer));

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the vertex buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_vertexindexBuffers.VBbuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_vertexindexBuffers.VBmemory));

    // Map the host-visible device memory and copy the vertex data. 
    // Once finished, we can unmap it since we no longer need to access the vertex buffer from the application.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, m_vertexindexBuffers.VBmemory, 0, memAlloc.allocationSize, 0, &data));
    memcpy(data, quadVertices.data(), static_cast<size_t>(quadVertices.size()) * sizeof(Vertex));
    vkUnmapMemory(m_vulkanParams.Device, m_vertexindexBuffers.VBmemory);

    // Bind the vertex buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, m_vertexindexBuffers.VBbuffer, m_vertexindexBuffers.VBmemory, 0));

    // Create the index buffer object
    VkBufferCreateInfo indexBufferInfo = {};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = vertexBufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;    
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &indexBufferInfo, nullptr, &m_vertexindexBuffers.IBbuffer));

    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_vertexindexBuffers.IBbuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_vertexindexBuffers.IBmemory));

    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, m_vertexindexBuffers.IBmemory, 0, memAlloc.allocationSize, 0, &data));
    memcpy(data, indices.data(), indexBufferSize);
    vkUnmapMemory(m_vulkanParams.Device, m_vertexindexBuffers.IBmemory);

    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, m_vertexindexBuffers.IBbuffer, m_vertexindexBuffers.IBmemory, 0));
}

void VKComputeShader::CreateHostVisibleBuffers()
{
    //
    // Create buffers in host-visible device memory
    // that need to be updated from the CPU on a per-frame basis.
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
        // Create a buffer in coherent, host-visible device memory.
        CreateBuffer(m_vulkanParams.Device, 
                        bufferInfo, 
                        m_sampleParams.FrameRes.HostVisibleBuffers[i],
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        m_deviceMemoryProperties);

        // Store information needed to write\update the corresponding descriptor (uniform buffer) in the descriptor set later.
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.buffer = m_sampleParams.FrameRes.HostVisibleBuffers[i].Handle;
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.offset = 0;
        m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor.range = sizeof(uBufVS);
    }
}

void VKComputeShader::CreateHostVisibleDynamicBuffers()
{
    // Create the dynamic buffer that store the array of world matrices.
	// Uniform block alignment differs between GPUs.

	// Calculate required alignment based on minimum device offset alignment
	size_t minUBOAlignment = m_deviceProperties.limits.minUniformBufferOffsetAlignment;
	m_dynamicUBOAlignment = sizeof(MeshInfo);
	if (minUBOAlignment > 0)
		m_dynamicUBOAlignment = (m_dynamicUBOAlignment + minUBOAlignment - 1) & ~(minUBOAlignment - 1);
    
	size_t dynBufferSize = m_numDrawCalls * m_dynamicUBOAlignment;

    dynUBufVS.meshInfo = (MeshInfo*)AlignedAlloc(dynBufferSize, m_dynamicUBOAlignment);
	assert(dynUBufVS.meshInfo);

    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;
    
    // Create the buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dynBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    m_sampleParams.FrameRes.HostVisibleDynamicBuffers.resize(MAX_FRAME_LAG);
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Create a buffer in coherent, host-visible device memory that is large enough to hold the array of world matrices.
        CreateBuffer(m_vulkanParams.Device, 
                        bufferInfo, 
                        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i],
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        m_deviceMemoryProperties);

        // Store information needed to write\update the corresponding descriptor (dynamic uniform buffer) in the descriptor set later.
        // In this case:
        // offset is the base offset (into the buffer) from which dynamic offsets will be applied
        // range is the static size used for all dynamic offsets; describe a region of m_dynamicUBOAlignment bytes in the buffer, depending on the dynamic offset provided
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.buffer = m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Handle;
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.offset = 0;
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.range = sizeof(MeshInfo);

        // Save buffer size for later use
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Size = dynBufferSize;
    }    
}

std::vector<uint8_t> VKComputeShader::GenerateTextureData()
{
    const size_t rowPitch = m_inputTexture.TextureWidth * m_inputTexture.TextureTexelSize; // The size of a texture row in bytes
    const size_t cellPitch = rowPitch >> 3;                                                // The width of a cell in the checkerboard texture, in bytes.
    const size_t cellHeight = m_inputTexture.TextureWidth >> 3;                            // The height of a cell in the checkerboard texture, in texels.
    const size_t textureSize = rowPitch * m_inputTexture.TextureHeight;                    // Texture size in bytes
 
    std::vector<uint8_t> data(textureSize);
    uint8_t* pData = &data[0];
 
    for (size_t n = 0; n < textureSize; n += m_inputTexture.TextureTexelSize)
    {
        size_t x = n % rowPitch;    // horizontal byte position within a texture row
        size_t y = n / rowPitch;    // vertical byte position within a texture column
        size_t i = x / cellPitch;   // i-th column of cells within the checkerboard texture
        size_t j = y / cellHeight;  // j-th row of cells within the checkerboard texture
 
        if (i % 2 == j % 2)
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xaa;    // G
            pData[n + 2] = 0xaa;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xa0;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
    }
 
    return data;
}

void VKComputeShader::CreateStagingBuffer()
{
    // Create a buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_inputTexture.TextureWidth * m_inputTexture.TextureHeight * m_inputTexture.TextureTexelSize,
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &bufferInfo, nullptr, &m_inputTexture.StagingBuffer.Handle));

    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the staging buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_inputTexture.StagingBuffer.Handle, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_inputTexture.StagingBuffer.Memory));

    // Bind the buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, 
                                        m_inputTexture.StagingBuffer.Handle, 
                                        m_inputTexture.StagingBuffer.Memory, 0));

    // Map the host-visible device memory just allocated.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, 
                                m_inputTexture.StagingBuffer.Memory, 
                                0, memAlloc.allocationSize, 
                                0, &m_inputTexture.StagingBuffer.MappedMemory));

    // Copy texture data in the staging buffer
    std::vector<uint8_t> texData = GenerateTextureData();
    memcpy(m_inputTexture.StagingBuffer.MappedMemory, texData.data(), texData.size());

    // Unmap staging buffer
    vkUnmapMemory(m_vulkanParams.Device, m_inputTexture.StagingBuffer.Memory);
}

void VKComputeShader::CreateInputTexture()
{
    const VkFormat tex_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties props;

    vkGetPhysicalDeviceFormatProperties(m_vulkanParams.PhysicalDevice, tex_format, &props);

    // Check if the device can sample from R8G8B8A8_UNORM textures in local device memory
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
    {
        // Create a texture image
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = tex_format;
        imageCreateInfo.extent = {m_inputTexture.TextureWidth, m_inputTexture.TextureHeight, 1};
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateImage(m_vulkanParams.Device, &imageCreateInfo, nullptr, &m_inputTexture.TextureImage.Handle));

        // Used to request an allocation of a specific size from a certain memory type.
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Request a memory allocation from local device memory that is large 
        // enough to hold the texture image.
        vkGetImageMemoryRequirements(m_vulkanParams.Device, m_inputTexture.TextureImage.Handle, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_deviceMemoryProperties);
        VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_inputTexture.TextureImage.Memory));

        // Bind the image object to the backing local device memory just allocated.
        VK_CHECK_RESULT(vkBindImageMemory(m_vulkanParams.Device, 
                                            m_inputTexture.TextureImage.Handle, 
                                            m_inputTexture.TextureImage.Memory, 0));

        // We need to record some commands to copy staging buffer data to texture image in local device memory
        VkCommandBufferBeginInfo cmdBufferInfo = {};
        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[0], &cmdBufferInfo);

        // Transition the image layout to provide optimal performance for transfering operations that use the image as a destination
        TransitionImageLayout(m_sampleParams.FrameRes.CommandBuffers[0],
                                m_inputTexture.TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
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
        copyRegion.imageExtent = {m_inputTexture.TextureWidth, m_inputTexture.TextureHeight, 1};

        vkCmdCopyBufferToImage(m_sampleParams.FrameRes.CommandBuffers[0], 
                                m_inputTexture.StagingBuffer.Handle, m_inputTexture.TextureImage.Handle, 
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition the image layout to provide optimal performance for reading by shaders
        TransitionImageLayout(m_sampleParams.FrameRes.CommandBuffers[0],
                        m_inputTexture.TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        // Save the last image layout
        m_inputTexture.TextureImage.Descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

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
        viewInfo.image = m_inputTexture.TextureImage.Handle;
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
        VK_CHECK_RESULT(vkCreateSampler(m_vulkanParams.Device, &samplerInfo, NULL, &m_inputTexture.TextureImage.Descriptor.sampler));

        // Create an image view
        VK_CHECK_RESULT(vkCreateImageView(m_vulkanParams.Device, &viewInfo, NULL, &m_inputTexture.TextureImage.Descriptor.imageView));
    }
    else {
        /* Can't support VK_FORMAT_R8G8B8A8_UNORM !? */
        assert(!"No support for R8G8B8A8_UNORM as texture image format");
    }

    // Flush the command buffer
    FlushInitCommandBuffer(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.Handle, m_sampleParams.FrameRes.CommandBuffers[0], m_sampleParams.FrameRes.Fences[0]);
}

void VKComputeShader::CreateOutputTextures()
{
    m_outputTextures.resize(MAX_FRAME_LAG);
    const VkFormat tex_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties props;

	// Get device properties for the R8G8B8A8_UNORM format
    vkGetPhysicalDeviceFormatProperties(m_vulkanParams.PhysicalDevice, tex_format, &props);

    // Check if the device can execute storage image operations from R8G8B8A8_UNORM textures in local device memory
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
    {
        for (size_t i = 0; i < MAX_FRAME_LAG; i++)
        {
            // Create a texture to be used as storage image (in CS) and combined image sampler (in FS)
            VkImageCreateInfo imageCreateInfo = {};
            imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.format = tex_format;
            imageCreateInfo.extent = {m_outputTextures[i].TextureWidth, m_outputTextures[i].TextureHeight, 1};
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VK_CHECK_RESULT(vkCreateImage(m_vulkanParams.Device, &imageCreateInfo, nullptr, &m_outputTextures[i].TextureImage.Handle));

            // Used to request an allocation of a specific size from a certain memory type.
            VkMemoryAllocateInfo memAlloc = {};
            memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            VkMemoryRequirements memReqs;

            // Request a memory allocation from local device memory that is large 
            // enough to hold the texture image.
            vkGetImageMemoryRequirements(m_vulkanParams.Device, m_outputTextures[i].TextureImage.Handle, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_deviceMemoryProperties);
            VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_outputTextures[i].TextureImage.Memory));

            // Bind the image object to the backing local device memory just allocated.
            VK_CHECK_RESULT(vkBindImageMemory(m_vulkanParams.Device, 
                                                m_outputTextures[i].TextureImage.Handle, 
                                                m_outputTextures[i].TextureImage.Memory, 0));

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
            viewInfo.image = m_outputTextures[i].TextureImage.Handle;
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
            VK_CHECK_RESULT(vkCreateSampler(m_vulkanParams.Device, &samplerInfo, NULL, &m_outputTextures[i].TextureImage.Descriptor.sampler));

            // Create an image view
            VK_CHECK_RESULT(vkCreateImageView(m_vulkanParams.Device, &viewInfo, NULL, &m_outputTextures[i].TextureImage.Descriptor.imageView));
        }
    }
    else 
    {
        /* Can't support VK_FORMAT_R8G8B8A8_UNORM !? */
        assert(!"No support for R8G8B8A8_UNORM as texture image format");
    }

    //
    // Set the image layout for general access
    //

    VkCommandBufferBeginInfo cmdBufferInfo = {};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[0], &cmdBufferInfo);

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        TransitionImageLayout(m_sampleParams.FrameRes.CommandBuffers[0],
                                m_outputTextures[i].TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                VK_ACCESS_NONE, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        // Save the last image layout
        m_outputTextures[i].TextureImage.Descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    // Flush the command buffer
    FlushInitCommandBuffer(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.Handle, m_sampleParams.FrameRes.CommandBuffers[0], m_sampleParams.FrameRes.Fences[0]);
}

void VKComputeShader::UpdateHostVisibleBufferData()
{
    // Update uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
        memcpy(m_sampleParams.FrameRes.HostVisibleBuffers[i].MappedMemory, &uBufVS, sizeof(uBufVS));
}

void VKComputeShader::UpdateHostVisibleDynamicBufferData()
{
    // Set an identity matrix as world matrix
    m_meshObjects[MESH_QUAD].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                        (m_meshObjects[MESH_QUAD].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    
    m_meshObjects[MESH_QUAD].meshInfo->worldMatrix = glm::identity<glm::mat4>();

    // Update dynamic uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    memcpy(m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].MappedMemory,
           dynUBufVS.meshInfo, 
           m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].Size);
}

void VKComputeShader::CreateDescriptorPool()
{
    //
    // To calculate the amount of memory required for a descriptor pool, the implementation needs to know
    // the max numbers of descriptor sets we will request from the pool, and the number of descriptors 
    // per type we will include in those descriptor sets.
    //

    // Describe the number of descriptors per type.
    // This sample uses four descriptor types (uniform buffer, dynamic uniform buffer, combined image sampler and storage image)
    VkDescriptorPoolSize typeCounts[4];
    typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    typeCounts[0].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG) * 2; // Pre and Post compute descriptor sets
    typeCounts[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    typeCounts[1].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG) * 2; // Pre and Post compute descriptor sets
    typeCounts[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    typeCounts[2].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG) * 2; // Pre and Post compute descriptor sets
    typeCounts[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    typeCounts[3].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG) * 2; // Pre and Post compute descriptor sets

    // Create a global descriptor pool
    // All descriptors set used in this sample will be allocated from this pool
    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.pNext = nullptr;
    descriptorPoolInfo.poolSizeCount = 4;
    descriptorPoolInfo.pPoolSizes = typeCounts;
    // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
    descriptorPoolInfo.maxSets = static_cast<uint32_t>(MAX_FRAME_LAG) * 3; // Pre and Post compute descriptor sets (for graphics) and compute descriptor set (for compute)

    VK_CHECK_RESULT(vkCreateDescriptorPool(m_vulkanParams.Device, &descriptorPoolInfo, nullptr, &m_sampleParams.DescriptorPool));
}

void VKComputeShader::CreateDescriptorSetLayout()
{
    //
    // Create a Descriptor Set Layout to connect binding points (resource declarations)
    // in the shader code to descriptors within descriptor sets.
    //

    VkDescriptorSetLayoutBinding layoutBinding[3] = {};

    // Binding 0: Uniform buffer (accessed by VS)
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // Binding 1: Dynamic uniform buffer (accessed by VS)
    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    // Binding 2: Combined image sampler (accessed by FS)
    layoutBinding[2].binding = 2;
    layoutBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding[2].descriptorCount = 1;
    layoutBinding[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = 3;
    descriptorLayout.pBindings = layoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vulkanParams.Device, &descriptorLayout, nullptr, &m_sampleParams.DescriptorSetLayout));
}

void VKComputeShader::AllocateDescriptorSets()
{
    // Allocate MAX_FRAME_LAG descriptor sets from the global descriptor pool.
    // Use the descriptor set layout to calculate the amount on memory required to store the descriptor sets.
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_sampleParams.DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    std::vector<VkDescriptorSetLayout> DescriptorSetLayouts(MAX_FRAME_LAG, m_sampleParams.DescriptorSetLayout);
    allocInfo.pSetLayouts = DescriptorSetLayouts.data();

    m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE].resize(MAX_FRAME_LAG);
    m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE].resize(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vulkanParams.Device, &allocInfo, m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE].data()));
    VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vulkanParams.Device, &allocInfo, m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE].data()));

    //
    // Write the descriptors updating the corresponding descriptor sets.
    // For every binding point used in a shader code there needs to be at least a descriptor 
    // in a descriptor set matching that binding point.
    //
    VkWriteDescriptorSet writeDescriptorSet[3] = {};

    //
    // Input texture before compute shader
    //
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Write the descriptor of the uniform buffer.
        // We need to pass the descriptor set where it is store and 
        // the binding point associated with the descriptor in the descriptor set.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE][i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet[0].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the dynamic uniform buffer.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE][i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSet[1].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        // Write the descriptor of the combined image sampler.
        writeDescriptorSet[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[2].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE][i];
        writeDescriptorSet[2].descriptorCount = 1;
        writeDescriptorSet[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet[2].pImageInfo = &m_inputTexture.TextureImage.Descriptor;
        writeDescriptorSet[2].dstBinding = 2;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 3, writeDescriptorSet, 0, nullptr);
    }

    //
    // Output texture after compute shader
    //
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Write the descriptor of the uniform buffer.
        // We need to pass the descriptor set where it is store and 
        // the binding point associated with the descriptor in the descriptor set.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE][i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet[0].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the dynamic uniform buffer.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE][i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSet[1].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        // Write the descriptor of the combined image sampler.
        writeDescriptorSet[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[2].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE][i];
        writeDescriptorSet[2].descriptorCount = 1;
        writeDescriptorSet[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet[2].pImageInfo = &m_outputTextures[i].TextureImage.Descriptor;
        writeDescriptorSet[2].dstBinding = 2;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 3, writeDescriptorSet, 0, nullptr);
    }
}

void VKComputeShader::CreatePipelineLayout()
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

void VKComputeShader::CreatePipelineObjects()
{
    //
    //  Set the various states for the graphics pipeline used by this sample
    //

    //
    // Input assembler state
    //    
    // Vertex binding descriptions describe the input assembler binding points where vertex buffers will be bound.
    // This sample uses a single vertex buffer at binding point 0 (see vkCmdBindVertexBuffers).
    VkVertexInputBindingDescription vertexInputBinding = {};
    vertexInputBinding.binding = 0;
    vertexInputBinding.stride = sizeof(Vertex);
    
    // Vertex attribute descriptions describe the vertex shader attribute locations and memory layouts, 
    // as well as the binding points from which the input assembler should retrieve data to pass to the 
    // corresponding vertex shader input attributes.
    std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs;
    // This match the following shader layout (see vertex shader):
    //	layout (location = 0) in vec3 inPos;
    //  layout (location = 1) in vec2 inTextCoord;
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
    // This pipeline will assemble vertex data as a triangle lists.
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    //
    // Rasterization state
    //
    VkPipelineRasterizationStateCreateInfo rasterizationState = {};
    rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;            // Cull back faces
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

    // Depth and stencil state containing depth and stencil information (compare and write operations; more on this in a later tutorial).
    // We also need to specify if the depth and stencil tests are enabled or disabled.
    VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable = VK_TRUE;
    depthStencilState.depthWriteEnable = VK_TRUE;
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
    VkShaderModule renderVS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/render.vert.spv");
    VkShaderModule renderFS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/render.frag.spv");


    // This sample will use three programmable stage: Vertex, Geometry and Fragment shaders
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    
    // Vertex shader
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    // Load binary SPIR-V shader module
    shaderStages[0].module = renderVS;
    // Main entry point for the shader
    shaderStages[0].pName = "main";
    assert(shaderStages[0].module != VK_NULL_HANDLE);
    
    // Fragment shader
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Load binary SPIR-V shader module
    shaderStages[1].module = renderFS;
    // Main entry point for the shader
    shaderStages[1].pName = "main";
    assert(shaderStages[1].module != VK_NULL_HANDLE);

    //
    // Create the graphics pipelines used in this sample
    //

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    // The pipeline layout used for this pipeline (can be shared among multiple pipelines using the same layout)
    pipelineCreateInfo.layout = m_sampleParams.PipelineLayout;
    // Render pass object defining what render pass instances the pipeline will be compatible with
    pipelineCreateInfo.renderPass = m_sampleParams.RenderPass;
    
    // Set pipeline shader stage info (it only includes the VS shader for capturing the result in the TF stage)
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
    
    // Create a graphics pipeline for rendering the quads
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.Pipelines[PIPELINE_RENDER]));

    //
    // Destroy shader modules
    //

    // SPIR-V shader modules are no longer needed once the graphics pipeline has been created
    // since the SPIR-V modules are compiled during pipeline creation.
    vkDestroyShaderModule(m_vulkanParams.Device, renderVS, nullptr);
    vkDestroyShaderModule(m_vulkanParams.Device, renderFS, nullptr);
}

void VKComputeShader::PrepareCompute()
{
    //
    // Get a compute queue from the same queue family used for recording graphics commands
    //

    // We already checked that the queue family used for graphics also supports compute work
    m_vulkanParams.ComputeQueue.FamilyIndex = m_vulkanParams.GraphicsQueue.FamilyIndex;

    // Get a compute queue from the device
	vkGetDeviceQueue(m_vulkanParams.Device, m_vulkanParams.ComputeQueue.FamilyIndex, 0, &m_vulkanParams.ComputeQueue.Handle);

    //
    // Create descriptor set layout
    //

    VkDescriptorSetLayoutBinding layoutBinding[2] = {};

    // Binding 0: Input texture
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // Binding 1: Output texture
    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = 2;
    descriptorLayout.pBindings = layoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vulkanParams.Device, &descriptorLayout, nullptr, &m_sampleComputeParams.DescriptorSetLayout));

    //
    // Create a pipeline layout
    //

    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
    pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineLayoutCreateInfo.setLayoutCount = 1;
    pPipelineLayoutCreateInfo.pSetLayouts = &m_sampleComputeParams.DescriptorSetLayout;
    
    VK_CHECK_RESULT(vkCreatePipelineLayout(m_vulkanParams.Device, &pPipelineLayoutCreateInfo, nullptr, &m_sampleComputeParams.PipelineLayout));

    //
    // Allocate descriptor sets and update descriptors
    //
        
    // Allocate MAX_FRAME_LAG descriptor sets from the global descriptor pool.
    // Use the descriptor set layout to calculate the amount on memory required to store the descriptor sets.
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_sampleParams.DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    std::vector<VkDescriptorSetLayout> DescriptorSetLayouts(MAX_FRAME_LAG, m_sampleComputeParams.DescriptorSetLayout);
    allocInfo.pSetLayouts = DescriptorSetLayouts.data();

    m_sampleComputeParams.FrameRes.DescriptorSets[DESC_SET_COMPUTE].resize(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vulkanParams.Device, &allocInfo, m_sampleComputeParams.FrameRes.DescriptorSets[DESC_SET_COMPUTE].data()));

    // Write the descriptors updating the corresponding descriptor sets.
    VkWriteDescriptorSet writeDescriptorSet[2] = {};

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Write the descriptor of the input texture.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleComputeParams.FrameRes.DescriptorSets[DESC_SET_COMPUTE][i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptorSet[0].pImageInfo = &m_inputTexture.TextureImage.Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the output texture.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleComputeParams.FrameRes.DescriptorSets[DESC_SET_COMPUTE][i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptorSet[1].pImageInfo = &m_outputTextures[i].TextureImage.Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 2, writeDescriptorSet, 0, nullptr);
    }

    //
    // Shaders
    //

    VkShaderModule luminanceCS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/luminance.comp.spv");

    VkPipelineShaderStageCreateInfo shaderStage{};
    
    // Compute shader
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    // Load binary SPIR-V shader module
    shaderStage.module = luminanceCS;
    // Main entry point for the shader
    shaderStage.pName = "main";
    assert(shaderStage.module != VK_NULL_HANDLE);

    //
    // Create the compute pipeline used in this sample
    //

    VkComputePipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    // The pipeline layout used for this pipeline
    pipelineCreateInfo.layout = m_sampleComputeParams.PipelineLayout;    
    // Set pipeline shader stage
    pipelineCreateInfo.stage = shaderStage;
    
    // Create a compute pipeline for computing the luminance of the input texture
    VK_CHECK_RESULT(vkCreateComputePipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleComputeParams.Pipelines[PIPELINE_LUMINANCE]));

    // Destroy shader modules
    vkDestroyShaderModule(m_vulkanParams.Device, luminanceCS, nullptr);

    //
    // Allocate command buffers to store compute commands
    //

    m_sampleComputeParams.FrameRes.CommandBuffers.resize(MAX_FRAME_LAG);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = m_sampleParams.CommandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkAllocateCommandBuffers(m_vulkanParams.Device, &commandBufferAllocateInfo, m_sampleComputeParams.FrameRes.CommandBuffers.data()));

    //
    // Create fences and semaphores
    //

    m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE].resize(MAX_FRAME_LAG);
    m_sampleComputeParams.FrameRes.Fences.resize(MAX_FRAME_LAG);

    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;

    // Create fences to synchronize CPU and GPU timelines.
    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Semaphore synchronizing graphics and compute operations
        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkanParams.Device, &semaphoreCreateInfo, nullptr, &m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE][i]));

        // Signaled fence for synchronizing frames in compute queue
        VK_CHECK_RESULT(vkCreateFence(m_vulkanParams.Device, &fenceCreateInfo, nullptr, &m_sampleComputeParams.FrameRes.Fences[i]));
    }

    //
    // Create and signal the graphics semaphores (to immediately submit the compute work during the creation of the first frame)
    //

    m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE].resize(MAX_FRAME_LAG);

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Create an unsignaled semaphore
        VK_CHECK_RESULT(vkCreateSemaphore(m_vulkanParams.Device, &semaphoreCreateInfo, nullptr, &m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE][i]));
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE].data();
    VK_CHECK_RESULT(vkQueueSubmit(m_vulkanParams.GraphicsQueue.Handle, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK_RESULT(vkQueueWaitIdle(m_vulkanParams.GraphicsQueue.Handle));
}

void VKComputeShader::PopulateComputeCommandBuffer()
{
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], &cmdBufInfo));

    // "Transition" the image layout of the output texture to only set a memory barrier
     TransitionImageLayout(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex],
                    m_outputTextures[m_frameIndex].TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Bind the compute pipeline to a compute bind point of the command buffer
    vkCmdBindPipeline(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 
                        VK_PIPELINE_BIND_POINT_COMPUTE, 
                        m_sampleComputeParams.Pipelines[PIPELINE_LUMINANCE]);

    // Bind descriptor set
    vkCmdBindDescriptorSets(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_COMPUTE, 
                            m_sampleComputeParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleComputeParams.FrameRes.DescriptorSets[DESC_SET_COMPUTE][m_frameIndex], 
                            0, nullptr);

    // Dispatch compute work
    vkCmdDispatch(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 
                            m_outputTextures[m_frameIndex].TextureWidth / 16, 
                            m_outputTextures[m_frameIndex].TextureHeight / 16, 1);

    VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex]));
}

void VKComputeShader::PopulateCommandBuffer(uint32_t currentImageIndex)
{
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // Values used to clear the framebuffer attachments at the start of the subpasses that use them.
    VkClearValue clearValues[2];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // Set the render area that is affected by the render pass instance.
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = m_width;
    renderPassBeginInfo.renderArea.extent.height = m_height;
    // Set clear values for all framebuffer attachments with loadOp set to clear.
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;
    // Set the render pass object used to begin an instance of.
    renderPassBeginInfo.renderPass = m_sampleParams.RenderPass;
    // Set the frame buffer to specify the color attachment (render target) where to draw the current frame.
    renderPassBeginInfo.framebuffer = m_sampleParams.Framebuffers[currentImageIndex];

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], &cmdBufInfo));

    // "Transition" the image layout of the output texture to only set a memory barrier
     TransitionImageLayout(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex],
                    m_outputTextures[m_frameIndex].TextureImage.Handle, VK_IMAGE_ASPECT_COLOR_BIT, 
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Begin the render pass instance.
    // This will clear the color attachment.
    vkCmdBeginRenderPass(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Update dynamic viewport state
    VkViewport viewport = {};
    viewport.height = (float)m_height;
    viewport.width = (float)m_width * 0.5f; // select the left part of the render target
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 0, 1, &viewport);

    // Update dynamic scissor state
    VkRect2D scissor = {};
    scissor.extent.width = m_width;
    scissor.extent.height = m_height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 0, 1, &scissor);
    
    // Bind the vertex buffer
    VkDeviceSize offsets[1] = { 0 };
    vkCmdBindVertexBuffers(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 0, 1, &m_vertexindexBuffers.VBbuffer, offsets);

    // Bind the index buffer
    vkCmdBindIndexBuffer(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], m_vertexindexBuffers.IBbuffer, 0, VK_INDEX_TYPE_UINT16);

    // Dynamic offset used to offset into the uniform buffer described by the dynamic uniform buffer and containing mesh information
    uint32_t dynamicOffset = m_meshObjects[MESH_QUAD].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind the graphics pipeline
    vkCmdBindPipeline(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 
                        VK_PIPELINE_BIND_POINT_GRAPHICS, 
                        m_sampleParams.Pipelines[PIPELINE_RENDER]);

    //
    // Pre compute
    //

    // Bind descriptor set with the input texture
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[DESC_SET_PRE_COMPUTE][m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the quad where the input texture will be mapped
    vkCmdDrawIndexed(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], m_meshObjects[MESH_QUAD].indexCount, 1, 0, 0, 0);

    //
    // Post compute
    //

    // Bind descriptor set with the output texture
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[DESC_SET_POST_COMPUTE][m_frameIndex], 
                            1, &dynamicOffset);

    // Shift viewport rectangle to select the right part of the render target
    viewport.x = (float)m_width / 2.0f;
    vkCmdSetViewport(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 0, 1, &viewport);

    // Draw the quad where the output texture will be mapped
    vkCmdDrawIndexed(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], m_meshObjects[MESH_QUAD].indexCount, 1, 0, 0, 0);

    // Ending the render pass will add an implicit barrier, transitioning the frame buffer color attachment to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex]);
    
    VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex]));
}

void VKComputeShader::SubmitCommandBuffer()
{
    // Pipeline stages at which the queue submission will wait (via pWaitSemaphores)
    VkPipelineStageFlags waitStageMasks[] = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    // Wait Semaphores
    VkSemaphore waitSemaphores[] = { m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE][m_frameIndex], m_sampleParams.FrameRes.ImageAvailableSemaphores[m_frameIndex] };
    VkSemaphore signalSemaphores[] = { m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE][m_frameIndex], m_sampleParams.FrameRes.RenderingCompleteSemaphores[m_frameIndex] };
    // The submit info structure specifies a command buffer queue submission batch
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = waitStageMasks;                                        // Pointer to the list of pipeline stages where the semaphore waits will occur
    submitInfo.waitSemaphoreCount = 2;                                                    // Two wait semaphores
    submitInfo.signalSemaphoreCount = 2;                                                  // Two signal semaphores
    submitInfo.pCommandBuffers = &m_sampleParams.FrameRes.CommandBuffers[m_frameIndex];   // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;                                                    // One command buffer

    submitInfo.pWaitSemaphores = waitSemaphores;        // Semaphore(s) to wait upon before the pWaitDstStageMask stagess start executing
    submitInfo.pSignalSemaphores = signalSemaphores;    // Semaphore(s) to be signaled when command buffers have completed

    VK_CHECK_RESULT(vkQueueSubmit(m_vulkanParams.GraphicsQueue.Handle, 1, &submitInfo, m_sampleParams.FrameRes.Fences[m_frameIndex]));
}

void VKComputeShader::SubmitComputeCommandBuffer()
{
    // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    // The submit info structure specifies a command buffer queue submission batch
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &waitStageMask;                                                // Pointer to the list of pipeline stages where the semaphore waits will occur
    submitInfo.waitSemaphoreCount = 1;                                                            // One wait semaphore
    submitInfo.signalSemaphoreCount = 1;                                                          // One signal semaphore
    submitInfo.pCommandBuffers = &m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex];    // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;                                                            // One command buffer

    submitInfo.pWaitSemaphores = &m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE][m_frameIndex];    // Semaphore(s) to wait upon before the pWaitDstStageMask stages start executing
    submitInfo.pSignalSemaphores = &m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE][m_frameIndex];   // Semaphore(s) to be signaled when command buffers have completed

    VK_CHECK_RESULT(vkQueueSubmit(m_vulkanParams.ComputeQueue.Handle, 1, &submitInfo, m_sampleComputeParams.FrameRes.Fences[m_frameIndex]));
}

void VKComputeShader::PresentImage(uint32_t currentImageIndex)
{
    // Present the current image to the presentation engine.
    // Pass the semaphore from the submit info as the wait semaphore for swap chain presentation.
    // This ensures that the image is not presented to the windowing system until all commands have been executed.
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = NULL;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_vulkanParams.SwapChain.Handle;
    presentInfo.pImageIndices = &currentImageIndex;
    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (m_sampleParams.FrameRes.RenderingCompleteSemaphores[m_frameIndex] != VK_NULL_HANDLE)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_sampleParams.FrameRes.RenderingCompleteSemaphores[m_frameIndex];
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
