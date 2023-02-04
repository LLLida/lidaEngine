#include "init.h"
#include "base.h"
#include "device.h"
#include "memory.h"
#include "render.h"
#include "window.h"

#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <SDL.h>

int
lida_EngineInit(int argc, char** argv)
{
  SDL_Init(SDL_INIT_VIDEO);
  lida_TempAllocatorCreate(128 * 1024);

  int enable_debug_layers = 1;
  VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_4_BIT;
  int window_w = 1080;
  int window_h = 720;
#ifdef __linux__
  // my tiling window manager immediately resizes the window at startup,
  // I don't like that behavior. We have an option whether the window is
  // resizable for debug purposes.
  int resizable = 0;
#else
  int resizable = 1;
#endif
  int opt;
  // TODO: use argp and introduce resizable option
  while ((opt = getopt(argc, argv, "d:s:w:h:")) != -1) {
    switch (opt) {
    case 'd':
      enable_debug_layers = atoi(optarg);
      LIDA_LOG_DEBUG("debug=%d", enable_debug_layers);
      fflush(stdout);
      break;
    case 's':
      switch (optarg[0]) {
      case '1':
        msaa_samples = VK_SAMPLE_COUNT_1_BIT;
        break;
      case '2':
        msaa_samples = VK_SAMPLE_COUNT_2_BIT;
        break;
      case '4':
        msaa_samples = VK_SAMPLE_COUNT_4_BIT;
        break;
      case '8':
        msaa_samples = VK_SAMPLE_COUNT_8_BIT;
        break;
      default:
        LIDA_LOG_WARN("invalid option for MSAA samples: %s", optarg);
        break;
      }
      break;
    case 'w':
      window_w = atoi(optarg);
      break;
    case 'h':
      window_h = atoi(optarg);
      break;
    }
  }
  lida_ProfilerBeginSession("results.json");
  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  lida_InitPlatformSpecificLoggers();
  LIDA_DEVICE_CREATE(.enable_debug_layers = enable_debug_layers,
                     .gpu_id = 0,
                     .app_name = "tst",
                     .app_version = VK_MAKE_VERSION(0, 0, 0),
                     .device_extensions = device_extensions,
                     .num_device_extensions = LIDA_ARR_SIZE(device_extensions));

  LIDA_WINDOW_CREATE(.name = "hello world",
                     .x = SDL_WINDOWPOS_CENTERED,
                     .y = SDL_WINDOWPOS_CENTERED,
                     .w = window_w,
                     .h = window_h,
                     .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR,
                     .resizable = resizable);

  lida_ForwardPassCreate(lida_WindowGetExtent().width, lida_WindowGetExtent().height, msaa_samples);
  lida_ShadowPassCreate(1024, 1024);
  return 0;
}

void
lida_EngineFree()
{
  lida_ShadowPassDestroy();
  lida_ForwardPassDestroy();

  lida_WindowDestroy();
  lida_DeviceDestroy(0);

  lida_ProfilerEndSession();

  lida_TempAllocatorDestroy();
}
