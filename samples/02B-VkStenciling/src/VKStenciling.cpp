#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKStenciling.hpp"
#include "VKDebug.hpp"
#include "MathHelper.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/ext/scalar_constants.hpp"

VKStenciling::VKStenciling(uint32_t width, uint32_t height, std::string name) :
VKSample(width, height, name),
m_curRotationAngleRad(0.0f),
m_dynamicUBOAlignment(0)
{
    // Initialize the pointer to the memory region that will store the array of world matrices.
    dynUBufVS.meshInfo = nullptr;

    // Initialize mesh objects
    m_meshObjects["cube"] = {0, 36, 0, 0, 24, nullptr};

    m_meshObjects["floor"] = {1, 6, 
                              m_meshObjects["cube"].indexCount, 
                              m_meshObjects["cube"].vertexCount, 
                              4, nullptr};

    m_meshObjects["wall"] = {2, 18, 
                             m_meshObjects["cube"].indexCount + m_meshObjects["floor"].indexCount,
                             m_meshObjects["cube"].vertexCount + m_meshObjects["floor"].vertexCount,
                             10, nullptr};

    m_meshObjects["mirror"] = {3, 6, 
                               m_meshObjects["cube"].indexCount + m_meshObjects["floor"].indexCount + m_meshObjects["wall"].indexCount, 
                               m_meshObjects["cube"].vertexCount + m_meshObjects["floor"].vertexCount + m_meshObjects["wall"].vertexCount, 
                               4, nullptr};

    m_meshObjects["reflectedCube"] = {4, 36, 0, 0, 24, nullptr};

    m_meshObjects["reflectedFloor"] = {5, m_meshObjects["floor"].indexCount, m_meshObjects["floor"].firstIndex, 
                                       m_meshObjects["floor"].vertexOffset, m_meshObjects["floor"].vertexCount, nullptr};

    m_meshObjects["shadowCube"] = {6, 36, 0, 0, 24, nullptr};
    m_meshObjects["shadowReflectedCube"] = {7, 36, 0, 0, 24, nullptr};

    // Initialize the view matrix
    glm::vec3 c_pos = { 3.0f, -10.0f, 4.0f };
    glm::vec3 c_at =  { 0.0f, 0.0f, 1.0f };
    glm::vec3 c_down =  { 0.0f, 0.0f, -1.0f };
    uBufVS.viewMatrix = glm::lookAtLH(c_pos, c_at, c_down);

    // Initialize the projection matrix by setting the frustum information
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)width/height, 0.01f, 100.0f);

    // Initialize the lighting parameters (directions and colors)
    uBufVS.lightDir = {-0.577f, -0.577f, 0.577f, 0.0f};
    uBufVS.lightColor = {0.9f, 0.9f, 0.9f, 1.0f};
}

VKStenciling::~VKStenciling()
{
    if (dynUBufVS.meshInfo)
        AlignedFree(dynUBufVS.meshInfo);
}

void VKStenciling::OnInit()
{
    InitVulkan();
    SetupPipeline();

    // Update buffer data (light direction and color, plus view and projection matrices)
    UpdateHostVisibleBufferData();
}

void VKStenciling::InitVulkan()
{
    CreateInstance();
    CreateSurface();
    CreateDevice(VK_QUEUE_GRAPHICS_BIT);
    GetDeviceQueue(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.FamilyIndex, m_vulkanParams.GraphicsQueue.Handle);
    CreateSwapchain(&m_width, &m_height, VKApplication::settings.vsync);
    CreateDepthStencilImage(m_width, m_height);
    CreateRenderPass();
    CreateFrameBuffers();
    AllocateCommandBuffers();
    CreateSynchronizationObjects();
}

void VKStenciling::SetupPipeline()
{
    CreateVertexBuffer();
    CreateHostVisibleBuffers();
    CreateHostVisibleDynamicBuffers();
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    AllocateDescriptorSets();
    CreatePipelineLayout();
    CreatePipelineObjects();

    m_initialized = true;
}

