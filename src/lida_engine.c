/* -*- mode: c -*-

  3D Vulkan engine.

  ===============================
  Author: Adil Mokhammad
  Email: 0adilmohammad0@gmail.com
 */

#include "lib/volk.h"

#include "string.h"

#include "lida_platform.h"

#define LIDA_ENGINE_VERSION 202302
#define INTERNAL static
#define GLOBAL static

#include "lida_base.c"
#include "lida_device.c"
#include "lida_window.c"


/// Engine general functions

void
EngineInit(const Engine_Startup_Info* info)
{
  g_persistent_memory.size = 16 * 1024 * 1024;
  g_persistent_memory.ptr = PlatformAllocateMemory(g_persistent_memory.size);
  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  CreateDevice(info->enable_debug_layers,
               info->gpu_id,
               info->app_name, info->app_version,
               device_extensions, ARR_SIZE(device_extensions));
  CreateWindow(info->window_vsync);
}

void
EngineFree()
{
  DestroyWindow(0);
  DestroyDevice(0);
  PlatformFreeMemory(g_persistent_memory.ptr);
}
