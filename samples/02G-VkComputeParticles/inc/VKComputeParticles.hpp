#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

class VKComputeParticles : public VKSample
{
public:
    VKComputeParticles(uint32_t width, uint32_t height, std::string name);
    ~VKComputeParticles();

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
    
    //void CreateVertexBuffer();            // Create a vertex buffer
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

    // Buffer creation
    void CreateStagingBuffer();                  // Create a staging buffer
    void CreateStorageBuffers();                 // Create storage buffers

    // Compute setup and operations
    void PrepareCompute();
    void PopulateComputeCommandBuffer();
    void SubmitComputeCommandBuffer();


    // For simplicity we use the same uniform block layout used in shader code:
    //
    // layout(std140, set = 0, binding = 0) uniform buf {
    //     mat4 viewMatrix;
    //     mat4 projMatrix;
    //     vec3 cameraPos;
    //     float deltaTime;
    // } uBuf;
    //
    // This way we can just memcopy the uBufVS data to match the uBuf memory layout.
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 viewMatrix;         // 64 bytes
        glm::mat4 projectionMatrix;   // 64 bytes
        glm::vec3 cameraPos;          // 12 bytes
        float     deltaTime;          // 4 bytes
    } uBufVS;

    // Uniform block defined in the shader code to be used as a dynamic uniform buffer:
    //
    //layout(std140, set = 0, binding = 1) uniform dynbuf {
    //     mat4 worlddMatrix;
    //     vec4 solidColor;
    // } dynBuf;
    //
    // Allow the specification of different world matrices for different objects by offsetting
    // into the same buffer.
    struct MeshInfo{
        glm::mat4 worldMatrix;
        glm::vec4 solidColor;
    };

    struct {
        MeshInfo *meshInfo;  // pointer to an array of mesh info
    } dynUBufVS;
    
    // Vertex layout used in this sample (stride: 32 bytes)
    // We will store multiple vertices countiguously in storage buffers used both as vertex buffer and 
    // uniform buffer, so we need to pad.
    struct Vertex {
        glm::vec3 position;
        float pad_1;
        glm::vec2 size;
        float speed;
        float pad_2;
    };

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

    // Storage buffer info and data
    struct StorageBuf {
        BufferParameters StorageBuffer;

        // Buffer lenght and element size
        static const uint32_t BufferLenght = 81; // num. of particles
        static const uint32_t BufferElementSize = sizeof(Vertex);  // byte size of each particle
    };

    // In this sample we have a single draw call.
    const unsigned int m_numDrawCalls = 1;

    // Mesh objects to draw
    std::map<std::string, MeshObject> m_meshObjects;

    // Storage buffers (one for each frame in flight).
    std::vector<StorageBuf> m_storageBuffers;
    BufferParameters m_stagingBuffer;    // Staging buffer

    // Compute resources and variables
    SampleParameters m_sampleComputeParams;

    // Sample members
    size_t m_dynamicUBOAlignment;
    std::vector<Vertex> m_particles;
};