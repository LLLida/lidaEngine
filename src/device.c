#include "device.h"
#include "base.h"
#include "memory.h"
#include "lib/spirv.h"

#include <assert.h>
#include <SDL_rwops.h>
#include <string.h>

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

  lida_TypeInfo shader_info_type;
  lida_HashTable shader_cache;

  lida_TypeInfo ds_layout_info_type;
  lida_HashTable ds_layout_cache;

  lida_TypeInfo sampler_info_type;
  lida_HashTable sampler_cache;

  lida_TypeInfo pipeline_layout_info_type;
  lida_HashTable pipeline_layout_cache;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;

} lida_Device;

lida_Device* g_device = NULL;

#define SHADER_REFLECT_MAX_SETS 8
#define SHADER_REFLECT_MAX_BINDINGS_PER_SET 16
#define SHADER_REFLECT_MAX_RANGES 4

typedef struct {
  VkDescriptorSetLayoutBinding bindings[SHADER_REFLECT_MAX_BINDINGS_PER_SET];
  uint32_t binding_count;
} BindingSetDesc;

struct lida_ShaderReflect {
  VkShaderStageFlags stages;
  uint32_t localX, localY, localZ;
  BindingSetDesc sets[SHADER_REFLECT_MAX_SETS];
  uint32_t set_count;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  uint32_t range_count;
};

typedef struct {
  const char* name;
  VkShaderModule module;
  lida_ShaderReflect reflect;
} ShaderInfo;

typedef struct {
  VkDescriptorSetLayoutBinding bindings[16];
  uint32_t num_bindings;
  VkDescriptorSetLayout layout;
} DS_LayoutInfo;

typedef struct {
  VkFilter filter;
  VkSamplerAddressMode mode;
  VkSampler handle;
} SamplerInfo;

typedef struct {
  VkDescriptorSetLayout set_layouts[SHADER_REFLECT_MAX_SETS];
  uint32_t num_sets;
  VkPushConstantRange ranges[SHADER_REFLECT_MAX_RANGES];
  uint32_t num_ranges;
  VkPipelineLayout handle;
} PipelineLayoutInfo;

static VkResult CreateInstance(const lida_DeviceDesc* desc);
static VkResult PickPhysicalDevice(const lida_DeviceDesc* desc);
static VkResult CreateLogicalDevice(const lida_DeviceDesc* desc);
static VkResult CreateCommandPool();
static VkResult CreateDescriptorPools();
static VkResult DebugMarkObject(VkDebugReportObjectTypeEXT type, uint64_t obj, const char* name);
static VkResult VideoMemoryProvide(lida_VideoMemory* memory, const VkMemoryRequirements* requirements);
static int Compare_DSLB(const void* l, const void* r);
static void MergeShaderReflects(lida_ShaderReflect* lhs, const lida_ShaderReflect* rhs);
static lida_ShaderReflect* CollectShaderReflects(const lida_ShaderReflect** shaders, uint32_t count);
static int ReflectSPIRV(const uint32_t* code, uint32_t size, lida_ShaderReflect* shader);


VkResult
lida_DeviceCreate(const lida_DeviceDesc* desc)
{
  LIDA_PROFILE_FUNCTION();
  // load vulkan functions
  VkResult err = volkInitialize();
  if (err != VK_SUCCESS) {
    LIDA_LOG_FATAL("vulkan driver is not present on this platform");
    return err;
  }
  // allocate memory for our big structure
  g_device = lida_TempAllocate(sizeof(lida_Device));
  err = CreateInstance(desc);
  if (err != VK_SUCCESS) {
    LIDA_LOG_FATAL("failed to create instance with error %s", lida_VkResultToString(err));
    return err;
  }
  err = PickPhysicalDevice(desc);
  if (err != VK_SUCCESS) {
    LIDA_LOG_FATAL("failed to pick a GPU with error %s", lida_VkResultToString(err));
  }
  err = CreateLogicalDevice(desc);
  if (err != VK_SUCCESS) {
    LIDA_LOG_FATAL("failed to create vulkan device with error %s", lida_VkResultToString(err));
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, (uint64_t)g_device->logical_device, "lida-engine-device");
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to debug mark Vulkan device object with error %s", lida_VkResultToString(err));
  }
  // we use only 1 device in the application
  // so load device-related Vulkan entrypoints directly from the driver
  // for more info read https://github.com/zeux/volk#optimizing-device-calls
  volkLoadDevice(g_device->logical_device);
  vkGetDeviceQueue(g_device->logical_device, g_device->graphics_queue_family, 0,
                   &g_device->graphics_queue);
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, (uint64_t)g_device->graphics_queue, "graphics-queue");
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to mark graphics queue with error %s", lida_VkResultToString(err));
  }
  err = CreateCommandPool();
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create command pool with error %s", lida_VkResultToString(err));
  }

  err = CreateDescriptorPools();

  g_device->shader_info_type =
    LIDA_TYPE_INFO(ShaderInfo, lida_MallocAllocator(), 0);
  g_device->shader_cache = LIDA_HT_EMPTY(&g_device->shader_info_type);

  g_device->ds_layout_info_type =
    LIDA_TYPE_INFO(DS_LayoutInfo, lida_MallocAllocator(),
                   0);
  g_device->ds_layout_cache = LIDA_HT_EMPTY(&g_device->ds_layout_info_type);

  g_device->sampler_info_type =
    LIDA_TYPE_INFO(SamplerInfo, lida_MallocAllocator(),
                   0);
  g_device->sampler_cache = LIDA_HT_EMPTY(&g_device->sampler_info_type);

  g_device->pipeline_layout_info_type =
    LIDA_TYPE_INFO(PipelineLayoutInfo, lida_MallocAllocator(),
                   0);
  g_device->pipeline_layout_cache = LIDA_HT_EMPTY(&g_device->pipeline_layout_info_type);

  return err;
}

