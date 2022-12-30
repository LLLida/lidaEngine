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

VkResult lida_DeviceCreate(const lida_DeviceDesc* desc);
#define LIDA_DEVICE_CREATE(...) lida_DeviceCreate(&(lida_DeviceDesc) { __VA_ARGS__ })

// if fast == 1 then do not free memory used by device explicitly.
// This is intended for case when TempAllocator will be freed anyway.
void lida_DeviceDestroy(int fast);

VkInstance lida_GetVulkanInstance();
VkDevice lida_GetLogicalDevice();
VkPhysicalDevice lida_GetPhysicalDevice();

const char** lida_GetEnabledInstanceExtensions();
uint32_t lida_GetNumEnabledInstanceExtensions();

const VkExtensionProperties* lida_GetAvailableInstanceExtensions();
uint32_t lida_GetNumAvailableInstanceExtensions();

const char** lida_GetEnabledDeviceExtensions();
uint32_t lida_GetNumEnabledDeviceExtensions();

const VkExtensionProperties* lida_GetAvailableDeviceExtensions();
uint32_t lida_GetNumAvailableDeviceExtensions();

uint32_t lida_GetGraphicsQueueFamily();

VkResult lida_AllocateCommandBuffers(VkCommandBuffer* cmds, uint32_t count, VkCommandBufferLevel level);
VkResult lida_QueueSubmit(VkSubmitInfo* submits, uint32_t count, VkFence fence);
VkResult lida_QueuePresent(VkPresentInfoKHR* present_info);

VkResult lida_VideoMemoryAllocate(lida_VideoMemory* memory, VkDeviceSize size,
                                  VkMemoryPropertyFlags flags, uint32_t memory_type_bits);
void lida_VideoMemoryFree(lida_VideoMemory* memory);
void lida_VideoMemoryReset(lida_VideoMemory* memory);
VkMemoryPropertyFlags lida_VideoMemoryGetFlags(const lida_VideoMemory* memory);
void lida_MergeMemoryRequirements(const VkMemoryRequirements* requirements, uint32_t count, VkMemoryRequirements* out);

VkShaderModule lida_LoadShader(const char* path, const lida_ShaderReflect** reflect);

VkDescriptorSetLayout lida_GetDescriptorSetLayout(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings);
VkResult lida_AllocateDescriptorSets(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings,
                                     VkDescriptorSet* sets, uint32_t num_sets, int dynamic);
VkResult lida_FreeAllocateDescriptorSets(const VkDescriptorSet* sets, uint32_t num_sets);
void lida_UpdateDescriptorSets(const VkWriteDescriptorSet* pDescriptorWrites, uint32_t count);

VkSampler lida_GetSampler(VkFilter filter, VkSamplerAddressMode mode);

VkPipelineLayout lida_CreatePipelineLayout(const lida_ShaderReflect** shader_templates, uint32_t count);

VkResult lida_BufferCreate(VkBuffer* buffer, VkDeviceSize size, VkBufferUsageFlags usage);
VkResult lida_BufferBindToMemory(lida_VideoMemory* memory, VkBuffer buffer,
                                 const VkMemoryRequirements* requirements, void** mapped,
                                 VkMappedMemoryRange* mappedRange);

VkFormat lida_FindSupportedFormat(VkFormat* options, uint32_t count, VkImageTiling tiling, VkFormatFeatureFlags flags);
#define LIDA_FIND_SUPPORTED_FORMAT(options_array, tiling, flags) lida_FindSupportedFormat(options_array, sizeof(options_array) / sizeof(VkFormat), tiling, flags)
VkResult lida_ImageBindToMemory(lida_VideoMemory* memory, VkImage image,
                                const VkMemoryRequirements* requirements);

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
