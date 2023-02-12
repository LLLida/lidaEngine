/*
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
  uint32_t set_count;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  uint32_t range_count;

} Shader_Reflect;

typedef struct {

  const char* name;
  VkShaderModule module;
  Shader_Reflect reflect;

} Shader_Info;

typedef struct {

  VkDescriptorSetLayoutBinding bindings[16];
  uint32_t num_bindings;
  VkDescriptorSetLayout layout;

} DS_Layout_Info;

typedef struct {

  VkFilter filter;
  VkSamplerAddressMode mode;
  VkSampler handle;

} Sampler_Info;

typedef struct {

  VkDescriptorSetLayout set_layouts[SHADER_REFLECT_MAX_SETS];
  uint32_t num_sets;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  uint32_t num_ranges;
  VkPipelineLayout handle;

} Pipeline_Layout_Info;


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
    LOG_DEBUG("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  return 1;
}

INTERNAL VkResult
CreateVkInstance(int enable_debug_layers,
                 const char* app_name, uint32_t app_version)
{
  const char** validation_layers;
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
  PersistentPop(devices);

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
    g_device->num_enabled_device_extensions = num_device_extensions + (enable_debug_layers != 0);
    g_device->enabled_device_extensions =
      PersistentAllocate(num_device_extensions * sizeof(const char*));
    for (uint32_t i = 0; i < num_device_extensions; i++) {
      g_device->enabled_device_extensions[i] = device_extensions[i];
    }
    // add DEBUG_MARKER extension if debug layers are enabled
    if (enable_debug_layers) {
      g_device->enabled_device_extensions[num_device_extensions] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
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
  return HashMemory32(obj, sizeof(DS_Layout_Info) - sizeof(VkDescriptorSetLayout));
}

INTERNAL int
CompareDSL_Infos(const void* lhs, const void* rhs)
{
  return memcmp(lhs, rhs, sizeof(DS_Layout_Info) - sizeof(VkDescriptorSetLayout));
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
  return HashMemory32(obj, sizeof(Pipeline_Layout_Info) - sizeof(VkPipelineLayout));
}

INTERNAL int
ComparePipelineLayoutInfo(const void* lhs, const void* rhs)
{
  return memcmp(lhs, rhs, sizeof(Pipeline_Layout_Info) - sizeof(VkPipelineLayout));
}


/// Functions used by other modules

INTERNAL VkResult
CreateDevice(int enable_debug_layers, uint32_t gpu_id,
             const char* app_name, uint32_t app_version,
             const char** device_extensions,
             uint32_t num_device_extensions)
{
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

  g_device->shader_info_type = TYPE_INFO(Shader_Info, &HashShaderInfo, &CompareShaderInfo);
  g_device->ds_layout_info_type = TYPE_INFO(DS_Layout_Info, &HashDSL_Info, &CompareDSL_Infos);
  g_device->sampler_info_type = TYPE_INFO(Sampler_Info, &HashSamplerInfo, &CompareSamplerInfo);
  g_device->pipeline_layout_info_type = TYPE_INFO(Pipeline_Layout_Info, &HashPipelineLayoutInfo, &ComparePipelineLayoutInfo);
  const size_t num_shaders = 32;
  const size_t num_ds_layouts = 16;
  const size_t num_samplers = 8;
  const size_t num_pipeline_layouts = 16;
  FHT_Init(&g_device->shader_cache,
           PersistentAllocate(sizeof(Shader_Info)*num_shaders),
           num_shaders, &g_device->shader_info_type);
  FHT_Init(&g_device->ds_layout_cache,
           PersistentAllocate(sizeof(DS_Layout_Info)*num_ds_layouts),
           num_ds_layouts, &g_device->ds_layout_info_type);
  FHT_Init(&g_device->sampler_cache,
           PersistentAllocate(sizeof(Sampler_Info)*num_samplers),
           num_samplers, &g_device->ds_layout_info_type);
  FHT_Init(&g_device->pipeline_layout_cache,
           PersistentAllocate(sizeof(Pipeline_Layout_Info)*num_pipeline_layouts),
           num_pipeline_layouts, &g_device->pipeline_layout_info_type);

  return VK_SUCCESS;
 error:
  PersistentPop(g_device);
  return err;
}

INTERNAL void
DestroyDevice(int free_memory)
{
  vkDestroyCommandPool(g_device->logical_device, g_device->command_pool, NULL);

  vkDestroyDevice(g_device->logical_device, NULL);
  if (g_device->debug_report_callback)
    vkDestroyDebugReportCallbackEXT(g_device->instance, g_device->debug_report_callback, NULL);
  vkDestroyInstance(g_device->instance, NULL);

  if (free_memory) {
    PersistentPop(g_device->shader_cache.ptr);
    PersistentPop(g_device->ds_layout_cache.ptr);
    PersistentPop(g_device->sampler_cache.ptr);
    PersistentPop(g_device->pipeline_layout_cache.ptr);

    PersistentPop(g_device->enabled_device_extensions);
    PersistentPop(g_device->available_device_extensions);
    PersistentPop(g_device->queue_families);
    PersistentPop(g_device->enabled_instance_extensions);
    PersistentPop(g_device->available_instance_extensions);
    PersistentPop(g_device);
  }
  g_device = NULL;
  LOG_INFO("destroyed device");
}

INTERNAL VkResult
RenderPassCreate(VkRenderPass* render_pass, const VkRenderPassCreateInfo* render_pass_info, const char* marker)
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
ImageCreate(VkImage* image, const VkImageCreateInfo* image_info, const char* marker)
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
ImageViewCreate(VkImageView* image_view, const VkImageViewCreateInfo* image_view_info, const char* marker)
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
FramebufferCreate(VkFramebuffer* framebuffer, const VkFramebufferCreateInfo* framebuffer_info, const char* marker)
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
    if ((g_device->memory_properties.memoryTypes[i].propertyFlags & flags) &&
        (1 << i) & memory_type_bits) {
      uint32_t a = g_device->memory_properties.memoryTypes[i].propertyFlags ^ flags;
      uint32_t b = g_device->memory_properties.memoryTypes[best_type].propertyFlags ^ flags;
      if (a < b) best_type = i;
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
      assert(word_count >= 2);
      switch (ins[1]) {
      case SpvExecutionModelVertex: shader->stages = VK_SHADER_STAGE_VERTEX_BIT; break;
      case SpvExecutionModelFragment: shader->stages = VK_SHADER_STAGE_FRAGMENT_BIT; break;
      case SpvExecutionModelGLCompute: shader->stages = VK_SHADER_STAGE_COMPUTE_BIT; break;
      default: assert(0 && "SPIR-V: invalid shader stage");
      }
      break;
    case SpvOpExecutionMode:
      assert(word_count >= 3);
      switch (ins[2]) {
      case SpvExecutionModeLocalSize:
        assert(word_count == 6);
        shader->localX = ins[3];
        shader->localY = ins[4];
        shader->localZ = ins[5];
        break;
      }
      break;
    case SpvOpDecorate:
      assert(word_count >= 3);
      // ins[1] is id of entity that describes current instruction
      assert(ins[1] < id_bound);
      switch (ins[2]) {
      case SpvDecorationDescriptorSet:
        assert(word_count == 4);
        ids[ins[1]].data.binding.set = ins[3];
        break;
      case SpvDecorationBinding:
        assert(word_count == 4);
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
      assert(word_count >= 2);
      assert(ins[1] < id_bound);
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      break;
    case SpvOpTypeInt:
      assert(word_count == 4);
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_int.integerWidth = ins[2];
      ids[ins[1]].data.val_int.integerSigned = ins[3];
      break;
    case SpvOpTypeFloat:
      assert(word_count == 3);
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_float.floatWidth = ins[2];
      break;
    case SpvOpTypeVector:
    case SpvOpTypeMatrix:
      assert(word_count == 4);
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_vec.componentTypeId = ins[2];
      ids[ins[1]].data.val_vec.numComponents = ins[3];
      break;
    case SpvOpTypeArray:
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.val_array.elementTypeId = ins[2];
      ids[ins[1]].data.val_array.sizeConstantId = ins[3];
      break;
    case SpvOpTypePointer:
      assert(word_count == 4);
      assert(ins[1] < id_bound);
      assert(ids[ins[1]].opcode == 0);
      ids[ins[1]].opcode = opcode;
      ids[ins[1]].data.binding.storageClass = ins[2];
      ids[ins[1]].data.binding.typeId = ins[3];
      break;
    case SpvOpVariable:
      assert(word_count >= 4);
      // ins[2] is id
      assert(ins[2] < id_bound);
      assert(ids[ins[2]].opcode == 0);
      ids[ins[2]].opcode = opcode;
      ids[ins[2]].data.binding.typeId = ins[1];
      ids[ins[2]].data.binding.storageClass = ins[3];
      break;
    case SpvOpConstant:
      assert(ids[ins[2]].opcode == 0);
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
      assert(id->data.binding.set < SHADER_REFLECT_MAX_SETS &&
             "descriptor set number is bigger than max value");
      if (id->data.binding.set+1 > shader->set_count)
        shader->set_count = id->data.binding.set+1;
      assert(id->data.binding.binding < SHADER_REFLECT_MAX_BINDINGS_PER_SET &&
             "descriptor binding number is bigger than max value");
      assert(ids[id->data.binding.typeId].opcode == SpvOpTypePointer);
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
        assert(0 && "Unknown resource type");
        break;
      }

      set->bindings[set->binding_count].binding = id->data.binding.binding;
      set->bindings[set->binding_count].descriptorCount = 1;
      set->bindings[set->binding_count].stageFlags = shader->stages;
      set->binding_count++;
    } else if (id->opcode == SpvOpVariable &&
               id->data.binding.storageClass == SpvStorageClassPushConstant) {
      // process push constant
      assert(ids[id->data.binding.typeId].data.binding.storageClass == SpvStorageClassPushConstant);
      shader->ranges[shader->range_count] = (VkPushConstantRange) {
        .stageFlags = shader->stages,
        .offset = 0,
        .size = SPIRV_ComputeTypeSize(ids, ids[id->data.binding.typeId].data.binding.typeId, 0),
      };
      shader->range_count++;
    }

  }

  PersistentPop(ids);
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
  PlatformFreeFile(buffer);
  return ret;
}