void
lida_DeviceDestroy(int fast)
{
  LIDA_PROFILE_FUNCTION();
  lida_HT_Iterator it;

  // clear pipeline layout cache
  LIDA_HT_FOREACH(&g_device->pipeline_layout_cache, &it) {
    PipelineLayoutInfo* layout = lida_HT_Iterator_Get(&it);
    vkDestroyPipelineLayout(g_device->logical_device, layout->handle, NULL);
  }
  lida_HT_Delete(&g_device->pipeline_layout_cache);

  // clear sampler cache
  LIDA_HT_FOREACH(&g_device->sampler_cache, &it) {
    SamplerInfo* sampler = lida_HT_Iterator_Get(&it);
    vkDestroySampler(g_device->logical_device, sampler->handle, NULL);
  }
  lida_HT_Delete(&g_device->sampler_cache);

  // clear descriptor layout cache
  LIDA_HT_FOREACH(&g_device->ds_layout_cache, &it) {
    DS_LayoutInfo* layout = lida_HT_Iterator_Get(&it);
    vkDestroyDescriptorSetLayout(g_device->logical_device, layout->layout, NULL);
  }
  lida_HT_Delete(&g_device->ds_layout_cache);

  // clear shader cache
  LIDA_HT_FOREACH(&g_device->shader_cache, &it) {
    ShaderInfo* shader = lida_HT_Iterator_Get(&it);
    vkDestroyShaderModule(g_device->logical_device, shader->module, NULL);
  }
  lida_HT_Delete(&g_device->shader_cache);

  vkDestroyDescriptorPool(g_device->logical_device, g_device->dynamic_ds_pool, NULL);
  vkDestroyDescriptorPool(g_device->logical_device, g_device->static_ds_pool, NULL);

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

const VkPhysicalDeviceProperties*
lida_GetDeviceProperties()
{
  return &g_device->properties;
}

uint32_t
lida_GetGraphicsQueueFamily()
{
  return g_device->graphics_queue_family;
}

VkQueue
lida_GetGraphicsQueue()
{
  return g_device->graphics_queue;
}

VkResult
lida_AllocateCommandBuffers(VkCommandBuffer* cmds, uint32_t count, VkCommandBufferLevel level, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
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
      snprintf(buff, sizeof(buff), "%s[%u]", marker, i);
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, (uint64_t)cmds[i], buff);
      if (err != VK_SUCCESS) {
        LIDA_LOG_WARN("failed to debug mark command buffers '%s' with error %s",
                      marker, lida_VkResultToString(err));
        break;
      }
    }
  }
  return err;
}

VkResult
lida_QueueSubmit(VkSubmitInfo* submits, uint32_t count, VkFence fence)
{
  LIDA_PROFILE_FUNCTION();
  return vkQueueSubmit(g_device->graphics_queue, count, submits, fence);
}

VkResult
lida_QueuePresent(VkPresentInfoKHR* present_info)
{
  LIDA_PROFILE_FUNCTION();
  // is it safe to use graphics queue? IDK, I think it should work on modern devices
  return vkQueuePresentKHR(g_device->graphics_queue, present_info);
}

