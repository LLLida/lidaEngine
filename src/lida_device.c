/*
  lida_device.c
  Vulkan device creation and a lot of vulkan abstraction.
 */

#include "lib/spirv.h" // for runtime SPIR-V reflection

typedef struct {

  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice logical_device;
  uint32_t graphics_queue_family;
  VkQueue graphics_queue;
  /* uint32_t present_queue_family; */
  VkDebugReportCallbackEXT debug_report_callback;
  VkCommandPool command_pool;
  // for static resources
  VkDescriptorPool static_ds_pool;
  // for dynamic resources
  VkDescriptorPool dynamic_ds_pool;

  VkExtensionProperties* available_instance_extensions;
  uint32_t num_available_instance_extensions;

  const char** enabled_instance_extensions;
  uint32_t num_enabled_instance_extensions;

  VkQueueFamilyProperties* queue_families;
  uint32_t num_queue_families;

  VkExtensionProperties* available_device_extensions;
  uint32_t num_available_device_extensions;

  // TODO: store those in hash table of smth
  const char** enabled_device_extensions;
  uint32_t num_enabled_device_extensions;

  int debug_marker_enabled;

  Type_Info shader_info_type;
  Type_Info ds_layout_info_type;
  Type_Info sampler_info_type;
  Type_Info pipeline_layout_info_type;

  Fixed_Hash_Table shader_cache;
  Fixed_Hash_Table ds_layout_cache;
  Fixed_Hash_Table sampler_cache;
  Fixed_Hash_Table pipeline_layout_cache;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;

} Device_Vulkan;

GLOBAL Device_Vulkan* g_device;

typedef struct {

  VkDeviceMemory handle;
  VkDeviceSize size;
  VkDeviceSize offset;
  uint32_t type;
  // maybe NULL
  void* mapped;

} Video_Memory;

#define SHADER_REFLECT_MAX_SETS 8
#define SHADER_REFLECT_MAX_BINDINGS_PER_SET 16
#define SHADER_REFLECT_MAX_RANGES 4

typedef struct {

  VkDescriptorSetLayoutBinding bindings[SHADER_REFLECT_MAX_BINDINGS_PER_SET];
  uint32_t binding_count;

} Binding_Set_Desc;

typedef struct {

  VkShaderStageFlags stages;
  uint32_t localX, localY, localZ;
  Binding_Set_Desc sets[SHADER_REFLECT_MAX_SETS];
  size_t set_count;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  size_t range_count;

} Shader_Reflect;

typedef struct {

  const char* name;
  VkShaderModule module;
  Shader_Reflect reflect;

} Shader_Info;

typedef struct {

  VkDescriptorSetLayoutBinding bindings[16];
  size_t num_bindings;
  VkDescriptorSetLayout layout;

} DS_Layout_Info;

typedef struct {

  VkFilter filter;
  VkSamplerAddressMode mode;
  VkBorderColor border_color;
  VkSampler handle;

} Sampler_Info;

typedef struct {

  VkDescriptorSetLayout set_layouts[SHADER_REFLECT_MAX_SETS];
  size_t num_sets;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  size_t num_ranges;
  VkPipelineLayout handle;

} Pipeline_Layout_Info;

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
  // NOTE: if enabled, depth bias should be set dynamically
  VkBool32 depth_bias_enable;
  float line_width;

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

} Pipeline_Desc;


/// Functions used primarily by this module

INTERNAL const char*
ToString_VkResult(VkResult err)
{
  switch (err) {
  case VK_SUCCESS: return "VK_SUCCESS";
  case VK_NOT_READY: return "VK_NOT_READY";
  case VK_TIMEOUT: return "VK_TIMEOUT";
  case VK_EVENT_SET: return "VK_EVENT_SET";
  case VK_EVENT_RESET: return "VK_EVENT_RESET";
  case VK_INCOMPLETE: return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
  case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
  case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
  case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
  case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
  case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
  case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
  case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
  case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
  case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
  case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
  case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
  case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
  case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
  case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
  case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
  default: return "VkResult(nil)";
  }
}

