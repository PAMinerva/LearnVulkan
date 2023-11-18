#pragma once

#include "VKSample.hpp"
#include "VKSampleHelper.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

#define LIGHT_NUM 2

class VKHelloPushSpecConstants : public VKSample
{
public:
    VKHelloPushSpecConstants(uint32_t width, uint32_t height, std::string name);
    ~VKHelloPushSpecConstants();

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

    virtual void OnResize();

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

    // In the fragment shader:
    //
    // layout(std140, push_constant) uniform push {
    //     vec4 lightDirs[LIGHT_NUM];
    //     vec4 lightColors[LIGHT_NUM];
    // } pushConsts;
    struct PushConsts {
        glm::vec4 lightDirs[LIGHT_NUM];
        glm::vec4 lightColors[LIGHT_NUM];
    } m_pushConstants;

    // In the fragment shader:
    //
    // layout (constant_id = 0) const uint LIGHT_NUM = 2;
    // layout (constant_id = 1) const float ALPHA = 0.0;
    struct SpecConsts {
        uint32_t lightNumber;
        float alphaChannel;
    } m_specConstants;

    // In the vertex shader:
    //
    // layout(std140, set = 0, binding = 0) uniform buf {
    //     mat4 View;
    //     mat4 Projection;
    // } uBuf;
    //
    // This way we can just memcopy the m_uBufVS data to match the uBuf memory layout.
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 viewMatrix;         // 64 bytes
        glm::mat4 projectionMatrix;   // 64 bytes
    } m_uBufVS;

    // Uniform block defined in the vertex and fragment shaders to be used as a dynamic uniform buffer:
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
    } m_dynUBufVS;
    
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

    // In this sample we have three draw calls for each frame.
    const unsigned int m_numDrawCalls = 3;

    // Sample members
    float m_curRotationAngleRad;
    size_t m_dynamicUBOAlignment;
};