VkResult
lida_VideoMemoryAllocate(lida_VideoMemory* memory, VkDeviceSize size,
                         VkMemoryPropertyFlags flags, uint32_t memory_type_bits,
                         const char* marker)
{
  LIDA_PROFILE_FUNCTION();
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
    LIDA_LOG_ERROR("failed to allocate memory with error %s", lida_VkResultToString(err));
    return err;
  }
  memory->offset = 0;
  memory->size = size;
  memory->type = best_type;
  if (lida_VideoMemoryGetFlags(memory) & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    err = vkMapMemory(g_device->logical_device, memory->handle, 0, VK_WHOLE_SIZE, 0, &memory->mapped);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to map memory with error %s", lida_VkResultToString(err));
      return err;
    }
  } else {
    memory->mapped = NULL;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, (uint64_t)memory->handle, marker);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to mark memory '%s' with error %s", marker, lida_VkResultToString(err));
  }
  return VK_SUCCESS;
}

void
lida_VideoMemoryFree(lida_VideoMemory* memory)
{
  LIDA_PROFILE_FUNCTION();
  if (memory->mapped) {
    vkUnmapMemory(g_device->logical_device, memory->handle);
  }
  vkFreeMemory(g_device->logical_device, memory->handle, NULL);
  memory->handle = VK_NULL_HANDLE;
}

void
lida_VideoMemoryReset(lida_VideoMemory* memory)
{
  memory->offset = 0;
}

VkMemoryPropertyFlags
lida_VideoMemoryGetFlags(const lida_VideoMemory* memory)
{
  return g_device->memory_properties.memoryTypes[memory->type].propertyFlags;
}

void
lida_MergeMemoryRequirements(const VkMemoryRequirements* requirements, uint32_t count, VkMemoryRequirements* out)
{
  memcpy(out, &requirements[0], sizeof(VkMemoryRequirements));
  for (uint32_t i = 1; i < count; i++) {
    out->size = LIDA_ALIGN_TO(out->size, requirements[i].alignment) + requirements[i].size;
    out->memoryTypeBits &= requirements[i].memoryTypeBits;
  }
}

VkShaderModule
lida_LoadShader(const char* path, const lida_ShaderReflect** reflect)
{
  LIDA_PROFILE_FUNCTION();
  ShaderInfo key_tmp = { .name = path };
  // check if we already have loaded this shader
  ShaderInfo* info = lida_HT_Search(&g_device->shader_cache, &key_tmp);
  if (info) {
    return info->module;
  }
  // load the shader
  size_t buffer_size = 0;
  uint32_t* buffer = SDL_LoadFile(path, &buffer_size);
  if (!buffer) {
    LIDA_LOG_ERROR("failed to load shader from file '%s' with error '%s'", path, SDL_GetError());
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
    LIDA_LOG_ERROR("failed to create shader module with error %s", lida_VkResultToString(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, (uint64_t)ret, path);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark shader module '%s' with error %s", path, lida_VkResultToString(err));
    }
    // Insert shader to cache if succeeded
    ShaderInfo* shader_info = lida_HT_Insert(&g_device->shader_cache,
                                             &(ShaderInfo) { .name = path, .module = ret });
    ReflectSPIRV(buffer, buffer_size / sizeof(uint32_t), &shader_info->reflect);
    if (reflect) {
      *reflect = &shader_info->reflect;
    }
  }
  SDL_free(buffer);
  return ret;
}

VkDescriptorSetLayout
lida_GetDescriptorSetLayout(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings)
{
  LIDA_PROFILE_FUNCTION();
  DS_LayoutInfo key;
  key.num_bindings = num_bindings;
  assert(num_bindings <= LIDA_ARR_SIZE(key.bindings));
  memcpy(key.bindings, bindings, num_bindings * sizeof(VkDescriptorSetLayoutBinding));
  SDL_qsort(key.bindings, num_bindings, sizeof(VkDescriptorSetLayoutBinding), &Compare_DSLB);
  DS_LayoutInfo* layout = lida_HT_Search(&g_device->ds_layout_cache, &key);
  if (layout) {
    return layout->layout;
  }
  VkDescriptorSetLayoutCreateInfo layout_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = num_bindings,
    .pBindings = bindings,
  };
  VkResult err = vkCreateDescriptorSetLayout(g_device->logical_device, &layout_info, NULL, &key.layout);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create descriptor layout with error %s", lida_VkResultToString(err));
  }
  lida_HT_Insert(&g_device->ds_layout_cache, &key);
  return key.layout;
}