// Update frame-based values.
void VKStenciling::OnUpdate()
{
    m_timer.Tick(nullptr);
    
    // Update FPS and frame count.
    snprintf(m_lastFPS, (size_t)32, "%u fps", m_timer.GetFramesPerSecond());
    m_frameCounter++;

    // Update dynamic buffer data (world matrices and solid colors)
    UpdateHostVisibleDynamicBufferData();
}

// Render the scene.
void VKStenciling::OnRender()
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

void VKStenciling::OnDestroy()
{
    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Destroy vertex and index buffer objects and deallocate backing memory
    vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffer.VBbuffer, nullptr);
    vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffer.IBbuffer, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffer.VBmemory, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffer.IBmemory, nullptr);

    // Destroy\Unmap frame resources
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
    for (auto const& pl : m_sampleParams.GraphicsPipelines)
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

void VKStenciling::OnResize()
{
    // Recreate the projection matrix
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)m_width/m_height, 0.01f, 100.0f);

    // Update buffer data (light direction and color, and view and projection matrices)
    UpdateHostVisibleBufferData();
}

// Create vertex and index buffers describing all mesh geometries
void VKStenciling::CreateVertexBuffer()
{
    // While it's fine for an example application to request small individual memory allocations, that is not
    // what should be done a real-world application, where you should allocate large chunks of memory at once instead.

    //
    // Create the vertex and index buffers.
    //

    // Define the combined geometries in local space (Z points up in this case).
    //
    std::vector<Vertex> cubeVertices =
    {
        // Cube (24 vertices: 0-23)
        { {-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },
        { {1.0f, -1.0f, 1.0f},  {0.0f, 0.0f, 1.0f} },
        { {1.0f, 1.0f, 1.0f},   {0.0f, 0.0f, 1.0f} },
        { {-1.0f, 1.0f, 1.0f},  {0.0f, 0.0f, 1.0f} },

        { {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f} },
        { {-1.0f, 1.0f, -1.0f},  {0.0f, 0.0f, -1.0f} },
        { {1.0f, 1.0f, -1.0f},   {0.0f, 0.0f, -1.0f} },
        { {1.0f, -1.0f, -1.0f},  {0.0f, 0.0f, -1.0f} },

        { {-1.0f, -1.0f, 1.0f},  {-1.0f, 0.0f, 0.0f} },
        { {-1.0f, 1.0f, 1.0f},   {-1.0f, 0.0f, 0.0f} },
        { {-1.0f, 1.0f, -1.0f},  {-1.0f, 0.0f, 0.0f} },
        { {-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f} },

        { {1.0f, -1.0f, 1.0f},  {1.0f, 0.0f, 0.0f} },
        { {1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f} },
        { {1.0f, 1.0f, -1.0f},  {1.0f, 0.0f, 0.0f} },
        { {1.0f, 1.0f, 1.0f},   {1.0f, 0.0f, 0.0f} },

        { {-1.0f, 1.0f, 1.0f},  {0.0f, 1.0f, 0.0f} },
        { {1.0f, 1.0f, 1.0f},   {0.0f, 1.0f, 0.0f} },
        { {1.0f, 1.0f, -1.0f},  {0.0f, 1.0f, 0.0f} },
        { {-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f} },

        { {1.0f, -1.0f, 1.0f},   {0.0f, -1.0f, 0.0f} },
        { {-1.0f, -1.0f, 1.0f},  {0.0f, -1.0f, 0.0f} },
        { {-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f} },
        { {1.0f, -1.0f, -1.0f},  {0.0f, -1.0f, 0.0f} },

        // Floor (4 vertices: 24-27)
        { {-3.5f, -10.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
        { {7.5f, -10.0f, 0.0f},  {0.0f, 0.0f, 1.0f} },
        { {7.5f, 0.0f, 0.0f},    {0.0f, 0.0f, 1.0f} },
        { {-3.5f, 0.0f, 0.0f},   {0.0f, 0.0f, 1.0f} },

        // Wall (10 vertices: 28-37): we leave a gap in the middle for the mirror
        { {-3.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {-2.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {-2.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} },
        { {-3.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} },

        { {2.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {7.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {7.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} },
        { {2.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} },

        { {7.5f, 0.0f, 6.0f},  {0.0f, 0.0f, -1.0f} },
        { {-3.5f, 0.0f, 6.0f}, {0.0f, 0.0f, -1.0f} },

        // Mirror (4 vertices: 38-41)
        { {-2.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {2.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
        { {2.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} },
        { {-2.5f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f} }
    };
    size_t vertexBufferSize = static_cast<size_t>(cubeVertices.size()) * sizeof(Vertex);

    // The indices defining the two triangle for each quad in the combined geometry
    // The vertices of each triangle are selected in counter-clockwise order.
    std::vector<uint16_t> indexBuffer =
    {
        //
        // Cube (36 incides: 0-35)
        //
        0,1,2,
        0,2,3,

        4,5,6,
        4,6,7,

        8,9,10,
        8,10,11,

        12,13,14,
        12,14,15,

        16,17,18,
        16,18,19,

        20,21,22,
        20,22,23,

        // Floor (6 indices: 36-41)
        0, 1, 2,
        0, 2, 3,

        // Wall (18 indices: 42-59)
        0, 1, 2,
        0, 2, 3,

        4, 5, 6,
        4, 6, 7,

        3, 8, 9,
        3, 6, 8,

        // Mirror (6 indices: 60-65)
        0, 1, 2,
        0, 2, 3
    };
    size_t indexBufferSize = static_cast<size_t>(indexBuffer.size()) * sizeof(uint16_t);
    m_vertexindexBuffer.indexBufferCount = indexBuffer.size();

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
    vertexBufferInfo.size = vertexBufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;    
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &vertexBufferInfo, nullptr, &m_vertexindexBuffer.VBbuffer));

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the vertex buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_vertexindexBuffer.VBbuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_vertexindexBuffer.VBmemory));

    // Map the host-visible device memory and copy the vertex data. 
    // Once finished, we can unmap it since we no longer need to access the vertex buffer from the application.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, m_vertexindexBuffer.VBmemory, 0, memAlloc.allocationSize, 0, &data));
    memcpy(data, cubeVertices.data(), vertexBufferSize);
    vkUnmapMemory(m_vulkanParams.Device, m_vertexindexBuffer.VBmemory);

    // Bind the vertex buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, m_vertexindexBuffer.VBbuffer, m_vertexindexBuffer.VBmemory, 0));

    // Create the index buffer object
    VkBufferCreateInfo indexBufferInfo = {};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = vertexBufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;    
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &indexBufferInfo, nullptr, &m_vertexindexBuffer.IBbuffer));

    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_vertexindexBuffer.IBbuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_vertexindexBuffer.IBmemory));

    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, m_vertexindexBuffer.IBmemory, 0, memAlloc.allocationSize, 0, &data));
    memcpy(data, indexBuffer.data(), indexBufferSize);
    vkUnmapMemory(m_vulkanParams.Device, m_vertexindexBuffer.IBmemory);

    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, m_vertexindexBuffer.IBbuffer, m_vertexindexBuffer.IBmemory, 0));
}

