#include "device.h"
#include "memory.h"

#include <SDL_rwops.h>
#include <string.h>
#include <stdio.h> // for printf

typedef struct {

  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice logical_device;
  uint32_t graphics_queue_family;
  VkQueue graphics_queue;
  /* uint32_t present_queue_family; */
  VkDebugReportCallbackEXT debug_report_callback;
  VkCommandPool command_pool;

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

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;

} lida_Device;

lida_Device* g_device = NULL;

static VkResult CreateInstance(const lida_DeviceDesc* desc);
static VkResult PickPhysicalDevice(const lida_DeviceDesc* desc);
static VkResult CreateLogicalDevice(const lida_DeviceDesc* desc);
static VkResult CreateCommandPool();


VkResult
lida_DeviceCreate(const lida_DeviceDesc* desc)
{
  // load vulkan functions
  VkResult err = volkInitialize();
  if (err != VK_SUCCESS) {
    printf("vulkan driver is not present on this platform\n");
    return err;
  }
  // allocate memory for our big structure
  g_device = lida_TempAllocate(sizeof(lida_Device));
  err = CreateInstance(desc);
  if (err != VK_SUCCESS) {
    printf("failed to create instance %d\n", err);
    return err;
  }
  err = PickPhysicalDevice(desc);
  if (err != VK_SUCCESS) {
    printf("failed to pick a GPU with error %d\n", err);
  }
  err =  CreateLogicalDevice(desc);
  if (err != VK_SUCCESS) {
    printf("failed to create vulkan device with error %d\n", err);
  }
  // we use only 1 device in the application
  // so load device-related Vulkan entrypoints directly from the driver
  // for more info read https://github.com/zeux/volk#optimizing-device-calls
  volkLoadDevice(g_device->logical_device);
  vkGetDeviceQueue(g_device->logical_device, g_device->graphics_queue_family, 0,
                   &g_device->graphics_queue);
  err = CreateCommandPool();
  if (err != VK_SUCCESS) {
    printf("failed to create command pool with error %d\n", err);
  }
  return err;
}

void
lida_DeviceDestroy(int fast)
{
  vkDestroyCommandPool(g_device->logical_device, g_device->command_pool, NULL);
  vkDestroyDevice(g_device->logical_device, NULL);
  if (g_device->debug_report_callback)
    vkDestroyDebugReportCallbackEXT(g_device->instance, g_device->debug_report_callback, NULL);
  vkDestroyInstance(g_device->instance, NULL);

  if (!fast) {
    lida_TempFree(g_device->enabled_device_extensions);
    lida_TempFree(g_device->available_device_extensions);
    lida_TempFree(g_device->queue_families);
    lida_TempFree(g_device->enabled_instance_extensions);
    lida_TempFree(g_device->available_instance_extensions);
    lida_TempFree(g_device);
  }

  g_device = NULL;
}

VkInstance
lida_GetVulkanInstance()
{
  return g_device->instance;
}

VkDevice
lida_GetLogicalDevice()
{
  return g_device->logical_device;
}

VkPhysicalDevice
lida_GetPhysicalDevice()
{
  return g_device->physical_device;
}


const char**
lida_GetEnabledInstanceExtensions()
{
  return g_device->enabled_instance_extensions;
}

uint32_t
lida_GetNumEnabledInstanceExtensions()
{
  return g_device->num_enabled_instance_extensions;
}

const VkExtensionProperties*
lida_GetAvailableInstanceExtensions()
{
  return g_device->available_instance_extensions;
}

uint32_t
lida_GetNumAvailableInstanceExtensions()
{
  return g_device->num_available_instance_extensions;
}

const char**
lida_GetEnabledDeviceExtensions()
{
  return g_device->enabled_device_extensions;
}

uint32_t
lida_GetNumEnabledDeviceExtensions()
{
  return g_device->num_enabled_device_extensions;
}

const VkExtensionProperties*
lida_GetAvailableDeviceExtensions()
{
  return g_device->available_device_extensions;
}

uint32_t
lida_GetNumAvailableDeviceExtensions()
{
  return g_device->num_available_device_extensions;
}

uint32_t
lida_GetGraphicsQueueFamily()
{
  return g_device->graphics_queue_family;
}

