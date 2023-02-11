/*
  Vulkan device creation and a lot of vulkan abstraction.
 */

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

  /* lida_TypeInfo shader_info_type; */
  /* lida_HashTable shader_cache; */

  /* lida_TypeInfo ds_layout_info_type; */
  /* lida_HashTable ds_layout_cache; */

  /* lida_TypeInfo sampler_info_type; */
  /* lida_HashTable sampler_cache; */

  /* lida_TypeInfo pipeline_layout_info_type; */
  /* lida_HashTable pipeline_layout_cache; */

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;

} Device_Vulkan;

GLOBAL Device_Vulkan* g_device;


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


/// Functions used by other modules

INTERNAL VkResult
CreateDevice(int enable_debug_layers, uint32_t gpu_id,
             const char* app_name, uint32_t app_version,
             const char** device_extensions,
             uint32_t num_device_extensions)
{
  // load vulkan functions
  VkResult err = volkInitialize();
  if (err != VK_SUCCESS) {
    LOG_FATAL("vulkan driver is not available on this platform");
    goto error;
  }
  // allocate memory for our big structure
  g_device = PersistentAllocate(sizeof(Device_Vulkan));
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