const char*
ToString_VkFormat(VkFormat format)
{
  switch (format) {
  case VK_FORMAT_UNDEFINED: return "VK_FORMAT_UNDEFINED";
  case VK_FORMAT_R4G4_UNORM_PACK8: return "VK_FORMAT_R4G4_UNORM_PACK8";
  case VK_FORMAT_R4G4B4A4_UNORM_PACK16: return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
  case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
  case VK_FORMAT_R5G6B5_UNORM_PACK16: return "VK_FORMAT_R5G6B5_UNORM_PACK16";
  case VK_FORMAT_B5G6R5_UNORM_PACK16: return "VK_FORMAT_B5G6R5_UNORM_PACK16";
  case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
  case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
  case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
  case VK_FORMAT_R8_UNORM: return "VK_FORMAT_R8_UNORM";
  case VK_FORMAT_R8_SNORM: return "VK_FORMAT_R8_SNORM";
  case VK_FORMAT_R8_USCALED: return "VK_FORMAT_R8_USCALED";
  case VK_FORMAT_R8_SSCALED: return "VK_FORMAT_R8_SSCALED";
  case VK_FORMAT_R8_UINT: return "VK_FORMAT_R8_UINT";
  case VK_FORMAT_R8_SINT: return "VK_FORMAT_R8_SINT";
  case VK_FORMAT_R8_SRGB: return "VK_FORMAT_R8_SRGB";
  case VK_FORMAT_R8G8_UNORM: return "VK_FORMAT_R8G8_UNORM";
  case VK_FORMAT_R8G8_SNORM: return "VK_FORMAT_R8G8_SNORM";
  case VK_FORMAT_R8G8_USCALED: return "VK_FORMAT_R8G8_USCALED";
  case VK_FORMAT_R8G8_SSCALED: return "VK_FORMAT_R8G8_SSCALED";
  case VK_FORMAT_R8G8_UINT: return "VK_FORMAT_R8G8_UINT";
  case VK_FORMAT_R8G8_SINT: return "VK_FORMAT_R8G8_SINT";
  case VK_FORMAT_R8G8_SRGB: return "VK_FORMAT_R8G8_SRGB";
  case VK_FORMAT_R8G8B8_UNORM: return "VK_FORMAT_R8G8B8_UNORM";
  case VK_FORMAT_R8G8B8_SNORM: return "VK_FORMAT_R8G8B8_SNORM";
  case VK_FORMAT_R8G8B8_USCALED: return "VK_FORMAT_R8G8B8_USCALED";
  case VK_FORMAT_R8G8B8_SSCALED: return "VK_FORMAT_R8G8B8_SSCALED";
  case VK_FORMAT_R8G8B8_UINT: return "VK_FORMAT_R8G8B8_UINT";
  case VK_FORMAT_R8G8B8_SINT: return "VK_FORMAT_R8G8B8_SINT";
  case VK_FORMAT_R8G8B8_SRGB: return "VK_FORMAT_R8G8B8_SRGB";
  case VK_FORMAT_B8G8R8_UNORM: return "VK_FORMAT_B8G8R8_UNORM";
  case VK_FORMAT_B8G8R8_SNORM: return "VK_FORMAT_B8G8R8_SNORM";
  case VK_FORMAT_B8G8R8_USCALED: return "VK_FORMAT_B8G8R8_USCALED";
  case VK_FORMAT_B8G8R8_SSCALED: return "VK_FORMAT_B8G8R8_SSCALED";
  case VK_FORMAT_B8G8R8_UINT: return "VK_FORMAT_B8G8R8_UINT";
  case VK_FORMAT_B8G8R8_SINT: return "VK_FORMAT_B8G8R8_SINT";
  case VK_FORMAT_B8G8R8_SRGB: return "VK_FORMAT_B8G8R8_SRGB";
  case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
  case VK_FORMAT_R8G8B8A8_SNORM: return "VK_FORMAT_R8G8B8A8_SNORM";
  case VK_FORMAT_R8G8B8A8_USCALED: return "VK_FORMAT_R8G8B8A8_USCALED";
  case VK_FORMAT_R8G8B8A8_SSCALED: return "VK_FORMAT_R8G8B8A8_SSCALED";
  case VK_FORMAT_R8G8B8A8_UINT: return "VK_FORMAT_R8G8B8A8_UINT";
  case VK_FORMAT_R8G8B8A8_SINT: return "VK_FORMAT_R8G8B8A8_SINT";
  case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
  case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
  case VK_FORMAT_B8G8R8A8_SNORM: return "VK_FORMAT_B8G8R8A8_SNORM";
  case VK_FORMAT_B8G8R8A8_USCALED: return "VK_FORMAT_B8G8R8A8_USCALED";
  case VK_FORMAT_B8G8R8A8_SSCALED: return "VK_FORMAT_B8G8R8A8_SSCALED";
  case VK_FORMAT_B8G8R8A8_UINT: return "VK_FORMAT_B8G8R8A8_UINT";
  case VK_FORMAT_B8G8R8A8_SINT: return "VK_FORMAT_B8G8R8A8_SINT";
  case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
  case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
  case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
  case VK_FORMAT_A8B8G8R8_USCALED_PACK32: return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
  case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
  case VK_FORMAT_A8B8G8R8_UINT_PACK32: return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
  case VK_FORMAT_A8B8G8R8_SINT_PACK32: return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
  case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
  case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
  case VK_FORMAT_A2R10G10B10_SNORM_PACK32: return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
  case VK_FORMAT_A2R10G10B10_USCALED_PACK32: return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
  case VK_FORMAT_A2R10G10B10_SSCALED_PACK32: return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
  case VK_FORMAT_A2R10G10B10_UINT_PACK32: return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
  case VK_FORMAT_A2R10G10B10_SINT_PACK32: return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
  case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
  case VK_FORMAT_A2B10G10R10_SNORM_PACK32: return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
  case VK_FORMAT_A2B10G10R10_USCALED_PACK32: return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
  case VK_FORMAT_A2B10G10R10_SSCALED_PACK32: return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
  case VK_FORMAT_A2B10G10R10_UINT_PACK32: return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
  case VK_FORMAT_A2B10G10R10_SINT_PACK32: return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
  case VK_FORMAT_R16_UNORM: return "VK_FORMAT_R16_UNORM";
  case VK_FORMAT_R16_SNORM: return "VK_FORMAT_R16_SNORM";
  case VK_FORMAT_R16_USCALED: return "VK_FORMAT_R16_USCALED";
  case VK_FORMAT_R16_SSCALED: return "VK_FORMAT_R16_SSCALED";
  case VK_FORMAT_R16_UINT: return "VK_FORMAT_R16_UINT";
  case VK_FORMAT_R16_SINT: return "VK_FORMAT_R16_SINT";
  case VK_FORMAT_R16_SFLOAT: return "VK_FORMAT_R16_SFLOAT";
  case VK_FORMAT_R16G16_UNORM: return "VK_FORMAT_R16G16_UNORM";
  case VK_FORMAT_R16G16_SNORM: return "VK_FORMAT_R16G16_SNORM";
  case VK_FORMAT_R16G16_USCALED: return "VK_FORMAT_R16G16_USCALED";
  case VK_FORMAT_R16G16_SSCALED: return "VK_FORMAT_R16G16_SSCALED";
  case VK_FORMAT_R16G16_UINT: return "VK_FORMAT_R16G16_UINT";
  case VK_FORMAT_R16G16_SINT: return "VK_FORMAT_R16G16_SINT";
  case VK_FORMAT_R16G16_SFLOAT: return "VK_FORMAT_R16G16_SFLOAT";
  case VK_FORMAT_R16G16B16_UNORM: return "VK_FORMAT_R16G16B16_UNORM";
  case VK_FORMAT_R16G16B16_SNORM: return "VK_FORMAT_R16G16B16_SNORM";
  case VK_FORMAT_R16G16B16_USCALED: return "VK_FORMAT_R16G16B16_USCALED";
  case VK_FORMAT_R16G16B16_SSCALED: return "VK_FORMAT_R16G16B16_SSCALED";
  case VK_FORMAT_R16G16B16_UINT: return "VK_FORMAT_R16G16B16_UINT";
  case VK_FORMAT_R16G16B16_SINT: return "VK_FORMAT_R16G16B16_SINT";
  case VK_FORMAT_R16G16B16_SFLOAT: return "VK_FORMAT_R16G16B16_SFLOAT";
  case VK_FORMAT_R16G16B16A16_UNORM: return "VK_FORMAT_R16G16B16A16_UNORM";
  case VK_FORMAT_R16G16B16A16_SNORM: return "VK_FORMAT_R16G16B16A16_SNORM";
  case VK_FORMAT_R16G16B16A16_USCALED: return "VK_FORMAT_R16G16B16A16_USCALED";
  case VK_FORMAT_R16G16B16A16_SSCALED: return "VK_FORMAT_R16G16B16A16_SSCALED";
  case VK_FORMAT_R16G16B16A16_UINT: return "VK_FORMAT_R16G16B16A16_UINT";
  case VK_FORMAT_R16G16B16A16_SINT: return "VK_FORMAT_R16G16B16A16_SINT";
  case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
  case VK_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
  case VK_FORMAT_R32_SINT: return "VK_FORMAT_R32_SINT";
  case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
  case VK_FORMAT_R32G32_UINT: return "VK_FORMAT_R32G32_UINT";
  case VK_FORMAT_R32G32_SINT: return "VK_FORMAT_R32G32_SINT";
  case VK_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
  case VK_FORMAT_R32G32B32_UINT: return "VK_FORMAT_R32G32B32_UINT";
  case VK_FORMAT_R32G32B32_SINT: return "VK_FORMAT_R32G32B32_SINT";
  case VK_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
  case VK_FORMAT_R32G32B32A32_UINT: return "VK_FORMAT_R32G32B32A32_UINT";
  case VK_FORMAT_R32G32B32A32_SINT: return "VK_FORMAT_R32G32B32A32_SINT";
  case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
  case VK_FORMAT_R64_UINT: return "VK_FORMAT_R64_UINT";
  case VK_FORMAT_R64_SINT: return "VK_FORMAT_R64_SINT";
  case VK_FORMAT_R64_SFLOAT: return "VK_FORMAT_R64_SFLOAT";
  case VK_FORMAT_R64G64_UINT: return "VK_FORMAT_R64G64_UINT";
  case VK_FORMAT_R64G64_SINT: return "VK_FORMAT_R64G64_SINT";
  case VK_FORMAT_R64G64_SFLOAT: return "VK_FORMAT_R64G64_SFLOAT";
  case VK_FORMAT_R64G64B64_UINT: return "VK_FORMAT_R64G64B64_UINT";
  case VK_FORMAT_R64G64B64_SINT: return "VK_FORMAT_R64G64B64_SINT";
  case VK_FORMAT_R64G64B64_SFLOAT: return "VK_FORMAT_R64G64B64_SFLOAT";
  case VK_FORMAT_R64G64B64A64_UINT: return "VK_FORMAT_R64G64B64A64_UINT";
  case VK_FORMAT_R64G64B64A64_SINT: return "VK_FORMAT_R64G64B64A64_SINT";
  case VK_FORMAT_R64G64B64A64_SFLOAT: return "VK_FORMAT_R64G64B64A64_SFLOAT";
  case VK_FORMAT_D16_UNORM: return "VK_FORMAT_D16_UNORM";
  case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT";
  case VK_FORMAT_S8_UINT: return "VK_FORMAT_S8_UINT";
  case VK_FORMAT_D16_UNORM_S8_UINT: return "VK_FORMAT_D16_UNORM_S8_UINT";
  case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";
  case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
  default: return "VkFormat(nil)";
  }
}