VkResult
lida_AllocateDescriptorSets(const VkDescriptorSetLayoutBinding* bindings, uint32_t num_bindings,
                            VkDescriptorSet* sets, uint32_t num_sets, int dynamic,
                            const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkDescriptorSetLayout layout = lida_GetDescriptorSetLayout(bindings, num_bindings);
  VkDescriptorSetLayout* layouts = lida_TempAllocate(num_sets * sizeof(VkDescriptorSetLayout));
  for (uint32_t i = 0; i < num_sets; i++)
    layouts[i] = layout;
  VkDescriptorSetAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = (dynamic) ? g_device->dynamic_ds_pool : g_device->static_ds_pool,
      .descriptorSetCount = num_sets,
      .pSetLayouts = layouts,
  };
  VkResult err = vkAllocateDescriptorSets(g_device->logical_device, &allocate_info, sets);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to allocate descriptor sets with error %s", lida_VkResultToString(err));
  }
  lida_TempFree(layouts);
  if (err == VK_SUCCESS) {
    char buff[64];
    const char* type = (dynamic != 0 ? "resetable" : "static");
    for (uint32_t i = 0; i < num_sets; i++) {
      snprintf(buff, sizeof(buff), "%s[%u]-%s", marker, i, type);
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, (uint64_t)sets[i], buff);
      if (err != VK_SUCCESS) {
        LIDA_LOG_WARN("failed to debug marker descriptor sets '%s' with error %s", marker, lida_VkResultToString(err));
        break;
      }
    }
  }
  return err;
}

VkResult
lida_FreeDescriptorSets(const VkDescriptorSet* sets, uint32_t num_sets)
{
  return vkFreeDescriptorSets(g_device->logical_device, g_device->dynamic_ds_pool,
                              num_sets, sets);
}

void
lida_UpdateDescriptorSets(const VkWriteDescriptorSet* pDescriptorWrites, uint32_t count)
{
  LIDA_PROFILE_FUNCTION();
  vkUpdateDescriptorSets(g_device->logical_device, count, pDescriptorWrites, 0, NULL);
}

VkResult
lida_AllocateAndUpdateDescriptorSet(const lida_DescriptorBindingInfo* bindings, uint32_t count,
                                    VkDescriptorSet* set, int dynamic, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkDescriptorSetLayoutBinding* layout_bindings = lida_TempAllocate(count * sizeof(VkDescriptorSetLayoutBinding));
  VkWriteDescriptorSet* write_sets = lida_TempAllocate(count * sizeof(VkWriteDescriptorSet));
  for (uint32_t i = 0; i < count; i++) {
    layout_bindings[i] = (VkDescriptorSetLayoutBinding) {
      .binding = bindings[i].binding,
      .descriptorType = bindings[i].type,
      .descriptorCount = 1,
      .stageFlags = bindings->shader_stages,
    };
  }
  VkResult err = lida_AllocateDescriptorSets(layout_bindings, count, set, 1, dynamic, marker);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to allocate descriptor set '%s' with error %s", marker,
                   lida_VkResultToString(err));
    return err;
  }
  for (uint32_t i = 0 ; i < count; i++) {
    write_sets[i] = (VkWriteDescriptorSet) {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = *set,
      .dstBinding = bindings[i].binding,
      .descriptorCount = 1,
      .descriptorType = bindings[i].type
    };
    switch (bindings[i].type) {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      write_sets[i].pImageInfo = &bindings[i].data.image;
      break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      write_sets[i].pBufferInfo = &bindings[i].data.buffer;
      break;
    default:
      LIDA_LOG_WARN("%d descriptor type is not yet supported", bindings[i].type);
      assert(0);
    }
  }
  lida_UpdateDescriptorSets(write_sets, count);
  lida_TempFree(layout_bindings);
  return err;
}

void
lida_ResetDynamicSets()
{
  vkResetDescriptorPool(g_device->logical_device, g_device->dynamic_ds_pool, 0);
}

VkDescriptorPool
lida_GetDescriptorPool()
{
  return g_device->static_ds_pool;
}

VkSampler
lida_GetSampler(VkFilter filter, VkSamplerAddressMode mode)
{
  LIDA_PROFILE_FUNCTION();
  SamplerInfo sampler;
  sampler.filter = filter;
  sampler.mode = mode;
  // try to look if have this sampler in cache
  SamplerInfo* it = lida_HT_Search(&g_device->sampler_cache, &sampler);
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
    .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
  };
  VkResult err = vkCreateSampler(g_device->logical_device, &sampler_info, NULL, &sampler.handle);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create sampler with error %s", lida_VkResultToString(err));
    return VK_NULL_HANDLE;
  }
  // add sampler to cache if succeeded
  lida_HT_Insert(&g_device->sampler_cache, &sampler);
  return sampler.handle;
}

