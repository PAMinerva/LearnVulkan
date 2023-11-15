#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKComputeParticles.hpp"
#include "VKDebug.hpp"
#include "MathHelper.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/ext/scalar_constants.hpp"

#define MESH_PARTICLES "MeshParticles"
#define DESC_SET_GRAPH_COMP "DescSetGraphComp"
#define PIPELINE_RENDER "PipelineRender"
#define PIPELINE_COMPUTE "PipelineCompute"
#define SEMAPHORE_GRAPH_COMPLETE "SemaphoreGraphicsComplete"
#define SEMAPHORE_COMP_COMPLETE "SemaphoreComputeComplete"

VKComputeParticles::VKComputeParticles(uint32_t width, uint32_t height, std::string name) :
VKSample(width, height, name),
m_dynamicUBOAlignment(0)
{
    // Initialize mesh objects
    m_meshObjects[MESH_PARTICLES] = {};

    // Initialize the pointer to the memory region that will store the array of mesh info.
    dynUBufVS.meshInfo = nullptr;

    // Initialize the view matrix
    glm::vec3 c_pos = { 0.0f, -50.0f, 50.0f }; // { 0.0f, -30.0f, 50.0f } for the interstellar travel effect
    glm::vec3 c_at =  { 0.0f, 0.0f, 0.0f };
    glm::vec3 c_down =  { 0.0f, 0.0f, -1.0f };
    uBufVS.viewMatrix = glm::lookAtLH(c_pos, c_at, c_down);

    // Save camera position in world coordinates
    uBufVS.cameraPos = c_pos;

    // Initialize the projection matrix by setting the frustum information
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)width/height, 0.01f, 100.0f);
}

VKComputeParticles::~VKComputeParticles()
{
    if (dynUBufVS.meshInfo)
        AlignedFree(dynUBufVS.meshInfo);
}

void VKComputeParticles::OnInit()
{
    InitVulkan();
    SetupPipeline();
}

void VKComputeParticles::InitVulkan()
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

void VKComputeParticles::SetupPipeline()
{
    //CreateVertexBuffer();
    CreateStagingBuffer();
    CreateStorageBuffers();
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

void VKComputeParticles::EnableInstanceExtensions(std::vector<const char*>& instanceExtensions)
{ }

void VKComputeParticles::EnableDeviceExtensions(std::vector<const char*>& deviceExtensions)
{ }

void VKComputeParticles::EnableFeatures(VkPhysicalDeviceFeatures& features)
{ 
    if (m_deviceFeatures.geometryShader)
    {
        m_vulkanParams.EnabledFeatures.geometryShader = VK_TRUE;
    }
    else
    {
        assert(!"Selected device does not support geometry shaders!");
    }
}

// Update frame-based values.
void VKComputeParticles::OnUpdate()
{
    m_timer.Tick(nullptr);
    
    // Update FPS and frame count.
    snprintf(m_lastFPS, (size_t)32, "%u fps", m_timer.GetFramesPerSecond());
    m_frameCounter++;

    // Update buffer data (delta time and view/projection matrices)
    UpdateHostVisibleBufferData();

    // Update dynamic buffer data (world matrices and solid colors)
    UpdateHostVisibleDynamicBufferData();
}

// Render the scene.
void VKComputeParticles::OnRender()
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

void VKComputeParticles::OnDestroy()
{
    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Destroy vertex and index buffer objects and deallocate backing memory
/*  vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffers.VBbuffer, nullptr);
    vkDestroyBuffer(m_vulkanParams.Device, m_vertexindexBuffers.IBbuffer, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffers.VBmemory, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertexindexBuffers.IBmemory, nullptr); */

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
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.ImageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.FrameRes.RenderingCompleteSemaphores[i], nullptr);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_COMP_COMPLETE][i], nullptr);
        vkDestroySemaphore(m_vulkanParams.Device, m_sampleComputeParams.FrameRes.Semaphores[SEMAPHORE_GRAPH_COMPLETE][i], nullptr);

        // Destroy storage buffers
        vkDestroyBuffer(m_vulkanParams.Device, m_storageBuffers[i].StorageBuffer.Handle, nullptr);
        vkFreeMemory(m_vulkanParams.Device, m_storageBuffers[i].StorageBuffer.Memory, nullptr);
    }

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