INTERNAL VkBool32
VulkanDebugLogCallback(VkDebugReportFlagsEXT flags,
                       VkDebugReportObjectTypeEXT obj_type,
                       uint64_t obj,
                       size_t location,
                       int32_t code,
                       const char* layer_prefix,
                       const char* msg,
                       void* user_data)
{
  (void)flags;
  (void)obj_type;
  (void)obj;
  (void)location;
  (void)user_data;
  if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    LOG_ERROR("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 0;
  } else if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ||
             (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) {
    LOG_WARN("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
    LOG_DEBUG("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
    LOG_INFO("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  return 1;
}

INTERNAL VkResult
CreateVkInstance(int enable_debug_layers,
                 const char* app_name, uint32_t app_version)
{
  const char** validation_layers = NULL;
  uint32_t layer_count = 0;
  if (enable_debug_layers) {
    validation_layers = PersistentAllocate(sizeof(const char*));
    validation_layers[0] = "VK_LAYER_KHRONOS_validation";
    layer_count++;
    // TODO: check if layer is present
  }

  // get available device extensions
  vkEnumerateInstanceExtensionProperties(NULL, &g_device->num_available_instance_extensions, NULL);
  g_device->available_instance_extensions =
    PersistentAllocate(sizeof(VkExtensionProperties) * g_device->num_available_instance_extensions);
  vkEnumerateInstanceExtensionProperties(NULL,
                                         &g_device->num_available_instance_extensions,
                                         g_device->available_instance_extensions);
  // get required instance extensions
  g_device->num_enabled_instance_extensions = 0;
  g_device->enabled_instance_extensions = PersistentAllocate(0);
  for (uint32_t i = 0; i < g_device->num_available_instance_extensions; i++) {
    const char* ext = NULL;
    const char* tmp = g_device->available_instance_extensions[i].extensionName;
    if ((enable_debug_layers && strcmp(tmp, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0)
        || strcmp(tmp, VK_KHR_SURFACE_EXTENSION_NAME) == 0
        || strcmp(tmp, "VK_KHR_win32_surface") == 0
        || strcmp(tmp, "VK_KHR_android_surface") == 0
        // || strcmp(tmp, VK_MVK_MACOS_SURFACE_EXTENSION_NAME) == 0
        // || strcmp(tmp, VK_MVK_IOS_SURFACE_EXTENSION_NAME) == 0
        || strcmp(tmp, "VK_KHR_xlib_surface") == 0
        || strcmp(tmp, "VK_KHR_xcb_surface") == 0
        || strcmp(tmp, "VK_KHR_wayland_surface") == 0
        // || strcmp(tmp, VK_KHR_DISPLAY_EXTENSION_NAME) == 0
        // || strcmp(tmp, VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME) == 0
        ) {
      ext = tmp;
    }
    if (ext) {
      PersistentAllocate(sizeof(char*));
      g_device->enabled_instance_extensions[g_device->num_enabled_instance_extensions] = ext;
      g_device->num_enabled_instance_extensions++;
    }
  }
  // finally create instance
  VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = app_name,
    .applicationVersion = app_version,
    .pEngineName = "lida",
    // TODO: use VK_MAKE_VERSION
    .engineVersion = LIDA_ENGINE_VERSION,
    .apiVersion = VK_API_VERSION_1_0
  };
  VkInstanceCreateInfo instance_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .enabledLayerCount = layer_count,
    .ppEnabledLayerNames = validation_layers,
    .enabledExtensionCount = g_device->num_enabled_instance_extensions,
    .ppEnabledExtensionNames = g_device->enabled_instance_extensions,
  };
  VkDebugReportCallbackCreateInfoEXT callback_info = {
    .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT|VK_DEBUG_REPORT_WARNING_BIT_EXT|VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
    .pfnCallback = &VulkanDebugLogCallback,
    .pUserData = NULL,
  };
  if (enable_debug_layers) {
    instance_info.pNext = &callback_info;
  }
  VkResult err = vkCreateInstance(&instance_info, NULL, &g_device->instance);
  if (err == VK_SUCCESS) {
    volkLoadInstance(g_device->instance);
    if (enable_debug_layers) {
      err = vkCreateDebugReportCallbackEXT(g_device->instance, &callback_info,
                                           NULL, &g_device->debug_report_callback);
      if (err != VK_SUCCESS) {
        LOG_WARN("failed to create debug report callback with error %s", ToString_VkResult(err));
      }
    } else {
      g_device->debug_report_callback = VK_NULL_HANDLE;
    }
  }
  return err;
}

INTERNAL VkResult
PickPhysicalDevice(uint32_t gpu_id)
{
  uint32_t count = 0;
  VkResult err = vkEnumeratePhysicalDevices(g_device->instance, &count, NULL);
  if (err != VK_SUCCESS)
    return err;
  VkPhysicalDevice* devices = PersistentAllocate(count * sizeof(VkPhysicalDevice));
  err = vkEnumeratePhysicalDevices(g_device->instance, &count, devices);
  if (gpu_id <= count) {
    g_device->physical_device = devices[gpu_id];
  } else {
    LOG_WARN("lida_DeviceDesc->gpu_id is out of bounds, picking GPU0");
    g_device->physical_device = devices[0];
  }
  PersistentRelease(devices);

  memset(&g_device->properties, 0, sizeof(VkPhysicalDeviceProperties));
  vkGetPhysicalDeviceProperties(g_device->physical_device, &g_device->properties);
  memset(&g_device->features, 0, sizeof(VkPhysicalDeviceFeatures));
  vkGetPhysicalDeviceFeatures(g_device->physical_device, &g_device->features);
  memset(&g_device->memory_properties, 0, sizeof(VkPhysicalDeviceMemoryProperties));
  vkGetPhysicalDeviceMemoryProperties(g_device->physical_device, &g_device->memory_properties);

  vkGetPhysicalDeviceQueueFamilyProperties(g_device->physical_device, &g_device->num_queue_families, NULL);
  g_device->queue_families = PersistentAllocate(g_device->num_queue_families * sizeof(VkQueueFamilyProperties));
  vkGetPhysicalDeviceQueueFamilyProperties(g_device->physical_device,
                                           &g_device->num_queue_families,
                                           g_device->queue_families);
  for (uint32_t i = 0; i < count; i++) {
    if (g_device->queue_families[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      g_device->graphics_queue_family = i;
      break;
    }
  }

  // get available extensions
  err = vkEnumerateDeviceExtensionProperties(g_device->physical_device, NULL,
                                             &g_device->num_available_device_extensions, NULL);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to enumerate device extensions with error %d", err);
  }
  g_device->available_device_extensions =
    PersistentAllocate(g_device->num_available_device_extensions * sizeof(VkExtensionProperties));
  err = vkEnumerateDeviceExtensionProperties(g_device->physical_device, NULL,
                                             &g_device->num_available_device_extensions,
                                             g_device->available_device_extensions);
  return err;
}

INTERNAL VkResult
CreateLogicalDevice(int enable_debug_layers,
                    const char** device_extensions,
                    uint32_t num_device_extensions)
{
  float queue_priorities[] = { 1.0f };
    VkDeviceQueueCreateInfo queueInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = g_device->graphics_queue_family,
    .queueCount = 1,
    .pQueuePriorities = queue_priorities,
  };

  if (num_device_extensions) {
    g_device->enabled_device_extensions = PersistentAllocate(0);
    g_device->num_enabled_device_extensions = 0;
    for (uint32_t i = 0; i < num_device_extensions; i++) {
      int found = 0;
      for (uint32_t j = 0; j < g_device->num_available_device_extensions; j++) {
        if (strcmp(device_extensions[i], g_device->available_device_extensions[j].extensionName) == 0) {
          PersistentAllocate(sizeof(const char*));
          g_device->enabled_device_extensions[g_device->num_enabled_device_extensions++] = device_extensions[i];
          found = 1;
          break;
        }
      }
      if (found == 0) {
        LOG_WARN("extension '%s' is not supported", device_extensions[i]);
      }
    }
    // add DEBUG_MARKER extension if debug layers are enabled
    if (enable_debug_layers) {
      // TODO: check if VK_EXT_debug_marker is supported
      PersistentAllocate(sizeof(const char*));
      g_device->enabled_device_extensions[g_device->num_enabled_device_extensions++] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
      g_device->debug_marker_enabled = 1;
    } else {
      g_device->debug_marker_enabled = 0;
    }
  } else {
    g_device->num_enabled_device_extensions = g_device->num_available_device_extensions;
    g_device->enabled_device_extensions =
      PersistentAllocate(g_device->num_enabled_device_extensions * sizeof(const char*));
    for (uint32_t i = 0; i < g_device->num_enabled_device_extensions; i++) {
      g_device->enabled_device_extensions[i] = g_device->available_device_extensions[i].extensionName;
    }
  }

  VkDeviceCreateInfo device_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queueInfo,
    .ppEnabledExtensionNames = g_device->enabled_device_extensions,
    .enabledExtensionCount = g_device->num_enabled_device_extensions,
    .pEnabledFeatures = &g_device->features,
  };
  return vkCreateDevice(g_device->physical_device, &device_info, NULL, &g_device->logical_device);
}

INTERNAL VkResult
DebugMarkObject(VkDebugReportObjectTypeEXT type, uint64_t obj, const char* name)
{
  VkResult err = VK_SUCCESS;
  if (g_device->debug_marker_enabled) {
    VkDebugMarkerObjectNameInfoEXT object_name_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
      .objectType = type,
      .object = obj,
      .pObjectName = name,
    };
    err = vkDebugMarkerSetObjectNameEXT(g_device->logical_device, &object_name_info);
  }
  return err;
}

INTERNAL VkResult
CreateDeviceCommandPool()
{
  VkCommandPoolCreateInfo command_pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = g_device->graphics_queue_family,
  };
  VkResult err = vkCreateCommandPool(g_device->logical_device, &command_pool_info, NULL,
                                     &g_device->command_pool);
  return err;
}

INTERNAL VkResult
CreateDeviceDescriptorPools()
{
  // tweak values here to reduce memory usage of application/add more space for descriptors
  VkDescriptorPoolSize sizes[] = {
    // { VK_DESCRIPTOR_TYPE_SAMPLER, 0 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
    // { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64 },
    // { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0 },
    // { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32 },
    // { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 0 },
    // { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 0 },
    // { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 16 },
  };
  VkDescriptorPoolCreateInfo pool_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 128,
    .poolSizeCount = ARR_SIZE(sizes),
    .pPoolSizes = sizes,
  };
  VkResult err = vkCreateDescriptorPool(g_device->logical_device, &pool_info, NULL,
                                        &g_device->static_ds_pool);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create pool for static resources with error %d", err);
    return err;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)g_device->static_ds_pool,
                        "static-descriptor-pool");
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to mark static descriptor pool with error %s", ToString_VkResult(err));
  }
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  err = vkCreateDescriptorPool(g_device->logical_device, &pool_info, NULL,
                               &g_device->dynamic_ds_pool);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create pool for dynamic resources with error %d", err);
    return err;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)g_device->static_ds_pool,
                        "dynamic-descriptor-pool");
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to mark dynamic descriptor pool with error %s", ToString_VkResult(err));
  }
  return err;
}

INTERNAL uint32_t
HashShaderInfo(const void* obj)
{
  const Shader_Info* shader = obj;
  return HashString32(shader->name);
}

INTERNAL int
CompareShaderInfo(const void* lhs, const void* rhs)
{
  const Shader_Info* l = lhs, *r = rhs;
  return strcmp(l->name, r->name);
}

INTERNAL uint32_t
HashDSL_Info(const void* obj)
{
  const DS_Layout_Info* info = obj;
  return HashMemory32(info->bindings, info->num_bindings * sizeof(VkDescriptorSetLayoutBinding));
}

INTERNAL int
CompareDSL_Infos(const void* lhs, const void* rhs)
{
  const DS_Layout_Info* l = lhs, *r = rhs;
  if (COMPARE(l->num_bindings, r->num_bindings) != 0)
    return COMPARE(l->num_bindings, r->num_bindings);
  return memcmp(l, r, l->num_bindings * sizeof(VkDescriptorSetLayoutBinding));
}

INTERNAL uint32_t
HashSamplerInfo(const void* obj)
{
  return HashMemory32(obj, sizeof(Sampler_Info) - sizeof(VkSampler));
}

INTERNAL int
CompareSamplerInfo(const void* lhs, const void* rhs)
{
  return memcmp(lhs, rhs, sizeof(Sampler_Info) - sizeof(VkSampler));
}

INTERNAL uint32_t
HashPipelineLayoutInfo(const void* obj)
{
  const Pipeline_Layout_Info* info = obj;
  uint32_t hashes[2];
  hashes[0] = HashMemory32(info->set_layouts, info->num_sets * sizeof(VkDescriptorSetLayout));
  hashes[1] = HashMemory32(info->ranges, info->num_ranges * sizeof(VkPushConstantRange));
  return HashCombine32(hashes, 2);
}

INTERNAL int
ComparePipelineLayoutInfo(const void* lhs, const void* rhs)
{
  const Pipeline_Layout_Info* l = lhs, *r = rhs;
  int ret = COMPARE(l->num_sets, r->num_sets);
  if (ret != 0) return ret;
  ret = memcmp(l->set_layouts, r->set_layouts, l->num_sets * sizeof(VkDescriptorSetLayout));
  if (ret != 0) return ret;
  ret = COMPARE(l->num_ranges, r->num_ranges);
  if (ret != 0) return ret;
  ret = memcmp(l->ranges, r->ranges, l->num_ranges * sizeof(VkPushConstantRange));
  return ret;
}


/// Functions used by other modules

