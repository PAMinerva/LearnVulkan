#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKHelloSCBs.hpp"
#include "VKDebug.h"

VKHelloSCB::VKHelloSCB(uint32_t width, uint32_t height, std::string name) :
VKSample(width, height, name)
{
}

void VKHelloSCB::OnInit()
{
    InitVulkan();
    SetupPipeline();
}

void VKHelloSCB::InitVulkan()
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

void VKHelloSCB::SetupPipeline()
{
    CreateVertexBuffer();
    CreatePipelineLayout();
    CreatePipelineObjects();
    AllocateSCBs();
    PopulateSCBs();
    m_initialized = true;
}

void VKHelloSCB::AllocateSCBs()
{
    // Allocates as many SCBs as framebuffer objects
    m_secondaryCommandBuffers.resize(m_sampleParams.Framebuffers.size());

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = m_sampleParams.GraphicsCommandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    commandBufferAllocateInfo.commandBufferCount = static_cast<uint32_t>(m_sampleParams.Framebuffers.size());

    VK_CHECK_RESULT(vkAllocateCommandBuffers(m_vulkanParams.Device, &commandBufferAllocateInfo, m_secondaryCommandBuffers.data()));
}

void VKHelloSCB::PopulateSCBs()
{
    // Secondary CBs will be executed by primary CBs inside a specific subpass of a render pass instance.
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.pNext = nullptr;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

    // Set the render pass object defining what render pass instances the SCBs will be compatible with.
    // Specifying the exact framebuffer that the SCBs will be executed with may result in better performance at command buffer execution time.
    VkCommandBufferInheritanceInfo inheritanceInfo = {};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass = m_sampleParams.RenderPass;
    inheritanceInfo.subpass = 0;

    // Record SCBs
    for (uint32_t i = 0; i < m_sampleParams.Framebuffers.size(); i++)
    {
        inheritanceInfo.framebuffer = m_sampleParams.Framebuffers[i];
        cmdBufInfo.pInheritanceInfo = &inheritanceInfo;

        // Begin secondary command buffer recording
        VK_CHECK_RESULT(vkBeginCommandBuffer(m_secondaryCommandBuffers[i], &cmdBufInfo));

        // Update dynamic viewport state
        VkViewport viewport = {};
        viewport.height = (float)m_height;
        viewport.width = (float)m_width;
        viewport.minDepth = (float)0.0f;
        viewport.maxDepth = (float)1.0f;
        vkCmdSetViewport(m_secondaryCommandBuffers[i], 0, 1, &viewport);

        // Update dynamic scissor state
        VkRect2D scissor = {};
        scissor.extent.width = m_width;
        scissor.extent.height = m_height;
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        vkCmdSetScissor(m_secondaryCommandBuffers[i], 0, 1, &scissor);

        // Bind the graphics pipeline.
        // The pipeline object contains all states of the graphics pipeline, 
        // binding it will set all the states specified at pipeline creation time
        vkCmdBindPipeline(m_secondaryCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_sampleParams.GraphicsPipeline);
        
        // Bind triangle vertex buffer (contains position and colors)
        VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(m_secondaryCommandBuffers[i], 0, 1, &m_vertices.buffer, offsets);
        
        // Draw triangle
        vkCmdDraw(m_secondaryCommandBuffers[i], 3, 1, 0, 0);

        // End secondary command buffer recording
        VK_CHECK_RESULT(vkEndCommandBuffer(m_secondaryCommandBuffers[i]));
    }
}

// Update frame-based values.
void VKHelloSCB::OnUpdate()
{
    m_timer.Tick(nullptr);
    
    // Update FPS and frame count.
    snprintf(m_lastFPS, (size_t)32, "%u fps", m_timer.GetFramesPerSecond());
    m_frameCounter++;
}