void VKComputeParticles::OnResize()
{
    // Recreate the projection matrix
    uBufVS.projectionMatrix = glm::perspectiveLH(glm::quarter_pi<float>(), (float)m_width/m_height, 0.01f, 100.0f);

    // Update buffer data (light direction and color, and view and projection matrices)
    UpdateHostVisibleBufferData();
}

void VKComputeParticles::CreateHostVisibleBuffers()
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

void VKComputeParticles::CreateHostVisibleDynamicBuffers()
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
        // range is the static size used for all dynamic offsets; describe a region of sizeof(MeshInfo) bytes in the buffer, depending on the dynamic offset provided
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.buffer = m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Handle;
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.offset = 0;
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor.range = sizeof(MeshInfo);

        // Save buffer size for later use
        m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Size = dynBufferSize;
    }    
}

void VKComputeParticles::CreateStagingBuffer()
{
    // Create a buffer object
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = StorageBuf::BufferLenght * StorageBuf::BufferElementSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &bufferInfo, nullptr, &m_stagingBuffer.Handle));

    // Used to request an allocation of a specific size from a certain memory type.
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;

    // Request a memory allocation from coherent, host-visible device memory that is large 
    // enough to hold the staging buffer.
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes performed by the host (application)
    // will be directly visible to the device without requiring the explicit flushing of cached memory.
    vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_stagingBuffer.Handle, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_deviceMemoryProperties);
    VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_stagingBuffer.Memory));

    // Bind the buffer object to the backing host-visible device memory just allocated.
    VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, 
                                        m_stagingBuffer.Handle, 
                                        m_stagingBuffer.Memory, 0));

    // Map the host-visible device memory just allocated.
    VK_CHECK_RESULT(vkMapMemory(m_vulkanParams.Device, 
                                m_stagingBuffer.Memory, 
                                0, memAlloc.allocationSize, 
                                0, &m_stagingBuffer.MappedMemory));

    // Define a grid of 9 * 9 particles lying in the XY plane of the local space inside the square [-20, 20] x [-20, 20].
    for (int i = 0; i < 81; ++i)
    {
        Vertex v;
        v.position = glm::vec4{ i % 9 * 5.0f - 20.0f, i / 9 * 5.0f - 20.0f, 0.0f, 1.0 };
        v.size = { 0.05f, 5.0f }; // { 0.3f, 5.0f } for the interstellar travel effect
        v.speed = {static_cast<float>(100 + rand() % 200) };
        m_particles.push_back(v);
    }

    m_meshObjects[MESH_PARTICLES].vertexCount = static_cast<uint32_t>(m_particles.size());

    // Copy particle data to the staging buffer
    memcpy(m_stagingBuffer.MappedMemory, m_particles.data(), m_particles.size() * sizeof(Vertex));

    // Unmap staging buffer
    vkUnmapMemory(m_vulkanParams.Device, m_stagingBuffer.Memory);
}