INTERNAL VkResult
CreateDevice(int enable_debug_layers, uint32_t gpu_id,
             const char* app_name, uint32_t app_version,
             const char** device_extensions,
             uint32_t num_device_extensions)
{
  PROFILE_FUNCTION();
  // allocate memory for our big structure
  g_device = PersistentAllocate(sizeof(Device_Vulkan));

  // load vulkan functions
  VkResult err = volkInitialize();
  if (err != VK_SUCCESS) {
    LOG_FATAL("vulkan driver is not available on this platform");
    goto error;
  }
  // create instance
  err = CreateVkInstance(enable_debug_layers, app_name, app_version);
  if (err != VK_SUCCESS) {
    LOG_FATAL("failed to create vulkan instance with error %s", ToString_VkResult(err));
    goto error;
  }

  err = PickPhysicalDevice(gpu_id);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to pick physical device with error %s", ToString_VkResult(err));
  }
  err = CreateLogicalDevice(enable_debug_layers,
                            device_extensions, num_device_extensions);
  if (err != VK_SUCCESS) {
    LOG_FATAL("failed to create vulkan device with error %s", ToString_VkResult(err));
    goto error;
  }
  DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, (uint64_t)g_device->logical_device,
                  "lida-engine-device");

  // we use only 1 device in the application
  // so load device-related Vulkan entrypoints directly from the driver
  // for more info read https://github.com/zeux/volk#optimizing-device-calls
  volkLoadDevice(g_device->logical_device);
  // get graphics queue
  vkGetDeviceQueue(g_device->logical_device, g_device->graphics_queue_family, 0,
                   &g_device->graphics_queue);
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, (uint64_t)g_device->graphics_queue,
                        "graphics-queue");
  // create command pool, yes we have 1 command pool for whole application
  err = CreateDeviceCommandPool();
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create command pool with error %s", ToString_VkResult(err));
  }

  err = CreateDeviceDescriptorPools();
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create descriptor pool with error %s", ToString_VkResult(err));
  }

  g_device->shader_info_type = TYPE_INFO(Shader_Info, &HashShaderInfo, &CompareShaderInfo);
  g_device->ds_layout_info_type = TYPE_INFO(DS_Layout_Info, &HashDSL_Info, &CompareDSL_Infos);
  g_device->sampler_info_type = TYPE_INFO(Sampler_Info, &HashSamplerInfo, &CompareSamplerInfo);
  g_device->pipeline_layout_info_type = TYPE_INFO(Pipeline_Layout_Info, &HashPipelineLayoutInfo, &ComparePipelineLayoutInfo);
  const size_t num_shaders = 32;
  const size_t num_ds_layouts = 16;
  const size_t num_samplers = 8;
  const size_t num_pipeline_layouts = 16;
  // this just allocates a hash table of needed size
#define INIT_HT(type)   FHT_Init(&g_device->type##_cache, \
           PersistentAllocate(FHT_CALC_SIZE(&g_device->type##_info_type, num_##type##s)), \
           num_##type##s, &g_device->type##_info_type)
  INIT_HT(shader);
  INIT_HT(ds_layout);
  INIT_HT(sampler);
  INIT_HT(pipeline_layout);
#undef INIT_HT

#if 0
  // print info about available video memory
  for (uint32_t i = 0; i < g_device->memory_properties.memoryTypeCount; i++) {
    LOG_DEBUG("memory heap %u: flags=%u size=%lu", i,
              g_device->memory_properties.memoryTypes[i].propertyFlags,
              g_device->memory_properties.memoryHeaps[g_device->memory_properties.memoryTypes[i].heapIndex].size);
  }
#endif

  return VK_SUCCESS;
 error:
  PersistentRelease(g_device);
  return err;
}

INTERNAL void
DestroyDevice(int free_memory)
{
  PROFILE_FUNCTION();
  FHT_Iterator it;

  FHT_FOREACH(&g_device->pipeline_layout_cache, &g_device->pipeline_layout_info_type, &it) {
    Pipeline_Layout_Info* layout = FHT_IteratorGet(&it);
    vkDestroyPipelineLayout(g_device->logical_device, layout->handle, NULL);
  }

  FHT_FOREACH(&g_device->sampler_cache, &g_device->sampler_info_type, &it) {
    Sampler_Info* sampler = FHT_IteratorGet(&it);
    vkDestroySampler(g_device->logical_device, sampler->handle, NULL);
  }

  FHT_FOREACH(&g_device->ds_layout_cache, &g_device->ds_layout_info_type, &it) {
    DS_Layout_Info* layout = FHT_IteratorGet(&it);
    vkDestroyDescriptorSetLayout(g_device->logical_device, layout->layout, NULL);
  }

  FHT_FOREACH(&g_device->shader_cache, &g_device->shader_info_type, &it) {
    Shader_Info* shader = FHT_IteratorGet(&it);
    vkDestroyShaderModule(g_device->logical_device, shader->module, NULL);
  }

  vkDestroyDescriptorPool(g_device->logical_device, g_device->dynamic_ds_pool, NULL);
  vkDestroyDescriptorPool(g_device->logical_device, g_device->static_ds_pool, NULL);

  vkDestroyCommandPool(g_device->logical_device, g_device->command_pool, NULL);

  vkDestroyDevice(g_device->logical_device, NULL);
  if (g_device->debug_report_callback)
    vkDestroyDebugReportCallbackEXT(g_device->instance, g_device->debug_report_callback, NULL);
  vkDestroyInstance(g_device->instance, NULL);

  if (free_memory) {
    PersistentRelease(g_device->shader_cache.ptr);
    PersistentRelease(g_device->ds_layout_cache.ptr);
    PersistentRelease(g_device->sampler_cache.ptr);
    PersistentRelease(g_device->pipeline_layout_cache.ptr);

    PersistentRelease(g_device->enabled_device_extensions);
    PersistentRelease(g_device->available_device_extensions);
    PersistentRelease(g_device->queue_families);
    PersistentRelease(g_device->enabled_instance_extensions);
    PersistentRelease(g_device->available_instance_extensions);
    PersistentRelease(g_device);
  }
  g_device = NULL;
  LOG_INFO("destroyed device");
}

INTERNAL VkResult
CreateBuffer(VkBuffer* buffer, VkDeviceSize size, VkBufferUsageFlags usage, const char* marker)
{
  VkBufferCreateInfo buffer_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VkResult err = vkCreateBuffer(g_device->logical_device, &buffer_info, NULL, buffer);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create buffer '%s' with error %s", marker, ToString_VkResult(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, (uint64_t)*buffer, marker);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark buffer '%s' with error %s", marker, ToString_VkResult(err));
    }
  }
  return err;
}

INTERNAL VkResult
CreateRenderPass(VkRenderPass* render_pass, const VkRenderPassCreateInfo* render_pass_info, const char* marker)
{
  VkResult err = vkCreateRenderPass(g_device->logical_device, render_pass_info, NULL, render_pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create render pass '%s' with error %s",
              marker, ToString_VkResult(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, (uint64_t)*render_pass, marker);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark render pass '%s' with error %s",
               marker, ToString_VkResult(err));
    }
  }
  return err;
}

INTERNAL VkResult
CreateImage(VkImage* image, const VkImageCreateInfo* image_info, const char* marker)
{
  VkResult err = vkCreateImage(g_device->logical_device, image_info, NULL, image);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create image '%s' with error %s", marker, ToString_VkResult(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (uint64_t)*image, marker);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark image '%s' with error %s",
               marker, ToString_VkResult(err));
    }
  }
  return err;
}

INTERNAL VkResult
CreateImageView(VkImageView* image_view, const VkImageViewCreateInfo* image_view_info, const char* marker)
{
  VkResult err = vkCreateImageView(g_device->logical_device, image_view_info, NULL, image_view);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create image view '%s' with error %s", marker, ToString_VkResult(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)*image_view, marker);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark image view '%s' with error %s",
               marker, ToString_VkResult(err));
    }
  }
  return err;
}

INTERNAL VkResult
CreateFramebuffer(VkFramebuffer* framebuffer, const VkFramebufferCreateInfo* framebuffer_info, const char* marker)
{
  VkResult err = vkCreateFramebuffer(g_device->logical_device, framebuffer_info, NULL, framebuffer);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create framebuffer '%s' with error %s",
                   marker, ToString_VkResult(err));
  } else if (g_device->debug_marker_enabled) {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, (uint64_t)*framebuffer, marker);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark framebuffer '%s' with error %s",
                    marker, ToString_VkResult(err));
    }
  }
  return err;
}

INTERNAL VkResult
AllocateCommandBuffers(VkCommandBuffer* cmds, uint32_t count, VkCommandBufferLevel level,
                       const char* marker)
{
  VkCommandBufferAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = g_device->command_pool,
    .level = level,
    .commandBufferCount = count,
  };
  VkResult err = vkAllocateCommandBuffers(g_device->logical_device, &alloc_info, cmds);
  if (err == VK_SUCCESS) {
    char buff[64];
    for (uint32_t i = 0; i < count; i++) {
      stbsp_snprintf(buff, sizeof(buff), "%s[%u]", marker, i);
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, (uint64_t)cmds[i], buff);
      if (err != VK_SUCCESS) {
        LOG_WARN("failed to debug mark command buffers '%s' with error %s",
                 marker, ToString_VkResult(err));
        break;
      }
    }
  }
  return err;
}

INTERNAL VkResult
QueueSubmit(VkSubmitInfo* submits, uint32_t count, VkFence fence)
{
  return vkQueueSubmit(g_device->graphics_queue, count, submits, fence);
}

INTERNAL VkResult
QueuePresent(VkPresentInfoKHR* present_info)
{
  // FIXME: is it safe to use graphics queue? IDK, I think it should work on modern devices
  return vkQueuePresentKHR(g_device->graphics_queue, present_info);
}

