#pragma once

#include "volk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

  int enable_debug_layers;
  uint32_t gpu_id;
  const char* app_name;
  uint32_t app_version;
  const char** device_extensions;
  uint32_t num_device_extensions;

} lida_DeviceDesc;

typedef struct {

  VkDeviceMemory handle;
  VkDeviceSize size;
  VkDeviceSize offset;
  uint32_t type;
  void* mapped;

} lida_VideoMemory;

typedef struct lida_ShaderReflect lida_ShaderReflect;

typedef struct {
  const char* vertex_shader;
  const char* fragment_shader;
  uint32_t vertex_binding_count;
  const VkVertexInputBindingDescription* vertex_bindings;
  uint32_t vertex_attribute_count;
  const VkVertexInputAttributeDescription* vertex_attributes;
  VkPrimitiveTopology topology;
  VkViewport* viewport;
  VkRect2D* scissor;
  VkPolygonMode polygonMode;
  VkCullModeFlags cullMode;
  VkBool32 depthBiasEnable;
  // FIXME: should we support these?
  float depthBiasConstantFactor;
  float depthBiasClamp;
  float depthBiasSlopeFactor;
  float lineWidth;

  VkSampleCountFlagBits msaa_samples;
  // TODO: support sample_shading_enable = VK_TRUE
  // VkBool32 sample_shading_enable;
  VkBool32 depth_test;
  VkBool32 depth_write;
  VkCompareOp depth_compare_op;
  uint32_t blend_logic_enable;
  VkLogicOp blend_logic_op;
  uint32_t attachment_count;
  const VkPipelineColorBlendAttachmentState* attachments;
  float blend_constants[4];
  uint32_t dynamic_state_count;
  VkDynamicState* dynamic_states;
  VkRenderPass render_pass;
  uint32_t subpass;
  const char* marker;
} lida_PipelineDesc;

// Create device
// if desc->enable_debug_layers then try to load validation layers and enable VK_EXT_DEBUG_MARKER extension
VkResult lida_DeviceCreate(const lida_DeviceDesc* desc);
#define LIDA_DEVICE_CREATE(...) lida_DeviceCreate(&(lida_DeviceDesc) { __VA_ARGS__ })

// if fast == 1 then do not free memory used by device explicitly.
// This is intended for case when TempAllocator will be freed anyway.
void lida_DeviceDestroy(int fast);

// Get underlying Vulkan Instance handle
VkInstance lida_GetVulkanInstance();
// Get underlying VkDevice handle
VkDevice lida_GetLogicalDevice();
VkPhysicalDevice lida_GetPhysicalDevice();

// get list of enabled instance extensions
const char** lida_GetEnabledInstanceExtensions();
uint32_t lida_GetNumEnabledInstanceExtensions();

// get list of available instance extensions
const VkExtensionProperties* lida_GetAvailableInstanceExtensions();
uint32_t lida_GetNumAvailableInstanceExtensions();

// get list of enabled device extensions
const char** lida_GetEnabledDeviceExtensions();
uint32_t lida_GetNumEnabledDeviceExtensions();

// get list of available device extensions
const VkExtensionProperties* lida_GetAvailableDeviceExtensions();
uint32_t lida_GetNumAvailableDeviceExtensions();

// currently we have 1 queue for whole application, we might consider adding compute queue
uint32_t lida_GetGraphicsQueueFamily();

VkResult lida_AllocateCommandBuffers(VkCommandBuffer* cmds, uint32_t count, VkCommandBufferLevel level, const char* marker);
VkResult lida_QueueSubmit(VkSubmitInfo* submits, uint32_t count, VkFence fence);
VkResult lida_QueuePresent(VkPresentInfoKHR* present_info);

VkResult lida_VideoMemoryAllocate(lida_VideoMemory* memory, VkDeviceSize size,
                                  VkMemoryPropertyFlags flags, uint32_t memory_type_bits,
                                  const char* marker);
void lida_VideoMemoryFree(lida_VideoMemory* memory);
void lida_VideoMemoryReset(lida_VideoMemory* memory);
VkMemoryPropertyFlags lida_VideoMemoryGetFlags(const lida_VideoMemory* memory);
void lida_MergeMemoryRequirements(const VkMemoryRequirements* requirements, uint32_t count, VkMemoryRequirements* out);

VkShaderModule lida_LoadShader(const char* path, const lida_ShaderReflect** reflect);

VkDescriptorSetLayout lida_GetDescriptorSetLayout(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings);
VkResult lida_AllocateDescriptorSets(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings,
                                     VkDescriptorSet* sets, uint32_t num_sets, int dynamic, const char* marker);
VkResult lida_FreeDescriptorSets(const VkDescriptorSet* sets, uint32_t num_sets);
void lida_UpdateDescriptorSets(const VkWriteDescriptorSet* pDescriptorWrites, uint32_t count);

VkSampler lida_GetSampler(VkFilter filter, VkSamplerAddressMode mode);

VkPipelineLayout lida_CreatePipelineLayout(const lida_ShaderReflect** shader_templates, uint32_t count);

VkResult lida_RenderPassCreate(VkRenderPass* render_pass, const VkRenderPassCreateInfo* render_pass_info, const char* marker);

VkResult lida_BufferCreate(VkBuffer* buffer, VkDeviceSize size, VkBufferUsageFlags usage, const char* marker);
VkResult lida_BufferBindToMemory(lida_VideoMemory* memory, VkBuffer buffer,
                                 const VkMemoryRequirements* requirements, void** mapped,
                                 VkMappedMemoryRange* mappedRange);

VkFormat lida_FindSupportedFormat(VkFormat* options, uint32_t count, VkImageTiling tiling, VkFormatFeatureFlags flags);
#define LIDA_FIND_SUPPORTED_FORMAT(options_array, tiling, flags) lida_FindSupportedFormat(options_array, sizeof(options_array) / sizeof(VkFormat), tiling, flags)
VkResult lida_ImageCreate(VkImage* image, const VkImageCreateInfo* image_info, const char* marker);
VkResult lida_ImageViewCreate(VkImageView* image_view, const VkImageViewCreateInfo* image_view_info, const char* marker);
VkResult lida_ImageBindToMemory(lida_VideoMemory* memory, VkImage image,
                                const VkMemoryRequirements* requirements);

VkResult lida_FramebufferCreate(VkFramebuffer* framebuffer, const VkFramebufferCreateInfo* framebuffer_info, const char* marker);

VkSampleCountFlagBits lida_MaxSampleCount(VkSampleCountFlagBits max_samples);

VkResult lida_CreateGraphicsPipelines(VkPipeline* pipelines, uint32_t count, const lida_PipelineDesc* descs, VkPipelineLayout* layouts);

const char* lida_VkResultToString(VkResult err);
const char* lida_VkFormatToString(VkFormat format);

VkShaderStageFlags lida_ShaderReflectGetStage(const lida_ShaderReflect* shader);
uint32_t lida_ShaderReflectGetNumSets(const lida_ShaderReflect* shader);
uint32_t lida_ShaderReflectGetNumBindings(const lida_ShaderReflect* shader, uint32_t set);
const VkDescriptorSetLayoutBinding* lida_ShaderReflectGetBindings(const lida_ShaderReflect* shader, uint32_t set);
uint32_t lida_ShaderReflectGetNumRanges(const lida_ShaderReflect* shader);
const VkPushConstantRange* lida_ShaderReflectGetRanges(const lida_ShaderReflect* shader);

#ifdef __cplusplus
}
#endif