void VKComputeParticles::CreateStorageBuffers()
{
    m_storageBuffers.resize(MAX_FRAME_LAG);

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Create a buffer to be used as storage buffer (in CS) and vertex buffer (in VS)
        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = StorageBuf::BufferLenght * StorageBuf::BufferElementSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(m_vulkanParams.Device, &bufferCreateInfo, nullptr, &m_storageBuffers[i].StorageBuffer.Handle));

        // Store information needed to write\update the corresponding descriptor (storage buffer) in the descriptor set later.
        m_storageBuffers[i].StorageBuffer.Descriptor.buffer = m_storageBuffers[i].StorageBuffer.Handle;
        m_storageBuffers[i].StorageBuffer.Descriptor.offset = 0;
        m_storageBuffers[i].StorageBuffer.Descriptor.range = StorageBuf::BufferLenght * StorageBuf::BufferElementSize;

        // Save buffer size for later use
        m_storageBuffers[i].StorageBuffer.Size = StorageBuf::BufferLenght * StorageBuf::BufferElementSize;

        // Used to request an allocation of a specific size from a certain memory type.
        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Request a memory allocation from local device memory that is large 
        // enough to hold the storage buffer.
        vkGetBufferMemoryRequirements(m_vulkanParams.Device, m_storageBuffers[i].StorageBuffer.Handle, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_deviceMemoryProperties);
        VK_CHECK_RESULT(vkAllocateMemory(m_vulkanParams.Device, &memAlloc, nullptr, &m_storageBuffers[i].StorageBuffer.Memory));

        // Bind the buffer object to the backing local device memory just allocated.
        VK_CHECK_RESULT(vkBindBufferMemory(m_vulkanParams.Device, 
                                            m_storageBuffers[i].StorageBuffer.Handle, 
                                            m_storageBuffers[i].StorageBuffer.Memory, 0));
    }

    //
    // Copy staging buffer to storage buffers in device local memory
    //

    VkCommandBufferBeginInfo cmdBufferInfo = {};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[0], &cmdBufferInfo);

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        VkBufferCopy copyRegion{};
        copyRegion.size = StorageBuf::BufferLenght * StorageBuf::BufferElementSize;;
        vkCmdCopyBuffer(m_sampleParams.FrameRes.CommandBuffers[0], m_stagingBuffer.Handle, m_storageBuffers[i].StorageBuffer.Handle, 1, &copyRegion);
    }

    // Flush the command buffer
    FlushInitCommandBuffer(m_vulkanParams.Device, m_vulkanParams.GraphicsQueue.Handle, m_sampleParams.FrameRes.CommandBuffers[0], m_sampleParams.FrameRes.Fences[0]);

    // Free staging buffer
    vkDestroyBuffer(m_vulkanParams.Device, m_stagingBuffer.Handle, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_stagingBuffer.Memory, nullptr);
}

void VKComputeParticles::UpdateHostVisibleBufferData()
{
    // Update time
    uBufVS.deltaTime = static_cast<float>(m_timer.GetElapsedSeconds());

    // Update uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
        memcpy(m_sampleParams.FrameRes.HostVisibleBuffers[i].MappedMemory, &uBufVS, sizeof(uBufVS));
}

void VKComputeParticles::UpdateHostVisibleDynamicBufferData()
{
    // Set an identity matrix as world matrix
    m_meshObjects[MESH_PARTICLES].meshInfo = (MeshInfo*)((uint64_t)dynUBufVS.meshInfo + 
                                        (m_meshObjects[MESH_PARTICLES].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment)));
    
    m_meshObjects[MESH_PARTICLES].meshInfo->worldMatrix = glm::identity<glm::mat4>();

    // Set a half-transparent white color
    m_meshObjects[MESH_PARTICLES].meshInfo->solidColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);

    // Update dynamic uniform buffer data
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    memcpy(m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].MappedMemory,
           dynUBufVS.meshInfo, 
           m_sampleParams.FrameRes.HostVisibleDynamicBuffers[m_frameIndex].Size);
}