VkPipelineLayout
lida_CreatePipelineLayout(const lida_ShaderReflect** shader_templates, uint32_t count)
{
  LIDA_PROFILE_FUNCTION();
  const lida_ShaderReflect* shader;
  PipelineLayoutInfo layout_info = { .num_sets = 0, .num_ranges = 0 };
  if (count > 0) {
    if (count == 1) {
      shader = shader_templates[0];
    } else {
      shader = CollectShaderReflects(shader_templates, count);
    }
    layout_info.num_sets = shader->set_count;
    for (uint32_t i = 0; i < shader->set_count; i++) {
      layout_info.set_layouts[i] = lida_GetDescriptorSetLayout(lida_ShaderReflectGetBindings(shader, i),
                                                               lida_ShaderReflectGetNumBindings(shader, i));
    }
    layout_info.num_ranges = shader->range_count;
    for (uint32_t i = 0; i < shader->range_count; i++) {
      layout_info.ranges[i] = shader->ranges[i];
    }
  }
  PipelineLayoutInfo* it = lida_HT_Search(&g_device->pipeline_layout_cache, &layout_info);
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
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create pipeline layout with error %s", lida_VkResultToString(err));
  } else {
    // add pipeline layout to cache if succeeded
    lida_HT_Insert(&g_device->pipeline_layout_cache, &layout_info);
  }
  if (count > 1)
    lida_TempFree((void*)shader);
  return layout_info.handle;
}

VkResult
lida_RenderPassCreate(VkRenderPass* render_pass, const VkRenderPassCreateInfo* render_pass_info, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = vkCreateRenderPass(g_device->logical_device, render_pass_info, NULL, render_pass);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create render pass '%s' with error %s",
                   marker, lida_VkResultToString(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, (uint64_t)*render_pass, marker);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark render pass '%s' with error %s",
                    marker, lida_VkResultToString(err));
    }
  }
  return err;
}

VkResult
lida_BufferCreate(VkBuffer* buffer, VkDeviceSize size, VkBufferUsageFlags usage, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkBufferCreateInfo buffer_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VkResult err = vkCreateBuffer(g_device->logical_device, &buffer_info, NULL, buffer);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create buffer '%s' with error %s", marker, lida_VkResultToString(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, (uint64_t)*buffer, marker);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark buffer '%s' with error %s",
                    marker, lida_VkResultToString(err));
    }
  }
  return err;
}

VkResult
lida_BufferBindToMemory(lida_VideoMemory* memory, VkBuffer buffer,
                        const VkMemoryRequirements* requirements, void** mapped,
                        VkMappedMemoryRange* mappedRange)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = VideoMemoryProvide(memory, requirements);
  if (err != VK_SUCCESS) {
    return err;
  }
  err = vkBindBufferMemory(g_device->logical_device, buffer, memory->handle, memory->offset);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to bind buffer to memory with error %s", lida_VkResultToString(err));
  } else if (mapped) {
    if (memory->mapped) {
      *mapped = (char*)memory->mapped + memory->offset;
    } else {
      LIDA_LOG_WARN("memory is not mapped, can't access it's content from CPU");
    }
    if (mappedRange) {
      mappedRange->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      mappedRange->memory = memory->handle;
      mappedRange->offset = memory->offset;
      // Vulkan spec: If size is not equal to VK_WHOLE_SIZE, size must either be a multiple of
      // VkPhysicalDeviceLimits::nonCoherentAtomSize, or offset plus size must equal the size
      // of memory
      mappedRange->size = LIDA_ALIGN_TO(requirements->size, g_device->properties.limits.nonCoherentAtomSize);
    }
    memory->offset += requirements->size;
  }
  return err;
}

VkFormat
lida_FindSupportedFormat(VkFormat* options, uint32_t count, VkImageTiling tiling, VkFormatFeatureFlags flags)
{
  VkFormatProperties properties;
  for (uint32_t i = 0; i < count; i++) {
    vkGetPhysicalDeviceFormatProperties(g_device->physical_device, options[i], &properties);
    if ((tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & flags)) ||
        (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & flags))) {
      return options[i];
    }
  }
  return VK_FORMAT_MAX_ENUM;
}

VkResult
lida_ImageCreate(VkImage* image, const VkImageCreateInfo* image_info, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = vkCreateImage(g_device->logical_device, image_info, NULL, image);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create image '%s' with error %s", marker, lida_VkResultToString(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (uint64_t)*image, marker);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark image '%s' with error %s",
                    marker, lida_VkResultToString(err));
    }
  }
  return err;
}

VkResult
lida_ImageViewCreate(VkImageView* image_view, const VkImageViewCreateInfo* image_view_info, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = vkCreateImageView(g_device->logical_device, image_view_info, NULL, image_view);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create image view '%s' with error %s", marker, lida_VkResultToString(err));
  } else {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)*image_view, marker);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark image view '%s' with error %s",
                    marker, lida_VkResultToString(err));
    }
  }
  return err;
}

