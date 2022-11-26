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

VkShaderModule lida_LoadShader(const char* path);

#ifdef __cplusplus
}
#endif