void VKComputeParticles::CreateDescriptorPool()
{
    //
    // To calculate the amount of memory required for a descriptor pool, the implementation needs to know
    // the max numbers of descriptor sets we will request from the pool, and the number of descriptors 
    // per type we will include in those descriptor sets.
    //

    // Describe the number of descriptors per type.
    // This sample uses four descriptor types (uniform buffer, dynamic uniform buffer and two storage buffers)
    VkDescriptorPoolSize typeCounts[3];
    typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    typeCounts[0].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    typeCounts[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    typeCounts[1].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    typeCounts[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    typeCounts[2].descriptorCount = static_cast<uint32_t>(MAX_FRAME_LAG) * 2;  // Previous and current storage buffers

    // Create a global descriptor pool
    // All descriptors set used in this sample will be allocated from this pool
    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.pNext = nullptr;
    descriptorPoolInfo.poolSizeCount = 3;
    descriptorPoolInfo.pPoolSizes = typeCounts;
    // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
    descriptorPoolInfo.maxSets = static_cast<uint32_t>(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkCreateDescriptorPool(m_vulkanParams.Device, &descriptorPoolInfo, nullptr, &m_sampleParams.DescriptorPool));
}

void VKComputeParticles::CreateDescriptorSetLayout()
{
    //
    // Create a Descriptor Set Layout to connect binding points (resource declarations)
    // in the shader code to descriptors within descriptor sets.
    //

    VkDescriptorSetLayoutBinding layoutBinding[4] = {};

    // Binding 0: Uniform buffer (accessed by CS and GS)
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // Binding 1: Dynamic uniform buffer (accessed by GS and FS)
    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    // Binding 2: Previous Storage Buffer (accessed by CS)
    layoutBinding[2].binding = 2;
    layoutBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutBinding[2].descriptorCount = 1;
    layoutBinding[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layoutBinding[2].pImmutableSamplers = nullptr;

    // Binding 3: Current Storage Buffer (accessed by CS)
    layoutBinding[3].binding = 3;
    layoutBinding[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutBinding[3].descriptorCount = 1;
    layoutBinding[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layoutBinding[3].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = 4;
    descriptorLayout.pBindings = layoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vulkanParams.Device, &descriptorLayout, nullptr, &m_sampleParams.DescriptorSetLayout));
}

void VKComputeParticles::AllocateDescriptorSets()
{
    // Allocate MAX_FRAME_LAG descriptor sets from the global descriptor pool.
    // Use the descriptor set layout to calculate the amount on memory required to store the descriptor sets.
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_sampleParams.DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAME_LAG);
    std::vector<VkDescriptorSetLayout> DescriptorSetLayouts(MAX_FRAME_LAG, m_sampleParams.DescriptorSetLayout);
    allocInfo.pSetLayouts = DescriptorSetLayouts.data();

    m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP].resize(MAX_FRAME_LAG);

    VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vulkanParams.Device, &allocInfo, m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP].data()));

    //
    // Write the descriptors updating the corresponding descriptor sets.
    // For every binding point used in a shader code there needs to be at least a descriptor 
    // in a descriptor set matching that binding point.
    //
    VkWriteDescriptorSet writeDescriptorSet[4] = {};

    for (size_t i = 0; i < MAX_FRAME_LAG; i++)
    {
        // Write the descriptor of the uniform buffer.
        // We need to pass the descriptor set where it is store and 
        // the binding point associated with the descriptor in the descriptor set.
        writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[0].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][i];
        writeDescriptorSet[0].descriptorCount = 1;
        writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet[0].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleBuffers[i].Descriptor;
        writeDescriptorSet[0].dstBinding = 0;

        // Write the descriptor of the dynamic uniform buffer.
        writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[1].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][i];
        writeDescriptorSet[1].descriptorCount = 1;
        writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSet[1].pBufferInfo = &m_sampleParams.FrameRes.HostVisibleDynamicBuffers[i].Descriptor;
        writeDescriptorSet[1].dstBinding = 1;

        // Write the descriptor of the previous storage buffer.
        writeDescriptorSet[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[2].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][i];
        writeDescriptorSet[2].descriptorCount = 1;
        writeDescriptorSet[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet[2].pBufferInfo = &m_storageBuffers[(i - 1) % MAX_FRAME_LAG].StorageBuffer.Descriptor; // if (i == 0) then -1 % 2 = 1, since -1 = 2*(-1) + 1 <-- remainder
        writeDescriptorSet[2].dstBinding = 2;

        // Write the descriptor of the current storage buffer.
        writeDescriptorSet[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet[3].dstSet = m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][i];
        writeDescriptorSet[3].descriptorCount = 1;
        writeDescriptorSet[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet[3].pBufferInfo = &m_storageBuffers[i].StorageBuffer.Descriptor;
        writeDescriptorSet[3].dstBinding = 3;

        vkUpdateDescriptorSets(m_vulkanParams.Device, 4, writeDescriptorSet, 0, nullptr);
    }
}