VkResult
lida_ImageBindToMemory(lida_VideoMemory* memory, VkImage image, const VkMemoryRequirements* requirements)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = VideoMemoryProvide(memory, requirements);
  if (err != VK_SUCCESS) {
    return err;
  }
  err = vkBindImageMemory(g_device->logical_device, image, memory->handle, memory->offset);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to bind image to memory with error %s", lida_VkResultToString(err));
  } else {
    memory->offset += requirements->size;
  }
  return err;
}

VkResult
lida_FramebufferCreate(VkFramebuffer* framebuffer, const VkFramebufferCreateInfo* framebuffer_info, const char* marker)
{
  LIDA_PROFILE_FUNCTION();
  VkResult err = vkCreateFramebuffer(g_device->logical_device, framebuffer_info, NULL, framebuffer);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create framebuffer '%s' with error %s",
                   marker, lida_VkResultToString(err));
  } else if (g_device->debug_marker_enabled) {
    err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, (uint64_t)*framebuffer, marker);
    if (err != VK_SUCCESS) {
      LIDA_LOG_WARN("failed to mark framebuffer '%s' with error %s",
                    marker, lida_VkResultToString(err));
    }
  }
  return err;
}

VkSampleCountFlagBits
lida_MaxSampleCount(VkSampleCountFlagBits max_samples)
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
  for (uint32_t i = 0; i < LIDA_ARR_SIZE(options); i++) {
    if (options[i] <= max_samples && options[i] & flags)
      ret = options[i];
  }
  return ret;
}

VkResult
lida_CreateGraphicsPipelines(VkPipeline* pipelines, uint32_t count, const lida_PipelineDesc* descs, VkPipelineLayout* layouts)
{
  LIDA_PROFILE_FUNCTION();
  VkGraphicsPipelineCreateInfo* create_infos = lida_TempAllocate(count * sizeof(VkGraphicsPipelineCreateInfo));
  VkPipelineShaderStageCreateInfo* stages = lida_TempAllocate(2 * count * sizeof(VkPipelineShaderStageCreateInfo));
  VkShaderModule* modules = lida_TempAllocate(2 * count * sizeof(VkShaderModule));
  const lida_ShaderReflect** reflects = lida_TempAllocate(2 * count * sizeof(lida_ShaderReflect*));
  VkPipelineVertexInputStateCreateInfo* vertex_input_states = lida_TempAllocate(count * sizeof(VkPipelineVertexInputStateCreateInfo));
  VkPipelineInputAssemblyStateCreateInfo* input_assembly_states = lida_TempAllocate(count * sizeof(VkPipelineInputAssemblyStateCreateInfo));
  VkPipelineViewportStateCreateInfo* viewport_states = lida_TempAllocate(count * sizeof(VkPipelineViewportStateCreateInfo));
  VkPipelineRasterizationStateCreateInfo* rasterization_states = lida_TempAllocate(count * sizeof(VkPipelineRasterizationStateCreateInfo));
  VkPipelineMultisampleStateCreateInfo* multisample_states = lida_TempAllocate(count * sizeof(VkPipelineMultisampleStateCreateInfo));
  VkPipelineDepthStencilStateCreateInfo* depth_stencil_states = lida_TempAllocate(count * sizeof(VkPipelineDepthStencilStateCreateInfo));
  VkPipelineColorBlendStateCreateInfo* color_blend_states = lida_TempAllocate(count * sizeof(VkPipelineColorBlendStateCreateInfo));
  VkPipelineDynamicStateCreateInfo* dynamic_states = lida_TempAllocate(count * sizeof(VkPipelineDynamicStateCreateInfo));
  // reserve shader cache
  lida_HT_Reserve(&g_device->shader_cache, g_device->shader_cache.size + count * 2);
  for (uint32_t i = 0; i < count; i++) {
    modules[2*i] = lida_LoadShader(descs[i].vertex_shader, &reflects[2*i]);
    stages[2*i] = (VkPipelineShaderStageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = modules[2*i],
      .pName = "main"
    };
    if (descs[i].fragment_shader) {
      // pipeline may have not a fragment shader
      modules[2*i+1] = lida_LoadShader(descs[i].fragment_shader, &reflects[2*i+1]);
      stages[2*i+1] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = modules[2*i+1],
        .pName = "main"
      };
    }
    layouts[i] = lida_CreatePipelineLayout(&reflects[2*i],
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
      // currently we always 1 viewport and 1 scissor, maybe I should add an option to use multiple scissors
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
      .depthBiasEnable = descs[i].depthBiasEnable,
      .depthBiasConstantFactor = descs[i].depthBiasConstantFactor,
      .depthBiasClamp = descs[i].depthBiasClamp,
      .depthBiasSlopeFactor = descs[i].depthBiasSlopeFactor,
      .lineWidth = descs[i].lineWidth
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
  lida_TempFree(dynamic_states);
  lida_TempFree(color_blend_states);
  lida_TempFree(depth_stencil_states);
  lida_TempFree(multisample_states);
  lida_TempFree(viewport_states);
  lida_TempFree(input_assembly_states);
  lida_TempFree(vertex_input_states);
  lida_TempFree(reflects);
  lida_TempFree(modules);
  lida_TempFree(stages);
  lida_TempFree(create_infos);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create graphics pipelines with error %s", lida_VkResultToString(err));
  } else {
    for (uint32_t i = 0; i < count; i++) {
      err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, (uint64_t)pipelines[i],
                            descs[i].marker);
      if (err != VK_SUCCESS) {
        LIDA_LOG_ERROR("failed to debug mark graphics pipeline '%s' with error %s", descs[i].marker,
                       lida_VkResultToString(err));
      }
    }
  }
  return err;
}