VkResult
lida_AllocateCommandBuffers(VkCommandBuffer* cmds, uint32_t count, VkCommandBufferLevel level)
{
  VkCommandBufferAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = g_device->command_pool,
    .level = level,
    .commandBufferCount = count,
  };
  return vkAllocateCommandBuffers(g_device->logical_device, &alloc_info, cmds);
}

VkResult
lida_QueueSubmit(VkSubmitInfo* submits, uint32_t count, VkFence fence)
{
  return vkQueueSubmit(g_device->graphics_queue, count, submits, fence);
}

VkResult
lida_QueuePresent(VkPresentInfoKHR* present_info)
{
  // is it safe to use graphics queue? IDK, I think it should work on modern devices
  return vkQueuePresentKHR(g_device->graphics_queue, present_info);
}

VkShaderModule
lida_LoadShader(const char* path)
{
  size_t buffer_size = 0;
  uint32_t* buffer = SDL_LoadFile(path, &buffer_size);
  if (!buffer) {
    printf("failed to load shader from file '%s' with error '%s'\n", path, SDL_GetError());
    return VK_NULL_HANDLE;
  }
  VkShaderModuleCreateInfo module_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = buffer_size,
    .pCode = buffer,
  };
  VkShaderModule ret;
  VkResult err = vkCreateShaderModule(g_device->logical_device, &module_info,
                                      NULL, &ret);
  SDL_free(buffer);
  if (err != VK_SUCCESS) {
    printf("failed to create shader module with error %d\n", err);
    return VK_NULL_HANDLE;
  }
  return ret;
}


/// static functions

static VkBool32
DebugLogCallback(VkDebugReportFlagsEXT flags,
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
  (void)code;
  (void)layer_prefix;
  (void)user_data;
  printf("Vulkan error: %s\n", msg);
  return 1;
}

VkResult
CreateInstance(const lida_DeviceDesc* desc)
{
  const char** validation_layers;
  uint32_t layer_count = 0;
  if (desc->enable_debug_layers) {
    validation_layers = lida_TempAllocate(sizeof(const char*));
    validation_layers[0] = "VK_LAYER_KHRONOS_validation";
    layer_count++;
    // FIXME: should we check if validation layer is present?
  }

  // Get available instance extensions
  vkEnumerateInstanceExtensionProperties(NULL, &g_device->num_available_instance_extensions, NULL);
  g_device->available_instance_extensions =
    lida_TempAllocate(sizeof(VkExtensionProperties) * g_device->num_available_instance_extensions);
  vkEnumerateInstanceExtensionProperties(NULL,
                                         &g_device->num_available_instance_extensions,
                                         g_device->available_instance_extensions);

  // Get required instance extensions
#if 1
  g_device->num_enabled_instance_extensions = 0;
  g_device->enabled_instance_extensions = lida_TempAllocate(0);
  for (uint32_t i = 0; i < g_device->num_available_instance_extensions; i++) {
    const char* ext = NULL;
    const char* tmp = g_device->available_instance_extensions[i].extensionName;
    if ((desc->enable_debug_layers && strcmp(tmp, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0)
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
      lida_TempAllocate(sizeof(const char*));
      g_device->enabled_instance_extensions[g_device->num_enabled_instance_extensions] = ext;
      g_device->num_enabled_instance_extensions++;
    }
  }
#else
  // FIXME: is it safe to pass window = NULL?
  // I briefly checked SDL2 source code and it seems to me that this is safe
  SDL_Window* dummy_window = SDL_CreateWindow("dummy", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 100, 100, SDL_WINDOW_VULKAN);
  SDL_Vulkan_GetInstanceExtensions(dummy_window, &g_device->num_enabled_instance_extensions, NULL);
  if (desc->enable_debug_layers) {
    g_device->enabled_instance_extensions =
      lida_TempAllocate(sizeof(const char*) * (g_device->num_enabled_instance_extensions + 1));
    SDL_Vulkan_GetInstanceExtensions(dummy_window,
                                     &g_device->num_enabled_instance_extensions,
                                     g_device->enabled_instance_extensions);
    g_device->enabled_instance_extensions[g_device->num_enabled_instance_extensions] = "VK_EXT_debug_report";
    g_device->num_enabled_instance_extensions++;
  } else {
    g_device->enabled_instance_extensions =
      lida_TempAllocate(sizeof(const char*) * g_device->num_enabled_instance_extensions);
    SDL_Vulkan_GetInstanceExtensions(dummy_window,
                                     &g_device->num_enabled_instance_extensions,
                                     g_device->enabled_instance_extensions);
  }
  SDL_DestroyWindow(dummy_window);
#endif

  VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = desc->app_name,
    .applicationVersion = desc->app_version,
    .pEngineName = "lida",
    .engineVersion = VK_MAKE_VERSION(0, 0, 2),
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
    .pfnCallback = &DebugLogCallback,
    .pUserData = NULL,
    };
  if (desc->enable_debug_layers) {
    instance_info.pNext = &callback_info;
  }
  VkResult err = vkCreateInstance(&instance_info, NULL, &g_device->instance);
  if (err == VK_SUCCESS) {
    volkLoadInstance(g_device->instance);
    if (desc->enable_debug_layers) {
      err = vkCreateDebugReportCallbackEXT(g_device->instance, &callback_info,
                                           NULL, &g_device->debug_report_callback);
      if (err != VK_SUCCESS) {
        printf("Failed to create debug report callback\n");
      }
    }
  }
  return err;
}