void VKComputeParticles::CreatePipelineLayout()
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

void VKComputeParticles::CreatePipelineObjects()
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
    std::array<VkVertexInputAttributeDescription, 3> vertexInputAttributs;
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
    // Attribute location 1: Size from vertex buffer at binding point 0
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    // Size attribute is two 32-bit signed (SFLOAT) floats (R32 G32)
    vertexInputAttributs[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, size);
    // Attribute location 2: Speed from vertex buffer at binding point 0
    vertexInputAttributs[2].binding = 0;
    vertexInputAttributs[2].location = 2;
    // Speed attribute is a 32-bit signed (SFLOAT) float (R32)
    vertexInputAttributs[2].format = VK_FORMAT_R32_SFLOAT;
    vertexInputAttributs[2].offset = offsetof(Vertex, speed);
    
    // Vertex input state used for pipeline creation.
    // The Vulkan specification uses it to specify the input of the entire pipeline, 
    // but since the first stage is almost always the input assembler, we can consider it as 
    // part of the input assembler state.
    VkPipelineVertexInputStateCreateInfo vertexInputState = {};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.vertexBindingDescriptionCount = 1;
    vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputState.vertexAttributeDescriptionCount = 3;
    vertexInputState.pVertexAttributeDescriptions = vertexInputAttributs.data();
    
    // Input assembly state describes how primitives are assembled by the input assembler.
    // This pipeline will assemble vertex data as a triangle lists.
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    
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
    VkShaderModule renderGS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/render.geom.spv");
    VkShaderModule renderFS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/render.frag.spv");


    // This sample will use three programmable stage: Vertex, Geometry and Fragment shaders
    std::array<VkPipelineShaderStageCreateInfo, 3> shaderStages{};
    
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

    // Geometry shader
    shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // Set pipeline stage for this shader
    shaderStages[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    // Load binary SPIR-V shader module
    shaderStages[2].module = renderGS;
    // Main entry point for the shader
    shaderStages[2].pName = "main";
    assert(shaderStages[2].module != VK_NULL_HANDLE);

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

    // Enable alpha blending
    blendAttachmentState[0].blendEnable = VK_TRUE;
    blendAttachmentState[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState[0].colorBlendOp = VK_BLEND_OP_ADD;
    
    // Assign the pipeline states to the pipeline creation info structure
    pipelineCreateInfo.pVertexInputState = &vertexInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    
    // Create a graphics pipeline for drawing using a solid color
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
    vkDestroyShaderModule(m_vulkanParams.Device, renderGS, nullptr);
    vkDestroyShaderModule(m_vulkanParams.Device, renderFS, nullptr);
}

void VKComputeParticles::PrepareCompute()
{
    //
    // Get a compute queue from the same queue family used for recording graphics commands
    //

    // We already checked that the queue family used for graphics also supports compute work
    m_vulkanParams.ComputeQueue.FamilyIndex = m_vulkanParams.GraphicsQueue.FamilyIndex;

    // Get a compute queue from the device
    vkGetDeviceQueue(m_vulkanParams.Device, m_vulkanParams.ComputeQueue.FamilyIndex, 0, &m_vulkanParams.ComputeQueue.Handle);

    //
    // Shaders
    //

    VkShaderModule luminanceCS = LoadSPIRVShaderModule(m_vulkanParams.Device, GetAssetsPath() + "/data/shaders/particle.comp.spv");

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
    pipelineCreateInfo.layout = m_sampleParams.PipelineLayout;    
    // Set pipeline shader stage
    pipelineCreateInfo.stage = shaderStage;
    
    // Create a compute pipeline for computing the luminance of the input texture
    VK_CHECK_RESULT(vkCreateComputePipelines(m_vulkanParams.Device, 
                                              VK_NULL_HANDLE, 1, 
                                              &pipelineCreateInfo, nullptr, 
                                              &m_sampleComputeParams.Pipelines[PIPELINE_COMPUTE]));

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

void VKComputeParticles::PopulateComputeCommandBuffer()
{
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], &cmdBufInfo));

    // Set a memory barrier for the current storage buffer between VS and CS
    SetBufferMemoryBarrier(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex],
                           m_storageBuffers[m_frameIndex].StorageBuffer.Handle, VK_WHOLE_SIZE, 0,
                           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Bind the compute pipeline to a compute bind point of the command buffer
    vkCmdBindPipeline(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 
                        VK_PIPELINE_BIND_POINT_COMPUTE, 
                        m_sampleComputeParams.Pipelines[PIPELINE_COMPUTE]);
    
    // Dynamic offset used to offset into the uniform buffer described by the dynamic uniform buffer and containing mesh information
    uint32_t dynamicOffset = m_meshObjects[MESH_PARTICLES].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind descriptor set
    vkCmdBindDescriptorSets(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_COMPUTE, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][m_frameIndex], 
                            1, &dynamicOffset);

    // Dispatch compute work
    vkCmdDispatch(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex], 1, 1, 1);

    VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleComputeParams.FrameRes.CommandBuffers[m_frameIndex]));
}