// Render the scene.
void VKHelloSCB::OnRender()
{
    // Get the index of the next available image in the swap chain
    uint32_t imageIndex;
    VkResult acquire = vkAcquireNextImageKHR(m_vulkanParams.Device, m_vulkanParams.SwapChain.Handle, UINT64_MAX, m_sampleParams.ImageAvailableSemaphore, nullptr, &imageIndex);
    if (!((acquire == VK_SUCCESS) || (acquire == VK_SUBOPTIMAL_KHR)))
    {
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
            WindowResize(m_width, m_height);
        else
            VK_CHECK_RESULT(acquire);
    }

    PopulateCommandBuffer(m_commandBufferIndex, imageIndex);

    SubmitCommandBuffer(m_commandBufferIndex);

    PresentImage(imageIndex);

    // WAITING FOR THE GPU TO COMPLETE THE FRAME BEFORE CONTINUING IS NOT BEST PRACTICE.
    // vkQueueWaitIdle is used for simplicity.
    // (so that we can reuse the command buffer indexed with m_commandBufferIndex)
    VK_CHECK_RESULT(vkQueueWaitIdle(m_vulkanParams.GraphicsQueue.Handle));

    // Update command buffer index
    m_commandBufferIndex = (m_commandBufferIndex + 1) % m_commandBufferCount;
}

void VKHelloSCB::OnDestroy()
{
    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Destroy vertex buffer object and deallocate backing memory
    vkDestroyBuffer(m_vulkanParams.Device, m_vertices.buffer, nullptr);
    vkFreeMemory(m_vulkanParams.Device, m_vertices.memory, nullptr);

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
                         static_cast<uint32_t>(m_sampleParams.GraphicsCommandBuffers.size()), 
                         m_sampleParams.GraphicsCommandBuffers.data());

    // Free allocated secondary command buffers
    vkFreeCommandBuffers(m_vulkanParams.Device, 
                         m_sampleParams.GraphicsCommandPool,
                         static_cast<uint32_t>(m_secondaryCommandBuffers.size()), 
                         m_secondaryCommandBuffers.data());

    vkDestroyRenderPass(m_vulkanParams.Device, m_sampleParams.RenderPass, NULL);

    // Destroy semaphores
    vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.ImageAvailableSemaphore, NULL);
    vkDestroySemaphore(m_vulkanParams.Device, m_sampleParams.RenderingFinishedSemaphore, NULL);

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
void VKHelloSCB::CreateVertexBuffer()
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
    std::vector<Vertex> vertexBuffer =
    {
        { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },     // v0 (red)
    	{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },  // v1 (green)
        { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }    // v2 (blue)
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

void VKHelloSCB::CreatePipelineLayout()
{
    //
    // If you have pipeline stages that use shader resources, 
    // you must first create a Descriptor Set Layout to connect resource declarations
    // to descriptors in memory (within descriptor sets).
    // In this case, since no stage accesses resources stored in device memory, 
    // we don't need a descriptor set layout to create a pipeline layout object.
    //
    
    // Create a pipeline layout that will be used to create one or more pipeline objects.
    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
    pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineLayoutCreateInfo.pNext = nullptr;
    pPipelineLayoutCreateInfo.setLayoutCount = 0;
    pPipelineLayoutCreateInfo.pSetLayouts = VK_NULL_HANDLE;
    
    VK_CHECK_RESULT(vkCreatePipelineLayout(m_vulkanParams.Device, &pPipelineLayoutCreateInfo, nullptr, &m_sampleParams.PipelineLayout));
}

void VKHelloSCB::CreatePipelineObjects()
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
    //	layout (location = 1) in vec3 inColor;
    //
    // Attribute location 0: Position from vertex buffer at binding point 0
    vertexInputAttributs[0].binding = 0;
    vertexInputAttributs[0].location = 0;
    // Position attribute is three 32-bit signed (SFLOAT) floats (R32 G32 B32)
    vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[0].offset = offsetof(Vertex, position);
    // Attribute location 1: Color from vertex buffer at binding point 0
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    // Color attribute is four 32-bit signed (SFLOAT) floats (R32 G32 B32 A32)
    vertexInputAttributs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, color);
    
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

