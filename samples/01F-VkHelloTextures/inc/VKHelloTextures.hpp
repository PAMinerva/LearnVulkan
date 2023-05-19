#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

class VKHelloTextures : public VKSample
{
public:
    VKHelloTextures(uint32_t width, uint32_t height, std::string name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    
    void InitVulkan();
    void SetupPipeline();
    
    void PopulateCommandBuffer(uint32_t currentImageIndex);
    void SubmitCommandBuffer();
    void PresentImage(uint32_t currentImageIndex);
    
    void CreateVertexBuffer();            // Create a vertex buffer
    void CreateHostVisibleBuffers();      // Create a buffer in host-visible memory
    void CreateDescriptorPool();          // Create a descriptor pool
    void CreateDescriptorSetLayout();     // Create a descriptor set layout
    void AllocateDescriptorSets();         // Allocate a descriptor set
    void CreatePipelineLayout();          // Create a pipeline layout
    void CreatePipelineObjects();         // Create a pipeline object

    void UpdateHostVisibleBufferData();   // Update buffer data

    std::vector<uint8_t> GenerateTextureData();  // Generate texture data
    void CreateStagingBuffer();                  // Create a staging buffer
    void CreateTexture();                        // Create a texture

    // For simplicity we use the same uniform block layout as in the vertex shader:
    //
    // layout(set = 0, binding = 0) uniform buf {
    //    vec4 displacement;
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
        float texCoord[2];
    };
    
    // Vertex buffer
    struct {
        VkDeviceMemory memory; // Handle to the device memory backing the vertex buffer
        VkBuffer buffer;       // Handle to the Vulkan buffer object that the memory is bound to
    } m_vertices;

    // Texture
    struct {
        BufferParameters StagingBuffer;    // Staging buffer
        ImageParameters  TextureImage;     // Texture image

        // Texture and texel dimensions
        const uint32_t TextureWidth = 256;
        const uint32_t TextureHeight = 256;
        const uint32_t TextureTexelSize = 4;  // The number of bytes used to represent a texel in the texture.
    } m_texture;
};