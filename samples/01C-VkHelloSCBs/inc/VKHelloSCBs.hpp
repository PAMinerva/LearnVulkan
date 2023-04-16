#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

class VKHelloSCB : public VKSample
{
public:
    VKHelloSCB(uint32_t width, uint32_t height, std::string name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

    virtual void WindowResize(uint32_t width, uint32_t height);

private:
    
    void InitVulkan();
    void SetupPipeline();

    void AllocateSCBs();
    void PopulateSCBs();
    
    void PopulateCommandBuffer(uint32_t currentBufferIndex, uint32_t currentIndexImage);
    void SubmitCommandBuffer(uint32_t currentBufferIndex);
    void PresentImage(uint32_t imageIndex);
    
    void CreateVertexBuffer();          // Create a vertex buffer
    void CreatePipelineLayout();        // Create a pipeline layout
    void CreatePipelineObjects();       // Create a pipeline object

    // Secondary Command Buffer;
     std::vector<VkCommandBuffer> m_secondaryCommandBuffers;

    // Vertex layout used in this sample
    struct Vertex {
    	float position[3];
    	float color[4];
    };
    
    // Vertex buffer
    struct {
    	VkDeviceMemory memory; // Handle to the device memory backing the vertex buffer
    	VkBuffer buffer;       // Handle to the Vulkan buffer object that the memory is bound to
    } m_vertices;
    
    uint32_t m_commandBufferIndex = 0;
};