void VKStenciling::CreateHostVisibleBuffers()
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

void VKStenciling::CreateHostVisibleDynamicBuffers()
{
    // Create the dynamic buffer that store the array of world matrices.
	// Uniform block alignment differs between GPUs.

	// Calculate required alignment based on minimum device offset alignment
	size_t minUBOAlignment = m_deviceProperties.limits.minUniformBufferOffsetAlignment;
	m_dynamicUBOAlignment = sizeof(MeshInfo); // 80 bytes
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

void VKStenciling::UpdateHostVisibleBufferData()
{
    // Update uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
        memcpy(m_sampleParams.FrameRes.HostVisibleBuffers[i].MappedMemory, &uBufVS, sizeof(uBufVS));
}

void VKStenciling::UpdateHostVisibleDynamicBufferData()
{
    const float rotationSpeed = 0.8f;

    // Update the rotation angle
    m_curRotationAngleRad += rotationSpeed * m_timer.GetElapsedSeconds();
    if (m_curRotationAngleRad >= glm::two_pi<float>())
    {
        m_curRotationAngleRad -= glm::two_pi<float>();
    }

    // Rotate the cube at the center of the scene around the z-axis
    m_meshObjects["cube"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                        (m_meshObjects["cube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    glm::mat4 transl = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -6.0f, 2.0f));
    glm::mat4 rotZTransl = glm::rotate(transl, m_curRotationAngleRad, glm::vec3(0.0f, 0.0f, 1.0f));
    m_meshObjects["cube"].meshInfo->worldMatrix = rotZTransl;

    // Set color of wall and floor
    m_meshObjects["floor"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo +
                                        (m_meshObjects["floor"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    m_meshObjects["floor"].meshInfo->worldMatrix = glm::identity<glm::mat4>();
    m_meshObjects["floor"].meshInfo->solidColor = { 1.0f, 0.9f, 0.7f, 1.0f };
    m_meshObjects["wall"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                        (m_meshObjects["wall"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    m_meshObjects["wall"].meshInfo->worldMatrix = glm::identity<glm::mat4>();
    m_meshObjects["wall"].meshInfo->solidColor = { 0.6f, 0.3f, 0.0f, 1.0f };

    // Set color of mirror
    m_meshObjects["mirror"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                            (m_meshObjects["mirror"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    m_meshObjects["mirror"].meshInfo->worldMatrix = glm::identity<glm::mat4>();
    m_meshObjects["mirror"].meshInfo->solidColor = { 0.5f, 1.0f, 1.0f, 0.15f };

    // Use the world matrix of the cube to reflect it with respect to the mirror plane
    m_meshObjects["reflectedCube"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                                    (m_meshObjects["reflectedCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    glm::vec4 mirrorPlane = {0.0f, 1.0f, 0.0f, 0.0f}; // xz-plane
    glm::mat4 R = MatrixReflect(mirrorPlane);
    m_meshObjects["reflectedCube"].meshInfo->worldMatrix = R * m_meshObjects["cube"].meshInfo->worldMatrix;

    // Use the world matrix of the floor to reflect it with respect to the mirror plane
    m_meshObjects["reflectedFloor"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                                    (m_meshObjects["reflectedFloor"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    m_meshObjects["reflectedFloor"].meshInfo->worldMatrix = R * m_meshObjects["floor"].meshInfo->worldMatrix;
    m_meshObjects["reflectedFloor"].meshInfo->solidColor = m_meshObjects["floor"].meshInfo->solidColor;

    // Use the world matrix of the cube to project it onto the floor with respect to the light source,
    // and raise it a little to prevent z-fighting.
    m_meshObjects["shadowCube"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                                (m_meshObjects["shadowCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    glm::vec4 shadowPlane = {0.0f, 0.0f, 1.0f, 0.0f}; // xy-plane
    glm::mat4 S = MatrixShadow(shadowPlane, uBufVS.lightDir);
    glm::mat4 T = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0.0f, 0.0f, 0.003f));
    m_meshObjects["shadowCube"].meshInfo->worldMatrix = T * S * m_meshObjects["cube"].meshInfo->worldMatrix;
    m_meshObjects["shadowCube"].meshInfo->solidColor = { 0.0f, 0.0f, 0.0f, 0.2f };

    // Use the world matrix of the shadow above to reflect it with respect to the mirror plane.
    m_meshObjects["shadowReflectedCube"].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                                        (m_meshObjects["shadowReflectedCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    m_meshObjects["shadowReflectedCube"].meshInfo->worldMatrix = R * m_meshObjects["shadowCube"].meshInfo->worldMatrix;
    m_meshObjects["shadowReflectedCube"].meshInfo->solidColor = m_meshObjects["shadowCube"].meshInfo->solidColor;

    // Update dynamic uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    memcpy(m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].MappedMemory,
           dynUBufVS.meshInfo, 
           m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].Size);
}

void VKStenciling::CreateDescriptorPool()
{
    //
    // To calculate the amount of memory required for a descriptor pool, the implementation needs to know
    // the max numbers of descriptor sets we will request from the pool, and the number of descriptors 
    // per type we will include in those descriptor sets.
    //

    // Describe the number of descriptors per type.
    // This sample uses two descriptor types (uniform buffer and dynamic uniform buffer)
    VkDescriptorPoolSize typeCounts[2];
    typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    typeCounts[0].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    typeCounts[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    typeCounts[1].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);

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

void VKStenciling::CreateDescriptorSetLayout()
{
    //
    // Create a Descriptor Set Layout to connect binding points (resource declarations)
    // in the shader code to descriptors within descriptor sets.
    //
    // Binding 0: Uniform buffer (vertex and fragment shader)
    VkDescriptorSetLayoutBinding layoutBinding[2] = {};
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // Binding 1: Dynamic uniform buffer (vertex and fragment shader)
    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = 2;
    descriptorLayout.pBindings = layoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vulkanParams.Device, &descriptorLayout, nullptr, &m_sampleParams.DescriptorSetLayout));
}

void VKStenciling::AllocateDescriptorSets()
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
        // the binding point associated with the descriptor in the descriptor set.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleParams.FrameRes.DescriptorSets[i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet[0].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the dynamic uniform buffer.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleParams.FrameRes.DescriptorSets[i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSet[1].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 2, writeDescriptorSet, 0, nullptr);
    }
}

void VKStenciling::CreatePipelineLayout()
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

void VKStenciling::CreatePipelineObjects()
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
    // These match the following shader layout (see vertex shader):
    //	layout (location = 0) in vec3 inPos;
    //	layout (location = 1) in vec3 inNormal;
    //
    // Attribute location 0: Position from vertex buffer at binding point 0
    vertexInputAttributs[0].binding = 0;
    vertexInputAttributs[0].location = 0;
    // Position attribute is three 32-bit signed (SFLOAT) floats (R32 G32 B32)
    vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[0].offset = offsetof(Vertex, position);
    // Attribute location 1: Normal from vertex buffer at binding point 0
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    // Normal attribute is three 32-bit signed (SFLOAT) floats (R32 G32 B32)
    vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, normal);
    
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
    dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE); // Stencil reference value will be set dinamically.
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
    VkShaderModule mainVS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/main.vert.spv");
    VkShaderModule lambertianFS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/lambertian.frag.spv");
    VkShaderModule solidFS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/solid.frag.spv");

    // This sample will only use two programmable stage: Vertex and Fragment shaders
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    
    // Vertex shader
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    // Load binary SPIR-V shader module
    shaderStages[0].module = mainVS;
    // Main entry point for the shader
    shaderStages[0].pName = "main";
    assert(shaderStages[0].module != VK_NULL_HANDLE);
    
    // Fragment shader
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Load binary SPIR-V shader module
    shaderStages[1].module = lambertianFS;
    // Main entry point for the shader
    shaderStages[1].pName = "main";
    assert(shaderStages[1].module != VK_NULL_HANDLE);

    //
    // Create the graphics pipelines used in this sample
    //
    
    //
    // Lambertian
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
    
    // Create a graphics pipeline for lambertian illumination
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["Lambertian"]));

    //
    // SolidColor
    //

    // Specify a fragment shader for shading using a solid color
	shaderStages[1].module = solidFS;
	// Create a graphics pipeline to draw using a solid color
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["SolidColor"]));

    //
    // Transparent
    //

    // Create a new blend attachment state for alpha blending
    blendAttachmentState[0].colorWriteMask = 0xf;
    blendAttachmentState[0].blendEnable = VK_TRUE;
    blendAttachmentState[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState[0].colorBlendOp = VK_BLEND_OP_ADD;
	// Create a graphics pipeline to draw using a solid color with blending enabled
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["Transparent"]));

    //
    // Stencil
    //

    // Disable both blending and writes to the render target
    blendAttachmentState[0].blendEnable = VK_FALSE;
    blendAttachmentState[0].colorWriteMask = 0x0;
    // Enable depth and stencil tests, while disabling writes to the depth image
    depthStencilState.stencilTestEnable = VK_TRUE;
    depthStencilState.depthWriteEnable = VK_FALSE;
    // A fragment on the front face of a primitive will ALWAYS pass the stencil test, and the value in
    // the corresponding texel of the stencil image will be REPLACEed with the stencil reference value
    // if the fragment also passes the depth test.
    depthStencilState.front.failOp = VK_STENCIL_OP_KEEP;
    depthStencilState.front.depthFailOp = VK_STENCIL_OP_KEEP;
    depthStencilState.front.passOp = VK_STENCIL_OP_REPLACE;
    depthStencilState.front.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilState.front.reference = 1;
    depthStencilState.front.compareMask = 0xff;
    depthStencilState.front.writeMask = 0xff;
    // Create a graphics pipeline for drawing on the stencil image (to create a mask)
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["Stencil"]));

    //
    // ReflectedLambertian
    //

    // Enable writes to the render target
    blendAttachmentState[0].colorWriteMask = 0xf;
    // Enable writes to the depth image
    depthStencilState.depthWriteEnable = VK_TRUE;
    // Both depth and stencil tests are enabled.
    // A fragment on the front face of a primitive will pass the stencil test if the value
    // of the corresponding texel of the stencil image is EQUAL to the stencil reference value.
    // The texel KEEPs its value if the fragment passes both depth and stencil tests.
    depthStencilState.front.passOp = VK_STENCIL_OP_KEEP;
    depthStencilState.front.compareOp = VK_COMPARE_OP_EQUAL;
    // The front face will be considered the side where the vertices are in clockwise order.
    rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    // Specify a fragment shader for shading using the semplified lambertian model.
	shaderStages[1].module = lambertianFS;
    // Create a graphics pipeline for drawing reflected, illuminated objects (using the stencil image as a mask)
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["ReflectedLambertian"]));

    //
    // ReflectedSolidColor
    //

    // Specify a fragment shader for shading using a solid color
	shaderStages[1].module = solidFS;
    // Create a graphics pipeline for drawing reflected, NON-illuminated objects (using the stencil image as a mask)
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["ReflectedSolidColor"]));

    //
    // Shadow
    //

    // Enable blending to implement alpha blending
    blendAttachmentState[0].blendEnable = VK_TRUE;
    // Both depth and stencil tests are enabled. To prevent double blending:
    // A fragment on the front face of a primitive will pass the stencil test if the value
    // of the corresponding texel of the stencil image is EQUAL to the stencil reference value.
    // The texel value is INCRemented if the fragment passes both depth and stencil tests.
    depthStencilState.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    // Create a graphics pipeline for drawing transparent objects projected onto other surfaces, like shadows.
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleParams.GraphicsPipelines["Shadow"]));

    // SPIR-V shader modules are no longer needed once the graphics pipeline has been created
    // since the SPIR-V modules are compiled during pipeline creation.
    vkDestroyShaderModule(m_vulkanParams.Device, mainVS, nullptr);
    vkDestroyShaderModule(m_vulkanParams.Device, lambertianFS, nullptr);
    vkDestroyShaderModule(m_vulkanParams.Device, solidFS, nullptr);
}

void VKStenciling::PopulateCommandBuffer(uint32_t currentImageIndex)
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

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], &cmdBufInfo));

    // Begin the render pass instance.
    // This will clear the color attachment.
    vkCmdBeginRenderPass(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Update dynamic viewport state
    VkViewport viewport = {};
    viewport.height = (float)m_height;
    viewport.width = (float)m_width;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &viewport);

    // Update dynamic scissor state
    VkRect2D scissor = {};
    scissor.extent.width = m_width;
    scissor.extent.height = m_height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &scissor);
    
    // Bind the vertex buffer (contains positions and colors)
    VkDeviceSize offsets[1] = { 0 };
    vkCmdBindVertexBuffers(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 0, 1, &m_vertexindexBuffer.VBbuffer, offsets);

    // Bind the index buffer
	vkCmdBindIndexBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], m_vertexindexBuffer.IBbuffer, 0, VK_INDEX_TYPE_UINT16);

    // Dynamic offset used to offset into the uniform buffer described by the dynamic uniform buffer and containing mesh information

    //
    // Cube
    //

    uint32_t dynamicOffset = m_meshObjects["cube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind the graphics pipeline for drawing with the semplified lambertian shading model
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        VK_PIPELINE_BIND_POINT_GRAPHICS, 
                        m_sampleParams.GraphicsPipelines["Lambertian"]);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Draw a cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], m_meshObjects["cube"].indexCount, 1, 0, 0, 0);

    //
    // Floor and Wall
    //

    // Bind the graphics pipeline for drawing opaque objects with a solid color
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["SolidColor"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["floor"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the floor
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["floor"].indexCount, 1, 
                        m_meshObjects["floor"].firstIndex, 
                        m_meshObjects["floor"].vertexOffset, 0);


    // Update dynamic offset
    dynamicOffset = m_meshObjects["wall"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the floor
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["wall"].indexCount, 1, 
                        m_meshObjects["wall"].firstIndex, 
                        m_meshObjects["wall"].vertexOffset, 0);

    //
    // Draw the mirror on the stencil image to create a mask
    //

    // Bind the graphics pipeline for drawing onto the stencil image
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["Stencil"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["mirror"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Set the stencil reference value to 1
    vkCmdSetStencilReference(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], VK_STENCIL_FACE_FRONT_BIT, 1);

    // Draw the mirror
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["mirror"].indexCount, 1, 
                        m_meshObjects["mirror"].firstIndex, 
                        m_meshObjects["mirror"].vertexOffset, 0);

    //
    // Reflected Cube
    //

    // Bind the graphics pipeline for drawing reflected objects using the semplified lambertian model
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["ReflectedLambertian"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["reflectedCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the reflected cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["reflectedCube"].indexCount, 1, 
                        m_meshObjects["reflectedCube"].firstIndex, 
                        m_meshObjects["reflectedCube"].vertexOffset, 0);

    //
    // Reflected opaque objects (in this case, the floor only)
    //

    // Bind the graphics pipeline for drawing reflected objects with a solid color
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["ReflectedSolidColor"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["reflectedFloor"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the reflected cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["reflectedFloor"].indexCount, 1, 
                        m_meshObjects["reflectedFloor"].firstIndex, 
                        m_meshObjects["reflectedFloor"].vertexOffset, 0);

    //
    // Shadow of the cube
    //

    // Bind the graphics pipeline for drawing shadows
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["Shadow"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["shadowCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Set the stencil reference value to 0
    vkCmdSetStencilReference(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], VK_STENCIL_FACE_FRONT_BIT, 0);

    // Draw the reflected cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["shadowCube"].indexCount, 1, 
                        m_meshObjects["shadowCube"].firstIndex, 
                        m_meshObjects["shadowCube"].vertexOffset, 0);

    //
    // Shadow of the reflected cube
    //

    // Update dynamic offset
    dynamicOffset = m_meshObjects["shadowReflectedCube"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Set the stencil reference value to 1
    vkCmdSetStencilReference(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], VK_STENCIL_FACE_FRONT_BIT, 1);

    // Draw the reflected cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["shadowReflectedCube"].indexCount, 1, 
                        m_meshObjects["shadowReflectedCube"].firstIndex, 
                        m_meshObjects["shadowReflectedCube"].vertexOffset, 0);

    //
    // Mirror
    //
    
    // Bind the graphics pipeline for drawing transparent objects
    vkCmdBindPipeline(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    m_sampleParams.GraphicsPipelines["Transparent"]);

    // Update dynamic offset
    dynamicOffset = m_meshObjects["mirror"].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor sets for drawing a mesh using a dynamic offset
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[m_frameIndex], 
                            1, &dynamicOffset);

    // Set the stencil reference value to 0
    vkCmdSetStencilReference(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], VK_STENCIL_FACE_FRONT_BIT, 0);

    // Draw the reflected cube
    vkCmdDrawIndexed(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex], 
                        m_meshObjects["mirror"].indexCount, 1, 
                        m_meshObjects["mirror"].firstIndex, 
                        m_meshObjects["mirror"].vertexOffset, 0);

    // Ending the render pass will add an implicit barrier, transitioning the frame buffer color attachment to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex]);
    
     VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleParams.FrameRes.GraphicsCommandBuffers[m_frameIndex]));
}

void VKStenciling::SubmitCommandBuffer()
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

void VKStenciling::PresentImage(uint32_t currentImageIndex)
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