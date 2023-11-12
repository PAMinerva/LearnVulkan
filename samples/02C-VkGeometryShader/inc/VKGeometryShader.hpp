#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

class VKGeometryShader : public VKSample
{
public:
    VKGeometryShader(uint32_t width, uint32_t height, std::string name);
    ~VKGeometryShader();

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

    virtual void OnResize();

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

    // For simplicity we use the same uniform block layout as in the vertex shader:
    //
    // layout(std140, set = 0, binding = 0) uniform buf {
    //     mat4 View;
    //     mat4 Projection;
    //     vec4 lightDirs;
    //     vec4 lightColor;
    // } uBuf;
    //
    // This way we can just memcopy the uBufVS data to match the uBuf memory layout.
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 viewMatrix;         // 64 bytes
        glm::mat4 projectionMatrix;   // 64 bytes
        glm::vec4 lightDir;           // 16 bytes
        glm::vec4 lightColor;         // 16 bytes
    } uBufVS;

    // Uniform block defined in the vertex shader to be used as a dynamic uniform buffer:
    //
    //layout(std140, set = 0, binding = 1) uniform dynbuf {
    //     mat4 World;
    //     vec4 solidColor;
    // } dynBuf;
    //
    // Allow the specification of different world matrices for different objects by offsetting
    // into the same buffer.
    struct MeshInfo{
        glm::mat4 worldMatrix;        // pointer to an array of world matrices
        glm::vec4 solidColor;
    };

    struct {
        MeshInfo *meshInfo;
    } dynUBufVS;
    
    // Vertex layout used in this sample
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
    };
    
    // Vertex and index buffers
    struct {
        VkDeviceMemory VBmemory; // Handle to the device memory backing the vertex buffer
        VkBuffer VBbuffer;       // Handle to the Vulkan buffer object that the memory is bound to
        VkDeviceMemory IBmemory; // Handle to the device memory backing the index buffer
        VkBuffer IBbuffer;       // Handle to the Vulkan buffer object that the memory is bound to
        size_t indexBufferCount; // Number of indices
    } m_vertexindexBuffer;

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

    // In this sample we have two draw calls for each frame.
    const unsigned int m_numDrawCalls = 2;

    // Mesh objects to draw
    std::map<std::string, MeshObject> m_meshObjects;

    // Sample members
    float m_curRotationAngleRad;
    size_t m_dynamicUBOAlignment;

    // List of vertices and indices 
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    // Create a sphere
    void ComputeSphere(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices, float diameter, uint16_t tessellation);
};