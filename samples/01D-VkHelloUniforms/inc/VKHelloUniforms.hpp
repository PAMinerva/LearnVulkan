#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

class VKHelloUniforms : public VKSample
{
public:
    VKHelloUniforms(uint32_t width, uint32_t height, std::string name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    
    void InitVulkan();
    void SetupPipeline();
    
    void PopulateCommandBuffer(uint32_t currentBufferIndex, uint32_t currentIndexImage);
    void SubmitCommandBuffer(uint32_t currentBufferIndex);
    void PresentImage(uint32_t imageIndex);
    
    void CreateVertexBuffer();          // Create a vertex buffer
    void CreateUniformBuffer();         // Create a uniform buffer
    void CreateDescriptorPool();        // Create a descriptor pool
    void CreateDescriptorSetLayout();   // Create a descriptor set layout
    void CreateDescriptorSet();         // Create a descriptor set
    void CreatePipelineLayout();        // Create a pipeline layout
    void CreatePipelineObjects();       // Create a pipeline object

    void UpdateUniformBuffer();         // Update uniform buffer data

    // For simplicity we use the same uniform block layout as in the vertex shader:
    //
    // layout(set = 0, binding = 0) uniform buf {
    //         vec4 displacement;
    // } uBuf;
    //
    // This way we can just memcopy the uBufVS data to match the uBuf memory layout.
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        float displacement[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    } uBufVS;
    
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