void VKComputeParticles::PopulateCommandBuffer(uint32_t currentImageIndex)
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

    // Set a memory barrier for the current storage buffer between CS and VS
    SetBufferMemoryBarrier(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex],
                           m_storageBuffers[m_frameIndex].StorageBuffer.Handle, VK_WHOLE_SIZE, 0,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    // Begin the render pass instance.
    // This will clear the color attachment.
    vkCmdBeginRenderPass(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Update dynamic viewport state
    VkViewport viewport = {};
    viewport.height = (float)m_height;
    viewport.width = (float)m_width;
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
    vkCmdBindVertexBuffers(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 0, 1, &m_storageBuffers[m_frameIndex].StorageBuffer.Handle, offsets);

    // Dynamic offset used to offset into the uniform buffer described by the dynamic uniform buffer and containing mesh information
    uint32_t dynamicOffset = m_meshObjects[MESH_PARTICLES].dynIndex * static_cast<uint32_t>(m_dynamicUBOAlignment);

    // Bind the graphics pipeline
    vkCmdBindPipeline(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 
                        VK_PIPELINE_BIND_POINT_GRAPHICS, 
                        m_sampleParams.Pipelines[PIPELINE_RENDER]);

    // Bind descriptor set
    vkCmdBindDescriptorSets(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            m_sampleParams.PipelineLayout, 
                            0, 1, 
                            &m_sampleParams.FrameRes.DescriptorSets[DESC_SET_GRAPH_COMP][m_frameIndex], 
                            1, &dynamicOffset);

    // Draw the raindrops
    vkCmdDraw(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex], m_meshObjects[MESH_PARTICLES].vertexCount, 1, 0, 0);

    // Ending the render pass will add an implicit barrier, transitioning the frame buffer color attachment to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex]);
    
    VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleParams.FrameRes.CommandBuffers[m_frameIndex]));
}

void VKComputeParticles::SubmitCommandBuffer()
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

void VKComputeParticles::SubmitComputeCommandBuffer()
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

void VKComputeParticles::PresentImage(uint32_t currentImageIndex)
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
