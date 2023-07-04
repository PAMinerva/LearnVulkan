#pragma once

extern PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT;
extern PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT;
extern VkDebugUtilsMessengerEXT debugUtilsMessenger;

// Load debug function pointers and set debug callback
// if callBack is NULL, default message callback will be used
void setupDebugUtil(VkInstance instance);
// Clear debug callback

void freeDebugCallback(VkInstance instance);




// Set to true if function pointer for the debug marker are available
extern bool active;

// Get function pointers for the debug report extensions from the device
void setupDebugMarker(VkDevice device);

// Sets the debug name of an object
// All Objects in Vulkan are represented by their 64-bit handles which are passed into this function
// along with the object type
void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char* name);

// Set the tag for an object
void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void* tag);

// Start a new debug marker region
void beginRegion(VkCommandBuffer cmdbuffer, const char* pMarkerName, const float* color);

// Insert a new debug marker into the command buffer
void insert(VkCommandBuffer cmdbuffer, std::string markerName, const float* color);

// End the current debug marker region
void endRegion(VkCommandBuffer cmdBuffer);

// Object specific naming functions
void setCommandBufferName(VkDevice device, VkCommandBuffer cmdBuffer, const char* name);
void setQueueName(VkDevice device, VkQueue queue, const char* name);
void setImageName(VkDevice device, VkImage image, const char* name);
void setSamplerName(VkDevice device, VkSampler sampler, const char* name);
void setBufferName(VkDevice device, VkBuffer buffer, const char* name);
void setDeviceMemoryName(VkDevice device, VkDeviceMemory memory, const char* name);
void setShaderModuleName(VkDevice device, VkShaderModule shaderModule, const char* name);
void setPipelineName(VkDevice device, VkPipeline pipeline, const char* name);
void setPipelineLayoutName(VkDevice device, VkPipelineLayout pipelineLayout, const char* name);
void setRenderPassName(VkDevice device, VkRenderPass renderPass, const char* name);
void setFramebufferName(VkDevice device, VkFramebuffer framebuffer, const char* name);
void setDescriptorSetLayoutName(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const char* name);
void setDescriptorSetName(VkDevice device, VkDescriptorSet descriptorSet, const char* name);
void setSemaphoreName(VkDevice device, VkSemaphore semaphore, const char* name);
void setFenceName(VkDevice device, VkFence fence, const char* name);
void setEventName(VkDevice device, VkEvent _event, const char* name);