const char*
lida_VkResultToString(VkResult err)
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
lida_VkFormatToString(VkFormat format)
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

VkShaderStageFlags
lida_ShaderReflectGetStage(const lida_ShaderReflect* shader)
{
  return shader->stages;
}

uint32_t
lida_ShaderReflectGetNumSets(const lida_ShaderReflect* shader)
{
  return shader->set_count;
}

uint32_t
lida_ShaderReflectGetNumBindings(const lida_ShaderReflect* shader, uint32_t set)
{
  if (set >= shader->set_count) return UINT32_MAX;
  return shader->sets[set].binding_count;
}

const VkDescriptorSetLayoutBinding*
lida_ShaderReflectGetBindings(const lida_ShaderReflect* shader, uint32_t set)
{
  if (set >= shader->set_count) return NULL;
  return shader->sets[set].bindings;
}

uint32_t
lida_ShaderReflectGetNumRanges(const lida_ShaderReflect* shader)
{
  return shader->range_count;
}

const VkPushConstantRange*
lida_ShaderReflectGetRanges(const lida_ShaderReflect* shader)
{
  return shader->ranges;
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
  (void)user_data;
  if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    LIDA_LOG_ERROR("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 0;
  } else if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ||
             (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) {
    LIDA_LOG_WARN("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
    LIDA_LOG_DEBUG("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
  else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
    LIDA_LOG_DEBUG("[Vulkan:%d: %s]: %s\n", code, layer_prefix, msg);
    return 1;
  }
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
        LIDA_LOG_ERROR("Failed to create debug report callback");
      }
    } else {
      g_device->debug_report_callback = VK_NULL_HANDLE;
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
    LIDA_LOG_WARN("lida_DeviceDesc->gpu_id is out of bounds, picking GPU0");
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
    LIDA_LOG_ERROR("failed to enumerate device extensions with error %d", err);
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
    g_device->num_enabled_device_extensions = desc->num_device_extensions + (desc->enable_debug_layers != 0);
    g_device->enabled_device_extensions =
      lida_TempAllocate(desc->num_device_extensions * sizeof(const char*));
    for (uint32_t i = 0; i < desc->num_device_extensions; i++) {
      g_device->enabled_device_extensions[i] = desc->device_extensions[i];
    }
    // add DEBUG_MARKER extension if debug layers are enabled
    if (desc->enable_debug_layers) {
      g_device->enabled_device_extensions[desc->num_device_extensions] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
      g_device->debug_marker_enabled = 1;
    } else {
      g_device->debug_marker_enabled = 0;
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

VkResult
CreateDescriptorPools()
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
    .poolSizeCount = LIDA_ARR_SIZE(sizes),
    .pPoolSizes = sizes,
  };
  VkResult err = vkCreateDescriptorPool(g_device->logical_device, &pool_info, NULL,
                                        &g_device->static_ds_pool);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create pool for static resources with error %d", err);
    return err;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)g_device->static_ds_pool, "static-descriptor-pool");
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to mark static descriptor pool with error %s", lida_VkResultToString(err));
  }
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  err = vkCreateDescriptorPool(g_device->logical_device, &pool_info, NULL,
                               &g_device->dynamic_ds_pool);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create pool for dynamic resources with error %d", err);
    return err;
  }
  err = DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (uint64_t)g_device->static_ds_pool, "dynamic-descriptor-pool");
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to mark dynamic descriptor pool with error %s", lida_VkResultToString(err));
  }
  return err;
}

VkResult
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

