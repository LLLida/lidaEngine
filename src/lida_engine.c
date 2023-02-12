/* -*- mode: c -*-

  lida engine - portable and small 3D Vulkan engine.

  ===============================
  Author: Adil Mokhammad
  Email: 0adilmohammad0@gmail.com
 */

#include "lib/volk.h"

#include "stdalign.h"
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

  // TEST: TODO: fix horrible bug
  // VkShaderModule shader = LoadShader("rect.frag.spv", NULL);
  // Assert(shader);

}

void
EngineFree()
{
  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  DestroyWindow(0);
  DestroyDevice(0);
  PlatformFreeMemory(g_persistent_memory.ptr);
}

void
EngineUpdateAndRender()
{
  VkCommandBuffer cmd = BeginCommands();

  BeginRenderingToWindow();

  vkCmdEndRenderPass(cmd);

  vkEndCommandBuffer(cmd);

  PresentToScreen();
}

void
EngineKeyPressed(PlatformKeyCode key)
{
  switch (key)
    {

    case PlatformKey_ESCAPE:
      PlatformWantToQuit();
      break;

    case PlatformKey_1:
      LOG_INFO("FPS=%f", g_window->frames_per_second);
      break;

    default:
      break;

    }
}

void
EngineKeyReleased(PlatformKeyCode key)
{
  switch (key)
    {

    default:
      break;

    }
}

void
EngineMouseMotion(int x, int y, int xrel, int yrel)
{
  (void)x;
  (void)y;
  (void)xrel;
  (void)yrel;
}