INTERNAL VkMemoryPropertyFlags
GetVideoMemoryFlags(const Video_Memory* memory)
{
  return g_device->memory_properties.memoryTypes[memory->type].propertyFlags;
}

INTERNAL VkResult
AllocateVideoMemory(Video_Memory* memory, VkDeviceSize size,
                    VkMemoryPropertyFlags flags, uint32_t memory_type_bits,
                    const char* marker)
{
  uint32_t best_type = 0;
  for (uint32_t i = 0; i < g_device->memory_properties.memoryTypeCount; i++) {
    if ((g_device->memory_properties.memoryTypes[i].propertyFlags & flags) == flags &&
        (1 << i) & memory_type_bits) {
      // NOTE CLEANUP: this seems incorrect
      /*uint32_t a = g_device->memory_properties.memoryTypes[i].propertyFlags ^ flags;
      uint32_t b = g_device->memory_properties.memoryTypes[best_type].propertyFlags ^ flags;
      if (a < b) best_type = i;*/
      best_type = i;
      break;
    }
  }
  VkMemoryAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = size,
    .memoryTypeIndex = best_type,
  };
  VkResult err = vkAllocateMemory(g_device->logical_device, &allocate_info, NULL, &memory->handle);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory with error %s", ToString_VkResult(err));
    return err;
  }
  memory->offset = 0;
  memory->size = size;
  memory->type = best_type;
  if (GetVideoMemoryFlags(memory) & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    err = vkMapMemory(g_device->logical_device, memory->handle, 0, VK_WHOLE_SIZE, 0, &memory->mapped);
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to map memory with error %s", ToString_VkResult(err));
      return err;
    }
  } else {
    memory->mapped = NULL;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, (uint64_t)memory->handle, marker);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to mark memory '%s' with error %s", marker, ToString_VkResult(err));
  }
  return VK_SUCCESS;
}

INTERNAL void
FreeVideoMemory(Video_Memory* memory)
{
  if (memory->mapped) {
    vkUnmapMemory(g_device->logical_device, memory->handle);
  }
  vkFreeMemory(g_device->logical_device, memory->handle, NULL);
  memory->handle = VK_NULL_HANDLE;
}

INTERNAL void
ResetVideoMemory(Video_Memory* memory)
{
  memory->offset = 0;
}

INTERNAL void
MergeMemoryRequirements(const VkMemoryRequirements* requirements, size_t count, VkMemoryRequirements* out)
{
  memcpy(out, &requirements[0], sizeof(VkMemoryRequirements));
  for (size_t i = 1; i < count; i++) {
    out->size = ALIGN_TO(out->size, requirements[i].alignment) + requirements[i].size;
    out->memoryTypeBits &= requirements[i].memoryTypeBits;
  }
}

INTERNAL VkResult
ProvideVideoMemory(Video_Memory* memory, const VkMemoryRequirements* requirements)
{
  if (((1 << memory->type) & requirements->memoryTypeBits) == 0) {
    LOG_ERROR("buffer cannot be bound to memory. bits %u are needed, but bit %u is available",
              requirements->memoryTypeBits, memory->type);
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }
  memory->offset = ALIGN_TO(memory->offset, requirements->alignment);
  if (memory->offset > memory->size) {
    LOG_ERROR("out of video memory");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }
  return VK_SUCCESS;
}

INTERNAL VkResult
ImageBindToMemory(Video_Memory* memory, VkImage image, const VkMemoryRequirements* requirements)
{
  VkResult err = ProvideVideoMemory(memory, requirements);
  if (err != VK_SUCCESS) {
    return err;
  }
  err = vkBindImageMemory(g_device->logical_device, image, memory->handle, memory->offset);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind image to memory with error %s", ToString_VkResult(err));
  } else {
    memory->offset += requirements->size;
  }
  return err;
}

INTERNAL VkResult
BufferBindToMemory(Video_Memory* memory, VkBuffer buffer,
                   const VkMemoryRequirements* requirements, void** mapped,
                   VkMappedMemoryRange* mappedRange)
{
  VkResult err = ProvideVideoMemory(memory, requirements);
  if (err != VK_SUCCESS) {
    return err;
  }
  err = vkBindBufferMemory(g_device->logical_device, buffer, memory->handle, memory->offset);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind buffer to memory with error %s", ToString_VkResult(err));
  } else {
    if (mapped) {
      if (memory->mapped) {
        *mapped = (char*)memory->mapped + memory->offset;
      } else {
        LOG_WARN("memory is not mapped(%p), can't access it's contents from CPU", memory->mapped);
      }
      if (mappedRange) {
        mappedRange->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange->memory = memory->handle;
        mappedRange->offset = memory->offset;
        // Vulkan spec: If size is not equal to VK_WHOLE_SIZE, size must either be a multiple of
        // VkPhysicalDeviceLimits::nonCoherentAtomSize, or offset plus size must equal the size
        // of memory.
        mappedRange->size = ALIGN_TO(requirements->size, g_device->properties.limits.nonCoherentAtomSize);
      }
    }
    memory->offset += requirements->size;
  }
  return err;
}

typedef struct {

  uint32_t opcode;
  union {
    struct {
      uint32_t typeId;
      uint32_t storageClass;
      uint32_t binding;
      uint32_t set;
      uint32_t inputAttachmentIndex;
    } binding;
    struct {
      uint32_t integerWidth;
      int integerSigned;
    } val_int;
    struct {
      uint32_t floatWidth;
    } val_float;
    struct {
      uint32_t componentTypeId;
      uint32_t numComponents;
    } val_vec;
    struct {
      const uint32_t* memberTypes;
      uint32_t numMemberTypes;
      SpvDecoration structType;
    } val_struct;
    struct {
      uint32_t elementTypeId;
      uint32_t sizeConstantId;
    } val_array;
    struct {
      uint32_t constantType;
      uint32_t constantValue;
    } val_const;
  } data;

} SPIRV_ID;

INTERNAL uint32_t
SPIRV_ComputeTypeSize(SPIRV_ID* ids, uint32_t id, uint32_t current_size/*for alignment*/)
{
  // NOTE about alignment: https://stackoverflow.com/a/45641579
  uint32_t offset = 0, alignment = 0;
  switch (ids[id].opcode) {
  case SpvOpTypeStruct:
    // A structure has a base alignment equal to the largest base alignment of
    // any of its members, rounded up to a multiple of 16.
    for (uint32_t typeId = 0; typeId < ids[id].data.val_struct.numMemberTypes; typeId++) {
      uint32_t member_size = SPIRV_ComputeTypeSize(ids, ids[id].data.val_struct.memberTypes[typeId], offset);
      offset += member_size;
      if (member_size > alignment)
        alignment = member_size;
    }
    break;
  case SpvOpTypeArray:
    // An array has a base alignment equal to the base alignment of its element type,
    // rounded up to a multiple of 16.
    {
      uint32_t arr_size = ids[ids[id].data.val_array.sizeConstantId].data.val_const.constantValue;
      // FIXME: I feel like we calculating alignment in wrong way
      uint32_t elem_alignment = SPIRV_ComputeTypeSize(ids, ids[id].data.val_array.elementTypeId, 0);
      alignment = ALIGN_TO(arr_size, 16 * elem_alignment);
      offset = arr_size * elem_alignment;
    }
    break;
  case SpvOpTypeFloat:
    return ids[id].data.val_float.floatWidth >> 3;
  case SpvOpTypeInt:
    return ids[id].data.val_int.integerWidth >> 3;
  case SpvOpTypeMatrix:
    // A column-major matrix has a base alignment equal to the base alignment of the matrix column type.
    // FIXME: should we check that matrix is row-major?
    {
      uint32_t vec_id = ids[id].data.val_vec.componentTypeId;
      uint32_t vec_size = SPIRV_ComputeTypeSize(ids, vec_id, 0);
      offset = ids[id].data.val_vec.numComponents * vec_size;
      uint32_t elem_size = SPIRV_ComputeTypeSize(ids, ids[vec_id].data.val_vec.componentTypeId, 0);
      alignment = ALIGN_TO(ids[vec_id].data.val_vec.numComponents, 2) * elem_size;
    }
    break;
  case SpvOpTypeVector:
    // A two-component vector, with components of size N, has a base alignment of 2 N.
    // A three- or four-component vector, with components of size N, has a base alignment of 4 N.
    {
      uint32_t component_size = SPIRV_ComputeTypeSize(ids, ids[id].data.val_vec.componentTypeId, 0);
      offset = ids[id].data.val_vec.numComponents * component_size;
      uint32_t num_components = ALIGN_TO(ids[id].data.val_vec.numComponents, 2);
      alignment = num_components * component_size;
    }
    break;
  default:
    assert(0 && "unrecognized type");
  }
  return ALIGN_TO(current_size, alignment) - current_size + offset;
}