VkResult VideoMemoryProvide(lida_VideoMemory* memory, const VkMemoryRequirements* requirements)
{
  if (((1 << memory->type) & requirements->memoryTypeBits) == 0) {
    LIDA_LOG_ERROR("buffer cannot be bound to memory. bits %u are needed, but bit %u is available",
                   requirements->memoryTypeBits, memory->type);
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }
  memory->offset = LIDA_ALIGN_TO(memory->offset, requirements->alignment);
  if (memory->offset > memory->size) {
    LIDA_LOG_ERROR("out of video memory");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }
  return VK_SUCCESS;
}

int
Compare_DSLB(const void* l, const void* r)
{
  return memcmp(l, r, sizeof(VkDescriptorSetLayoutBinding));
}

void
MergeShaderReflects(lida_ShaderReflect* lhs, const lida_ShaderReflect* rhs)
{
  lhs->stages |= rhs->stages;
  if (rhs->set_count > lhs->set_count) lhs->set_count = rhs->set_count;
  for (uint32_t i = 0; i < rhs->set_count; i++) {
    const BindingSetDesc* set = &rhs->sets[i];
    uint32_t* pCount = &lhs->sets[i].binding_count;
    uint32_t old_count = *pCount;
    for (uint32_t j = 0; j < set->binding_count; j++) {
      int found = 0;
      for (uint32_t k = 0; old_count; k++) {
        VkDescriptorSetLayoutBinding* binding = &lhs->sets[j].bindings[k];
        if (binding->binding == set->bindings[j].binding) {
          if (binding->descriptorType != set->bindings[j].descriptorType ||
              binding->descriptorCount != set->bindings[j].descriptorCount) {
            LIDA_LOG_WARN("shader merge error: different uniforms have the same binding number");
          }
          binding->stageFlags |= set->bindings[j].stageFlags;
          found = 1;
          break;
        }
      }
      if (!found) {
        // add binding
        assert(*pCount < SHADER_REFLECT_MAX_BINDINGS_PER_SET &&
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
      assert(lhs->range_count < SHADER_REFLECT_MAX_RANGES &&
             "shader reflect merge: push constant number overflow");
      memcpy(&lhs->ranges[lhs->range_count], rrange, sizeof(VkPushConstantRange));
      lhs->range_count++;
    }
  }
}

lida_ShaderReflect*
CollectShaderReflects(const lida_ShaderReflect** shaders, uint32_t count)
{
  lida_ShaderReflect* shader = lida_TempAllocate(sizeof(lida_ShaderReflect));
  memcpy(shader, shaders[0], sizeof(lida_ShaderReflect));
  for (uint32_t i = 1; i < count; i++) {
    MergeShaderReflects(shader, shaders[i]);
  }
  return shader;
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

static uint32_t
SPIRV_ComputeTypeSize(SPIRV_ID* ids, uint32_t id, uint32_t current_size/*for alignment*/)
{
  // about alignment: https://stackoverflow.com/a/45641579
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
      alignment = LIDA_ALIGN_TO(arr_size, 16 * elem_alignment);
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
      alignment = LIDA_ALIGN_TO(ids[vec_id].data.val_vec.numComponents, 2) * elem_size;
    }
    break;
  case SpvOpTypeVector:
    // A two-component vector, with components of size N, has a base alignment of 2 N.
    // A three- or four-component vector, with components of size N, has a base alignment of 4 N.
    {
      uint32_t component_size = SPIRV_ComputeTypeSize(ids, ids[id].data.val_vec.componentTypeId, 0);
      offset = ids[id].data.val_vec.numComponents * component_size;
      uint32_t num_components = LIDA_ALIGN_TO(ids[id].data.val_vec.numComponents, 2);
      alignment = num_components * component_size;
    }
    break;
  default:
    assert(0 && "unrecognized type");
  }
  return LIDA_ALIGN_TO(current_size, alignment) - current_size + offset;
}

int
ReflectSPIRV(const uint32_t* code, uint32_t size, lida_ShaderReflect* shader)
{
  // based on https://github.com/zeux/niagara/blob/98f5d5ae2b48e15e145e3ad13ae7f4f9f1e0e297/src/shaders.cpp#L45
  // https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#_physical_layout_of_a_spir_v_module_and_instruction
  // this tool also helped me a lot: https://www.khronos.org/spir/visualizer/
  if (code[0] != SpvMagicNumber) {
    LIDA_LOG_WARN("code is not valid SPIR-V");
    return -1;
  }
  uint32_t id_bound = code[3];
  SPIRV_ID* ids = lida_TempAllocate(sizeof(SPIRV_ID) * id_bound);
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
  memset(shader->sets, 0, sizeof(BindingSetDesc) * SHADER_REFLECT_MAX_SETS);

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
      BindingSetDesc* set = &shader->sets[id->data.binding.set];
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
  lida_TempFree(ids);
  return 0;
}