VkResult
PickPhysicalDevice(const lida_DeviceDesc* desc)
{
  uint32_t count = 0;
  VkResult err = vkEnumeratePhysicalDevices(g_device->instance, &count, NULL);
  if (err != VK_SUCCESS)
    return err;
  VkPhysicalDevice* devices = lida_TempAllocate(count * sizeof(VkPhysicalDevice));
  err = vkEnumeratePhysicalDevices(g_device->instance, &count, devices);
  if (desc->gpu_id <= count) {
    g_device->physical_device = devices[desc->gpu_id];
  } else {
    printf("warning: lida_DeviceDesc->gpu_id is out of bounds, picking GPU0\n");
    g_device->physical_device = devices[0];
  }
  lida_TempFree(devices);

  memset(&g_device->properties, 0, sizeof(VkPhysicalDeviceProperties));
  vkGetPhysicalDeviceProperties(g_device->physical_device, &g_device->properties);
  memset(&g_device->properties, 0, sizeof(VkPhysicalDeviceFeatures));
  vkGetPhysicalDeviceFeatures(g_device->physical_device, &g_device->features);
  memset(&g_device->properties, 0, sizeof(VkPhysicalDeviceMemoryProperties));
  vkGetPhysicalDeviceMemoryProperties(g_device->physical_device, &g_device->memory_properties);

  vkGetPhysicalDeviceQueueFamilyProperties(g_device->physical_device, &g_device->num_queue_families, NULL);
  g_device->queue_families = lida_TempAllocate(g_device->num_queue_families * sizeof(VkQueueFamilyProperties));
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
    printf("failed to enumerate device extensions with error %d\n", err);
  }
  g_device->available_device_extensions =
    lida_TempAllocate(g_device->num_available_device_extensions * sizeof(VkExtensionProperties));
  err = vkEnumerateDeviceExtensionProperties(g_device->physical_device, NULL,
                                             &g_device->num_available_device_extensions,
                                             g_device->available_device_extensions);
  return err;
}

VkResult
CreateLogicalDevice(const lida_DeviceDesc* desc)
{
  float queuePriorities[] = { 1.0f };
  VkDeviceQueueCreateInfo queueInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = g_device->graphics_queue_family,
    .queueCount = 1,
    .pQueuePriorities = queuePriorities,
  };

  if (desc->num_device_extensions) {
    g_device->num_enabled_device_extensions = desc->num_device_extensions;
    g_device->enabled_device_extensions =
      lida_TempAllocate(desc->num_device_extensions * sizeof(const char*));
    for (uint32_t i = 0; i < desc->num_device_extensions; i++) {
      g_device->enabled_device_extensions[i] = desc->device_extensions[i];
    }
  } else {
    g_device->num_enabled_device_extensions = g_device->num_available_device_extensions;
    g_device->enabled_device_extensions =
      lida_TempAllocate(g_device->num_enabled_device_extensions * sizeof(const char*));
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

VkResult CreateCommandPool()
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