INTERNAL int
ReflectSPIRV(const uint32_t* code, uint32_t size, Shader_Reflect* shader)
{
  // based on https://github.com/zeux/niagara/blob/98f5d5ae2b48e15e145e3ad13ae7f4f9f1e0e297/src/shaders.cpp#L45
  // https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#_physical_layout_of_a_spir_v_module_and_instruction
  // this tool also helped me a lot: https://www.khronos.org/spir/visualizer/
  if (code[0] != SpvMagicNumber) {
    LOG_WARN("code is not valid SPIR-V");
    return -1;
  }
  uint32_t id_bound = code[3];
  SPIRV_ID* ids = PersistentAllocate(sizeof(SPIRV_ID) * id_bound);
  memset(ids, 0, sizeof(SPIRV_ID) * id_bound);
  for (uint32_t i = 0; i < id_bound; i++) {
    ids->data.binding.inputAttachmentIndex = UINT32_MAX;
  }
  const uint32_t* ins = code + 5;
  const uint32_t* const end = code + size;

  // parse all opcodes
  while (ins != end) {
    SpvOp opcode = ins[0] & 0xffff;
    uint32_t word_count = ins[0] >> 16;
    switch (opcode) {
    case SpvOpEntryPoint:
      Assert(word_count >= 2);
      switch (ins[1]) {
      case SpvExecutionModelVertex: shader->stages = VK_SHADER_STAGE_VERTEX_BIT; break;
      case SpvExecutionModelFragment: shader->stages = VK_SHADER_STAGE_FRAGMENT_BIT; break;
      case SpvExecutionModelGLCompute: shader->stages = VK_SHADER_STAGE_COMPUTE_BIT; break;
      default: Assert(0 && "SPIR-V: invalid shader stage");
      }
      break;
    case SpvOpExecutionMode:
      Assert(word_count >= 3);
      switch (ins[2]) {
      case SpvExecutionModeLocalSize:
        Assert(word_count == 6);
        shader->localX = ins[3];
        shader->localY = ins[4];
        shader->localZ = ins[5];
        break;
      }
      break;
    case SpvOpDecorate:
      Assert(word_count >= 3);
      // ins[1] is id of entity that describes current instruction
      Assert(ins[1] < id_bound);
      switch (ins[2]) {
      case SpvDecorationDescriptorSet:
        Assert(word_count == 4);
        ids[ins[1]].data.binding.set = ins[3];
        break;
      case SpvDecorationBinding:
        Assert(word_count == 4);
        ids[ins[1]].data.binding.binding = ins[3];
        break;
      case SpvDecorationBlock:
      case SpvDecorationBufferBlock:
        ids[ins[1]].data.val_struct.structType = ins[2];
        break;
      case SpvDecorationInputAttachmentIndex:
        ids[ins[1]].data.binding.inputAttachmentIndex = ins[3];
        break;
      }
      break;
    case SpvOpTypeStruct:
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_struct.memberTypes = ins + 2;
      ids[ins[1]].data.val_struct.numMemberTypes = word_count - 2;
      break;
    case SpvOpTypeImage:
    case SpvOpTypeSampler:
    case SpvOpTypeSampledImage:
      Assert(word_count >= 2);
      Assert(ins[1] < id_bound);
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      break;
    case SpvOpTypeInt:
      Assert(word_count == 4);
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_int.integerWidth = ins[2];
      ids[ins[1]].data.val_int.integerSigned = ins[3];
      break;
    case SpvOpTypeFloat:
      Assert(word_count == 3);
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_float.floatWidth = ins[2];
      break;
    case SpvOpTypeVector:
    case SpvOpTypeMatrix:
      Assert(word_count == 4);
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_vec.componentTypeId = ins[2];
      ids[ins[1]].data.val_vec.numComponents = ins[3];
      break;
    case SpvOpTypeArray:
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_array.elementTypeId = ins[2];
      ids[ins[1]].data.val_array.sizeConstantId = ins[3];
      break;
    case SpvOpTypePointer:
      Assert(word_count == 4);
      Assert(ins[1] < id_bound);
      Assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.binding.storageClass = ins[2];
      ids[ins[1]].data.binding.typeId = ins[3];
      break;
    case SpvOpVariable:
      Assert(word_count >= 4);
      // ins[2] is id
      Assert(ins[2] < id_bound);
      Assert(ids[ins[2]].opcode == 0);
      ids[ins[2]].opcode = opcode;
      ids[ins[2]].data.binding.typeId = ins[1];
      ids[ins[2]].data.binding.storageClass = ins[3];
      break;
    case SpvOpConstant:
      Assert(ids[ins[2]].opcode == 0);
      ids[ins[2]].opcode = opcode;
      ids[ins[2]].data.val_const.constantType = ins[1];
      ids[ins[2]].data.val_const.constantValue = ins[3];
      break;
      // avoid warnings from GCC
    default:
      break;
    }
    ins += word_count;
  }

  shader->set_count = 0;
  shader->range_count = 0;
  memset(shader->sets, 0, sizeof(Binding_Set_Desc) * SHADER_REFLECT_MAX_SETS);

  // use ids that we parsed to collect reflection data
  for (uint32_t i = 0; i < id_bound; i++) {
    SPIRV_ID* id = &ids[i];

    if (id->opcode == SpvOpVariable &&
        (id->data.binding.storageClass == SpvStorageClassUniform ||
         id->data.binding.storageClass == SpvStorageClassUniformConstant ||
         id->data.binding.storageClass == SpvStorageClassStorageBuffer)) {
      // process uniform
      Assert(id->data.binding.set < SHADER_REFLECT_MAX_SETS &&
             "descriptor set number is bigger than max value");
      if (id->data.binding.set+1 > shader->set_count)
        shader->set_count = id->data.binding.set+1;
      Assert(id->data.binding.binding < SHADER_REFLECT_MAX_BINDINGS_PER_SET &&
             "descriptor binding number is bigger than max value");
      Assert(ids[id->data.binding.typeId].opcode == SpvOpTypePointer);
      Binding_Set_Desc* set = &shader->sets[id->data.binding.set];
      VkDescriptorType* ds_type = &set->bindings[set->binding_count].descriptorType;
      switch (ids[ids[id->data.binding.typeId].data.binding.typeId].opcode) {
      case SpvOpTypeStruct:
        switch (ids[ids[id->data.binding.typeId].data.binding.typeId].data.val_struct.structType) {
        case SpvDecorationBlock:
          *ds_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          break;
        case SpvDecorationBufferBlock:
          *ds_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
          break;
        default: break;
        }
        break;
      case SpvOpTypeImage:
        if (id->data.binding.inputAttachmentIndex != UINT32_MAX) {
          *ds_type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        } else {
          *ds_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        break;
      case SpvOpTypeSampler:
        *ds_type = VK_DESCRIPTOR_TYPE_SAMPLER;
        break;
      case SpvOpTypeSampledImage:
        *ds_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        break;
      default:
        Assert(0 && "Unknown resource type");
        break;
      }

      set->bindings[set->binding_count].binding = id->data.binding.binding;
      set->bindings[set->binding_count].descriptorCount = 1;
      set->bindings[set->binding_count].stageFlags = shader->stages;
      set->binding_count++;
    } else if (id->opcode == SpvOpVariable &&
               id->data.binding.storageClass == SpvStorageClassPushConstant) {
      // process push constant
      Assert(ids[id->data.binding.typeId].data.binding.storageClass == SpvStorageClassPushConstant);
      shader->ranges[shader->range_count] = (VkPushConstantRange) {
        .stageFlags = shader->stages,
        .offset = 0,
        .size = SPIRV_ComputeTypeSize(ids, ids[id->data.binding.typeId].data.binding.typeId, 0),
      };
      shader->range_count++;
    }

  }

  PersistentRelease(ids);
  return 0;
}

// load a shader (SPIR-V format) and parse its contents parse results
// are then writed to 'reflect'. 'reflect' can be NULL
INTERNAL VkShaderModule
LoadShader(const char* path, const Shader_Reflect** reflect)
{
  // check if we already have loaded this shader
  Shader_Info* info = FHT_Search(&g_device->shader_cache, &g_device->shader_info_type, &path);
  if (info) {
    if (reflect) {
      *reflect = &info->reflect;
    }
    return info->module;
  }
  // load the shader
  size_t buffer_size = 0;
  uint32_t* buffer = PlatformLoadEntireFile(path, &buffer_size);
  if (!buffer) {
    LOG_ERROR("failed to load shader from file '%s' with error '%s'", path, PlatformGetError());
    return VK_NULL_HANDLE;
  }
  VkShaderModuleCreateInfo module_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = buffer_size,
    .pCode = buffer,
  };
  VkShaderModule ret = VK_NULL_HANDLE;
  VkResult err = vkCreateShaderModule(g_device->logical_device, &module_info,
                                      NULL, &ret);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create shader module with error %s", ToString_VkResult(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, (uint64_t)ret, path);
    if (err != VK_SUCCESS) {
      LOG_WARN("failed to mark shader module '%s' with error %s", path, ToString_VkResult(err));
    }
    // Insert shader to cache if succeeded
    Shader_Info* shader_info = FHT_Insert(&g_device->shader_cache, &g_device->shader_info_type,
                                          &(Shader_Info) { .name = path, .module = ret });
    ReflectSPIRV(buffer, buffer_size / sizeof(uint32_t), &shader_info->reflect);
    if (reflect) {
      *reflect = &shader_info->reflect;
    }
  }
  PlatformFreeLoadedFile(buffer);
  return ret;
}

INTERNAL VkResult
ForceUpdateShader(const char* path)
{
  size_t buffer_size = 0;
  uint32_t* buffer = PlatformLoadEntireFile(path, &buffer_size);
  if (!buffer) {
    LOG_ERROR("failed to load shader from file '%s' with error '%s'", path, PlatformGetError());
    return VK_ERROR_UNKNOWN;
  }
  VkShaderModuleCreateInfo module_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = buffer_size,
    .pCode = buffer,
  };
  VkShaderModule ret = VK_NULL_HANDLE;
  VkResult err = vkCreateShaderModule(g_device->logical_device, &module_info,
                                      NULL, &ret);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create shader module with error %s", ToString_VkResult(err));
    return err;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, (uint64_t)ret, path);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to mark shader module '%s' with error %s", path, ToString_VkResult(err));
  }
  // Insert shader to cache if succeeded
  Shader_Info* shader_info = FHT_Search(&g_device->shader_cache, &g_device->shader_info_type,
                                        &(Shader_Info) { .name = path });
  if (shader_info == NULL) {
    LOG_ERROR("shader '%s' was not created before", path);
    vkDestroyShaderModule(g_device->logical_device, ret, NULL);
    return VK_ERROR_UNKNOWN;
  }
  // destroy old shader module
  vkDestroyShaderModule(g_device->logical_device, shader_info->module, NULL);
  shader_info->module = ret;
  ReflectSPIRV(buffer, buffer_size / sizeof(uint32_t), &shader_info->reflect);
  PlatformFreeLoadedFile(buffer);
  return VK_SUCCESS;
}

