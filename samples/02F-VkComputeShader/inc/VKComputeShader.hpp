#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

class VKComputeShader : public VKSample
{
public:
    VKComputeShader(uint32_t width, uint32_t height, std::string name);
    ~VKComputeShader();

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

    virtual void OnResize();

    virtual void EnableInstanceExtensions(std::vector<const char*>& instanceExtensions);
    virtual void EnableDeviceExtensions(std::vector<const char*>& deviceExtensions);
    virtual void EnableFeatures(VkPhysicalDeviceFeatures& features);

private:
    
    void InitVulkan();
    void SetupPipeline();
    
    void PopulateCommandBuffer(uint32_t currentImageIndex);
    void SubmitCommandBuffer();
    void PresentImage(uint32_t currentImageIndex);
    
    void CreateVertexBuffer();              // Create a vertex buffer
    void CreateHostVisibleBuffers();        // Create a buffer in host-visible memory
    void CreateHostVisibleDynamicBuffers(); // Create a dynamic buffer
    void CreateDescriptorPool();            // Create a descriptor pool
    void CreateDescriptorSetLayout();       // Create a descriptor set layout
    void AllocateDescriptorSets();          // Allocate a descriptor set
    void CreatePipelineLayout();            // Create a pipeline layout
    void CreatePipelineObjects();           // Create a pipeline object

    // Update buffer data
    void UpdateHostVisibleBufferData();
    void UpdateHostVisibleDynamicBufferData();

    // Texture creation
    std::vector<uint8_t> GenerateTextureData();  // Generate texture data
    void CreateStagingBuffer();                  // Create a staging buffer
    void CreateInputTexture();                   // Create input texture
    void CreateOutputTextures();                 // Create output textures

    // Compute setup and operations
    void PrepareCompute();
    void PopulateComputeCommandBuffer();
    void SubmitComputeCommandBuffer();


    // For simplicity we use the same uniform block layout as in the vertex shader:
    //
    // layout(std140, set = 0, binding = 0) uniform buf {
    //     mat4 View;
    //     mat4 Projection;
    // } uBuf;
    //
    // This way we can just memcopy the uBufVS data to match the uBuf memory layout.
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 viewMatrix;         // 64 bytes
        glm::mat4 projectionMatrix;   // 64 bytes
    } uBufVS;

    // Uniform block defined in the vertex shader to be used as a dynamic uniform buffer:
    //
    //layout(std140, set = 0, binding = 1) uniform dynbuf {
    //     mat4 World;
    // } dynBuf;
    //
    // Allow the specification of different world matrices for different objects by offsetting
    // into the same buffer.
    struct MeshInfo{
        glm::mat4 worldMatrix;
    };

    struct {
        MeshInfo *meshInfo;        // pointer to an array of mesh info
    } dynUBufVS;
    
    // Vertex layout used in this sample (stride: 20 bytes)
    struct Vertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };
    
    // Vertex and index buffers
    struct {
        VkDeviceMemory VBmemory; // Handle to the device memory backing the vertex buffer
        VkBuffer VBbuffer;       // Handle to the Vulkan buffer object that the memory is bound to
        VkDeviceMemory IBmemory; // Handle to the device memory backing the index buffer
        VkBuffer IBbuffer;       // Handle to the Vulkan buffer object that the memory is bound to
        size_t indexBufferCount; // Number of indices
    } m_vertexindexBuffers;

    // Mesh object info
    struct MeshObject
    {
        uint32_t dynIndex;
        uint32_t indexCount;
        uint32_t firstIndex;
        uint32_t vertexOffset;
        uint32_t vertexCount;
        MeshInfo *meshInfo;
    };

    // Texture info and data
    struct Texture2D {
        BufferParameters StagingBuffer;    // Staging buffer
        ImageParameters  TextureImage;     // Texture image

        // Texture and texel dimensions
        const uint32_t TextureWidth = 256;
        const uint32_t TextureHeight = 256;
        const uint32_t TextureTexelSize = 4;  // The number of bytes used to represent a texel in the texture.
    };

    // In this sample we have two draw calls for each frame.
    // However, the drawing parameters will be the same so we can set 1.
    const unsigned int m_numDrawCalls = 1;

    // Mesh objects to draw
    std::map<std::string, MeshObject> m_meshObjects;

    // Input texture and a set of output textures used to store the result of compute processing (one for each frame in flight).
    Texture2D m_inputTexture;
    std::vector<Texture2D> m_outputTextures;

    // Compute resources and variables
    SampleParameters m_sampleComputeParams;

    // Sample members
    size_t m_dynamicUBOAlignment;
};