void VKHelloSCB::PopulateCommandBuffer(uint32_t currentBufferIndex, uint32_t currentIndexImage)
{
    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.pNext = nullptr;
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // We use a single color attachment that is cleared at the start of the subpass.
    VkClearValue clearValues[1];
    clearValues[0].color = { { 0.2f, 0.5f, 1.0f, 1.0f } };

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.pNext = nullptr;
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
    renderPassBeginInfo.framebuffer = m_sampleParams.Framebuffers[currentIndexImage];

    VK_CHECK_RESULT(vkBeginCommandBuffer(m_sampleParams.GraphicsCommandBuffers[currentBufferIndex], &cmdBufInfo));

    // Begin a render pass instance specifying that the contents are recorded in secondary command buffers.
    // vkCmdExecuteCommands is the only valid command on the primary command buffer.
    // This will clear the color attachment.
    vkCmdBeginRenderPass(m_sampleParams.GraphicsCommandBuffers[currentBufferIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    // Execute render commands from the secondary command buffer
    vkCmdExecuteCommands(m_sampleParams.GraphicsCommandBuffers[currentBufferIndex], 1, &m_secondaryCommandBuffers[currentIndexImage]);

    // Ending the render pass will add an implicit barrier, transitioning the frame buffer color attachment to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system
    vkCmdEndRenderPass(m_sampleParams.GraphicsCommandBuffers[currentBufferIndex]);
    
     VK_CHECK_RESULT(vkEndCommandBuffer(m_sampleParams.GraphicsCommandBuffers[currentBufferIndex]));
}

void VKHelloSCB::SubmitCommandBuffer(uint32_t currentBufferIndex)
{
    // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // The submit info structure specifies a command buffer queue submission batch
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &waitStageMask;                                           // Pointer to the list of pipeline stages that the semaphore waits will occur at
    submitInfo.waitSemaphoreCount = 1;                                                       // One wait semaphore
    submitInfo.signalSemaphoreCount = 1;                                                     // One signal semaphore
    submitInfo.pCommandBuffers = &m_sampleParams.GraphicsCommandBuffers[currentBufferIndex]; // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;                                                       // One command buffer

    submitInfo.pWaitSemaphores = &m_sampleParams.ImageAvailableSemaphore;          // Semaphore(s) to wait upon before the submitted command buffers start executing
    submitInfo.pSignalSemaphores = &m_sampleParams.RenderingFinishedSemaphore;     // Semaphore(s) to be signaled when command buffers have completed

    VK_CHECK_RESULT(vkQueueSubmit(m_vulkanParams.GraphicsQueue.Handle, 1, &submitInfo, VK_NULL_HANDLE));
}

void VKHelloSCB::PresentImage(uint32_t imageIndex)
{
    // Present the current buffer to the swap chain.
    // Pass the semaphore from the submit info as the wait semaphore for swap chain presentation.
    // This ensures that the image is not presented to the windowing system until all commands have been executed.
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = NULL;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_vulkanParams.SwapChain.Handle;
    presentInfo.pImageIndices = &imageIndex;
    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (m_sampleParams.RenderingFinishedSemaphore != VK_NULL_HANDLE)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_sampleParams.RenderingFinishedSemaphore;
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

void VKHelloSCB::WindowResize(uint32_t width, uint32_t height)
{
    if (!m_initialized)
    return;

    m_initialized = false;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(m_vulkanParams.Device);

    // Recreate swap chain
    m_width = width;
    m_height = height;
    CreateSwapchain(&m_width, &m_height, false);

    // Recreate the frame buffers
    for (uint32_t i = 0; i < m_sampleParams.Framebuffers.size(); i++) {
        vkDestroyFramebuffer(m_vulkanParams.Device, m_sampleParams.Framebuffers[i], nullptr);
    }
    CreateFrameBuffers();


    // Secondary Command buffers need to be recreated as they may store
    // references to the recreated frame buffer
    vkFreeCommandBuffers(m_vulkanParams.Device, m_sampleParams.GraphicsCommandPool, static_cast<uint32_t>(m_secondaryCommandBuffers.size()),m_secondaryCommandBuffers.data());
    AllocateSCBs();
    PopulateSCBs();
    vkDeviceWaitIdle(m_vulkanParams.Device);

    m_initialized = true;
}