INTERNAL int
Compare_DSLB(const void* l, const void* r)
{
  return memcmp(l, r, sizeof(VkDescriptorSetLayoutBinding));
}

INTERNAL VkDescriptorSetLayout
GetDescriptorSetLayout(const VkDescriptorSetLayoutBinding* bindings, size_t num_bindings)
{
  DS_Layout_Info key;
  key.num_bindings = num_bindings;
  Assert(num_bindings <= ARR_SIZE(key.bindings));
  memcpy(key.bindings, bindings, sizeof(VkDescriptorSetLayoutBinding));
  QuickSort(key.bindings, num_bindings, sizeof(VkDescriptorSetLayoutBinding), &Compare_DSLB);
  DS_Layout_Info* layout = FHT_Search(&g_device->ds_layout_cache, &g_device->ds_layout_info_type, &key);
  if (layout) {
    return layout->layout;
  }
    VkDescriptorSetLayoutCreateInfo layout_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = num_bindings,
    .pBindings = bindings,
  };
  VkResult err = vkCreateDescriptorSetLayout(g_device->logical_device, &layout_info, NULL, &key.layout);
  VkDescriptorSetLayout ret = key.layout;
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create descriptor layout with error %s", ToString_VkResult(err));
  }
  FHT_Insert(&g_device->ds_layout_cache, &g_device->ds_layout_info_type, &key);
  return ret;
}

INTERNAL VkSampler
GetSampler(VkFilter filter, VkSamplerAddressMode mode, VkBorderColor border_color)
{
  Sampler_Info sampler;
  sampler.filter = filter;
  sampler.mode = mode;
  sampler.border_color = border_color;
  // try to look if have this sampler in cache
  Sampler_Info* it = FHT_Search(&g_device->sampler_cache, &g_device->sampler_info_type, &sampler);
  if (it) {
    return it->handle;
  }
  // create a new sampler
  VkSamplerCreateInfo sampler_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = filter,
    .minFilter = filter,
    .mipmapMode = (filter == VK_FILTER_NEAREST) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .addressModeU = mode,
    .addressModeV = mode,
    .addressModeW = mode,
    .minLod = 0.0f,
    .maxLod = 1.0f,
    .borderColor = border_color
  };
  VkResult err = vkCreateSampler(g_device->logical_device, &sampler_info, NULL, &sampler.handle);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create sampler with error %s", ToString_VkResult(err));
    return VK_NULL_HANDLE;
  }
  // add sampler to cache if succeeded
  FHT_Insert(&g_device->sampler_cache, &g_device->sampler_info_type, &sampler);
  return sampler.handle;
}

INTERNAL void
MergeShaderReflects(Shader_Reflect* lhs, const Shader_Reflect* rhs)
{
  lhs->stages |= rhs->stages;
  if (rhs->set_count > lhs->set_count) lhs->set_count = rhs->set_count;
  for (uint32_t i = 0; i < rhs->set_count; i++) {
    const Binding_Set_Desc* set = &rhs->sets[i];
    uint32_t* pCount = &lhs->sets[i].binding_count;
    uint32_t old_count = *pCount;
    for (uint32_t j = 0; j < set->binding_count; j++) {
      int found = 0;
      for (uint32_t k = 0; old_count; k++) {
        VkDescriptorSetLayoutBinding* binding = &lhs->sets[j].bindings[k];
        if (binding->binding == set->bindings[j].binding) {
          if (binding->descriptorType != set->bindings[j].descriptorType ||
              binding->descriptorCount != set->bindings[j].descriptorCount) {
            LOG_WARN("shader merge error: different uniforms have the same binding number");
          }
          binding->stageFlags |= set->bindings[j].stageFlags;
          found = 1;
          break;
        }
      }
      if (!found) {
        // add binding
        Assert(*pCount < SHADER_REFLECT_MAX_BINDINGS_PER_SET &&
               "shader reflect merge: binding number overflow, "
               "try to use less number of bindings per set");
        memcpy(&lhs->sets[i].bindings[*pCount], &set->bindings[j],
               sizeof(VkDescriptorSetLayoutBinding));
        (*pCount)++;
      }
    }
  }
  for (uint32_t i = 0; i < rhs->range_count; i++) {
    const VkPushConstantRange* lrange, *rrange;
    int found = 0;
    rrange = &rhs->ranges[i];
    for (uint32_t j = 0; j < lhs->range_count; j++) {
      lrange = &lhs->ranges[j];
      if (lrange->offset == rrange->offset &&
          lrange->size == rrange->size) {
        found = 1;
        break;
      }
    }
    if (!found) {
      Assert(lhs->range_count < SHADER_REFLECT_MAX_RANGES &&
             "shader reflect merge: push constant number overflow");
      memcpy(&lhs->ranges[lhs->range_count], rrange, sizeof(VkPushConstantRange));
      lhs->range_count++;
    }
  }
}

INTERNAL Shader_Reflect*
CollectShaderReflects(const Shader_Reflect** shaders, size_t count)
{
  Shader_Reflect* shader = PersistentAllocate(sizeof(Shader_Reflect));
  memcpy(shader, shaders[0], sizeof(Shader_Reflect));
  for (size_t i = 1; i < count; i++) {
    MergeShaderReflects(shader, shaders[i]);
  }
  return shader;
}

INTERNAL VkPipelineLayout
CreatePipelineLayout(const Shader_Reflect** shader_templates, size_t count)
{
  // NOTE: I didn't figure out a good way to debug mark pipeline layout.
  // Leaving it unmarked
  const Shader_Reflect* shader;
  Pipeline_Layout_Info layout_info = { .num_sets = 0, .num_ranges = 0 };
  if (count > 0) {
    if (count == 1) {
      shader = shader_templates[0];
    } else {
      shader = CollectShaderReflects(shader_templates, count);
    }
    layout_info.num_sets = shader->set_count;
    for (uint32_t i = 0; i < shader->set_count; i++) {
      layout_info.set_layouts[i] = GetDescriptorSetLayout(shader->sets[i].bindings,
                                                          shader->sets[i].binding_count);
    }
    layout_info.num_ranges = shader->range_count;
    for (uint32_t i = 0; i < shader->range_count; i++) {
      layout_info.ranges[i] = shader->ranges[i];
    }
  }
  Pipeline_Layout_Info* it = FHT_Search(&g_device->pipeline_layout_cache,
                                        &g_device->pipeline_layout_info_type, &layout_info);
  if (it) {
    return it->handle;
  }
  // create a new pipeline layout
  VkPipelineLayoutCreateInfo pipeline_layout = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = layout_info.num_sets,
    .pSetLayouts = layout_info.set_layouts,
    .pushConstantRangeCount = layout_info.num_ranges,
    .pPushConstantRanges = layout_info.ranges,
  };
  VkResult err = vkCreatePipelineLayout(g_device->logical_device, &pipeline_layout, NULL,
                                        &layout_info.handle);
  VkPipelineLayout ret = layout_info.handle;
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create pipeline layout with error %s", ToString_VkResult(err));
  } else {
    // add pipeline layout to cache if succeeded
    FHT_Insert(&g_device->pipeline_layout_cache, &g_device->pipeline_layout_info_type, &layout_info);
  }
  if (count > 1)
    PersistentRelease((void*)shader);
  return ret;
}

