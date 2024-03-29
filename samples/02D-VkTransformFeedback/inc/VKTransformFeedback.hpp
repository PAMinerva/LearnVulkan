#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

class VKTransformFeedback : public VKSample
{
public:
    VKTransformFeedback(uint32_t width, uint32_t height, std::string name);
    ~VKTransformFeedback();

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

    // Extension functions and structs
    PFN_vkCmdBindTransformFeedbackBuffersEXT     vkCmdBindTransformFeedbackBuffersEXT;
    PFN_vkCmdBeginTransformFeedbackEXT           vkCmdBeginTransformFeedbackEXT;
    PFN_vkCmdEndTransformFeedbackEXT             vkCmdEndTransformFeedbackEXT;
    PFN_vkCmdDrawIndirectByteCountEXT            vkCmdDrawIndirectByteCountEXT;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT featuresTF;
    
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
        glm::mat4 worldMatrix;
        glm::vec4 solidColor;
    };

    struct {
        MeshInfo *meshInfo;        // pointer to an array of mesh info
    } dynUBufVS;
    
    // Vertex layout used in this sample (stride: 24 bytes)
    struct Vertex {
        glm::vec3 position;
        glm::vec2 size;
        float     speed;
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

    // In this sample we have two draw calls for each frame.
    const unsigned int m_numDrawCalls = 2;

    // Mesh objects to draw
    std::map<std::string, MeshObject> m_meshObjects;

    // Sample members
    size_t m_dynamicUBOAlignment;

    // Particle collection
    std::vector<Vertex> particles;

    // Create buffers for the Transform Feedback stage
    void CreateTransformFeedbackBuffers();

    // Transform Feedback buffers
    struct {
        VkDeviceMemory TFmemory;        // Handle to the device memory backing the Transform Feedback buffer
        VkBuffer TFbuffer;              // Handle to the Transform Feedback buffer
        VkDeviceMemory CounterMemory;   // Handle to the device memory backing the Counter buffer
        VkBuffer CounterBuffer;         // Handle to the Counter buffer
    } m_transformFeedbackBuffers;
};