INTERNAL VkResult
CreateGraphicsPipelines(VkPipeline* pipelines, size_t count, const Pipeline_Desc* descs, VkPipelineLayout* layouts)
{
  PROFILE_FUNCTION();
  // allocate some structures
  VkGraphicsPipelineCreateInfo* create_infos = PersistentAllocate(count * sizeof(VkGraphicsPipelineCreateInfo));
  VkPipelineShaderStageCreateInfo* stages = PersistentAllocate(2 * count * sizeof(VkPipelineShaderStageCreateInfo));
  VkShaderModule* modules = PersistentAllocate(2 * count * sizeof(VkShaderModule));
  const Shader_Reflect** reflects = PersistentAllocate(2 * count * sizeof(Shader_Reflect*));
  VkPipelineVertexInputStateCreateInfo* vertex_input_states = PersistentAllocate(count * sizeof(VkPipelineVertexInputStateCreateInfo));
  VkPipelineInputAssemblyStateCreateInfo* input_assembly_states = PersistentAllocate(count * sizeof(VkPipelineInputAssemblyStateCreateInfo));
  VkPipelineViewportStateCreateInfo* viewport_states = PersistentAllocate(count * sizeof(VkPipelineViewportStateCreateInfo));
  VkPipelineRasterizationStateCreateInfo* rasterization_states = PersistentAllocate(count * sizeof(VkPipelineRasterizationStateCreateInfo));
  VkPipelineMultisampleStateCreateInfo* multisample_states = PersistentAllocate(count * sizeof(VkPipelineMultisampleStateCreateInfo));
  VkPipelineDepthStencilStateCreateInfo* depth_stencil_states = PersistentAllocate(count * sizeof(VkPipelineDepthStencilStateCreateInfo));
  VkPipelineColorBlendStateCreateInfo* color_blend_states = PersistentAllocate(count * sizeof(VkPipelineColorBlendStateCreateInfo));
  VkPipelineDynamicStateCreateInfo* dynamic_states = PersistentAllocate(count * sizeof(VkPipelineDynamicStateCreateInfo));
  for (size_t i = 0; i < count; i++) {

    modules[2*i] = LoadShader(descs[i].vertex_shader, &reflects[2*i]);
    stages[2*i] = (VkPipelineShaderStageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = modules[2*i],
      .pName = "main"
    };
    if (descs[i].fragment_shader) {
      // some pipelines may have not a fragment shader
      modules[2*i+1] = LoadShader(descs[i].fragment_shader, &reflects[2*i+1]);
      stages[2*i+1] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = modules[2*i+1],
        .pName = "main"
      };
    }

    layouts[i] = CreatePipelineLayout(&reflects[2*i],
                                      (descs[i].fragment_shader) ? 2 : 1);

    vertex_input_states[i] = (VkPipelineVertexInputStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = descs[i].vertex_binding_count,
      .pVertexBindingDescriptions = descs[i].vertex_bindings,
      .vertexAttributeDescriptionCount = descs[i].vertex_attribute_count,
      .pVertexAttributeDescriptions = descs[i].vertex_attributes
    };
    input_assembly_states[i] = (VkPipelineInputAssemblyStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = descs[i].topology,
      // currently we don't use primitiveRestartEnable in lidaEngine
      .primitiveRestartEnable = VK_FALSE
    };

    viewport_states[i] = (VkPipelineViewportStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      // currently we always 1 viewport and 1 scissor, maybe I should
      // add an option to use multiple scissors. I saw that ImGui uses
      // multiple scissors for rendering.
      .viewportCount = 1,
      .pViewports = descs[i].viewport,
      .scissorCount = 1,
      .pScissors = descs[i].scissor,
    };

    rasterization_states[i] = (VkPipelineRasterizationStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = descs[i].polygonMode,
      .cullMode = descs[i].cullMode,
      // we always use CCW
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable = descs[i].depth_bias_enable,
      .lineWidth = descs[i].line_width
    };

    multisample_states[i] = (VkPipelineMultisampleStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = descs[i].msaa_samples,
      .sampleShadingEnable = VK_FALSE,
    };

    depth_stencil_states[i] = (VkPipelineDepthStencilStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = descs[i].depth_test,
      .depthWriteEnable = descs[i].depth_write,
      .depthCompareOp = descs[i].depth_compare_op,
      // we're not using depth bounds
      .depthBoundsTestEnable = VK_FALSE,
    };

    color_blend_states[i] = (VkPipelineColorBlendStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = descs[i].blend_logic_enable,
      .logicOp = descs[i].blend_logic_op,
      .attachmentCount = descs[i].attachment_count,
      .pAttachments = descs[i].attachments,
    };

    memcpy(color_blend_states[i].blendConstants, descs[i].blend_constants, sizeof(float)*4);
    dynamic_states[i] = (VkPipelineDynamicStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = descs[i].dynamic_state_count,
      .pDynamicStates = descs[i].dynamic_states,
    };

    create_infos[i] = (VkGraphicsPipelineCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = (descs[i].fragment_shader) ? 2 : 1,
      .pStages = &stages[i*2],
      .pVertexInputState = &vertex_input_states[i],
      .pInputAssemblyState = &input_assembly_states[i],
      .pViewportState = &viewport_states[i],
      .pRasterizationState = &rasterization_states[i],
      .pMultisampleState = &multisample_states[i],
      // I think it's pretty convenient to specify depth_write = 0 and
      // depth_test = 0 to say that pipeline doesn't use depth buffer.
      .pDepthStencilState = (descs[i].depth_write || descs[i].depth_test) ? &depth_stencil_states[i] : NULL,
      .pColorBlendState = &color_blend_states[i],
      .pDynamicState = &dynamic_states[i],
      .layout = layouts[i],
      .renderPass = descs[i].render_pass,
      .subpass = descs[i].subpass
    };

  }

  VkResult err = vkCreateGraphicsPipelines(g_device->logical_device, VK_NULL_HANDLE,
                                           count, create_infos, VK_NULL_HANDLE, pipelines);

  PersistentRelease(dynamic_states);
  PersistentRelease(color_blend_states);
  PersistentRelease(depth_stencil_states);
  PersistentRelease(multisample_states);
  PersistentRelease(viewport_states);
  PersistentRelease(input_assembly_states);
  PersistentRelease(vertex_input_states);
  PersistentRelease(reflects);
  PersistentRelease(modules);
  PersistentRelease(stages);
  PersistentRelease(create_infos);

  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create graphics pipelines with error %s", ToString_VkResult(err));
  } else {
    for (size_t i = 0; i < count; i++) {
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, (uint64_t)pipelines[i],
                            descs[i].marker);
      if (err != VK_SUCCESS) {
        LOG_WARN("failed to debug mark graphics pipeline '%s' with error %s", descs[i].marker,
                  ToString_VkResult(err));
      }
    }
  }
  return err;
}

INTERNAL VkResult
CreateComputePipelines(VkPipeline* pipelines, size_t count, const char* shaders[], VkPipelineLayout* layouts)
{
  PROFILE_FUNCTION();
  // allocate some structures
  VkComputePipelineCreateInfo* create_infos = PersistentAllocate(count * sizeof(VkComputePipelineCreateInfo));
  VkShaderModule* modules = PersistentAllocate(count * sizeof(VkShaderModule));
  const Shader_Reflect** reflects = PersistentAllocate(count * sizeof(Shader_Reflect*));
  for (size_t i = 0; i < count; i++) {
    modules[i] = LoadShader(shaders[i], &reflects[i]);
    layouts[i] = CreatePipelineLayout(&reflects[i], 1);
    create_infos[i] = (VkComputePipelineCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = modules[i],
        .pName = "main"
      },
      .layout = layouts[i]
    };
  }
  VkResult err = vkCreateComputePipelines(g_device->logical_device, VK_NULL_HANDLE,
                                          count, create_infos, VK_NULL_HANDLE, pipelines);
  PersistentRelease(reflects);
  PersistentRelease(modules);
  PersistentRelease(create_infos);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create compute pipelines with error %s", ToString_VkResult(err));
  } else {
    for (size_t i = 0; i < count; i++) {
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, (uint64_t)pipelines[i],
                            shaders[i]);
      if (err != VK_SUCCESS) {
        LOG_WARN("failed to debug mark graphics pipeline '%s' with error %s", shaders[i],
                 ToString_VkResult(err));
      }
    }
  }
  return err;
}

INTERNAL VkResult
AllocateDescriptorSets(const VkDescriptorSetLayoutBinding* bindings, size_t num_bindings,
                       VkDescriptorSet* sets, size_t num_sets, int dynamic,
                       const char* marker)
{
  // find appropriate descriptor layout
  VkDescriptorSetLayout layout = GetDescriptorSetLayout(bindings, num_bindings);
  VkDescriptorSetLayout* layouts = PersistentAllocate(num_sets * sizeof(VkDescriptorSetLayout));
  for (size_t i = 0; i < num_sets; i++)
    layouts[i] = layout;
  VkDescriptorSetAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = (dynamic) ? g_device->dynamic_ds_pool : g_device->static_ds_pool,
      .descriptorSetCount = num_sets,
      .pSetLayouts = layouts,
  };
  VkResult err = vkAllocateDescriptorSets(g_device->logical_device, &allocate_info, sets);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to allocate descriptor sets with error %s", ToString_VkResult(err));
  }
  PersistentRelease(layouts);
  if (err == VK_SUCCESS) {
    // do naming
    char buff[128];
    const char* type = (dynamic != 0 ? "resetable" : "static");
    for (uint32_t i = 0; i < num_sets; i++) {
      stbsp_snprintf(buff, sizeof(buff), "%s[%u]-%s", marker, i, type);
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, (uint64_t)sets[i], buff);
      if (err != VK_SUCCESS) {
        LOG_WARN("failed to debug marker descriptor sets '%s' with error %s", marker,
                 ToString_VkResult(err));
        break;
      }
    }
  }
  return err;
}

INTERNAL VkResult
FreeDescriptorSets(const VkDescriptorSet* sets, size_t num_sets)
{
  return vkFreeDescriptorSets(g_device->logical_device, g_device->dynamic_ds_pool,
                              num_sets, sets);
}

INTERNAL void
UpdateDescriptorSets(const VkWriteDescriptorSet* pDescriptorWrites, size_t count)
{
  vkUpdateDescriptorSets(g_device->logical_device, count, pDescriptorWrites, 0, NULL);
}

INTERNAL void
ResetDynamicDescriptorSets()
{
  vkResetDescriptorPool(g_device->logical_device, g_device->dynamic_ds_pool, 0);
}

INTERNAL VkFormat
FindSupportedFormat(VkFormat* options, uint32_t count, VkImageTiling tiling, VkFormatFeatureFlags flags)
{
  VkFormatProperties properties;
  for (uint32_t i = 0; i < count; i++) {
    vkGetPhysicalDeviceFormatProperties(g_device->physical_device, options[i], &properties);
    if ((tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & flags)) ||
        (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & flags))) {
      return options[i];
    }
  }
  // I hope we will never run in this case...
  return VK_FORMAT_MAX_ENUM;
}

INTERNAL VkSampleCountFlagBits
MaxSampleCount(VkSampleCountFlagBits max_samples)
{
  VkSampleCountFlags flags =
    g_device->properties.limits.framebufferColorSampleCounts &
    g_device->properties.limits.framebufferDepthSampleCounts &
    // FIXME: should we use this? we don't currently use stencil test
    g_device->properties.limits.framebufferStencilSampleCounts;
  // SAMPLE_COUNT_1 is guaranteed to work
  VkSampleCountFlagBits ret = VK_SAMPLE_COUNT_1_BIT;
  VkSampleCountFlagBits options[] = {
    VK_SAMPLE_COUNT_2_BIT,
    VK_SAMPLE_COUNT_4_BIT,
    VK_SAMPLE_COUNT_8_BIT,
    VK_SAMPLE_COUNT_16_BIT,
    VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_64_BIT,
  };
  for (uint32_t i = 0; i < ARR_SIZE(options); i++) {
    if ((options[i] <= max_samples) && (options[i] & flags))
      ret = options[i];
  }
  return ret;
}
