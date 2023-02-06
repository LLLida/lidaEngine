#include "init.h"
#include "base.h"
#include "device.h"
#include "memory.h"
#include "render.h"
#include "window.h"

#include <argp.h>
#include <SDL.h>

typedef struct {
  lida_DeviceDesc device;
  lida_WindowDesc window;
  VkSampleCountFlagBits msaa_samples;
} Arguments;

static char doc[] = "lida engine sample application";
static char args_doc[] = "";

static struct argp_option arg_options[] = {
  { "debug-layers", 'd', "BOOLEAN", 0, "Enable vulkan validation layers", 0 },
  { "msaa", 's', "INTEGER", 0, "Number of MSAA samples", 0 },
  { "width", 'w', "INTEGER", 0, "Window width in pixels", 0 },
  { "height", 'h', "INTEGER", 0, "Window height in pixels", 0 },
  { "resizable", 'r', "BOOLEAN", 0, "Whether window is resizable", 0 },
  { "gpu", 'g', "INDEX", 0, "Index of GPU to use", 0 },
  { 0 },
};

static error_t parse_opt(int key, char* arg, struct argp_state* state);



int
lida_EngineInit(int argc, char** argv)
{
  SDL_Init(SDL_INIT_VIDEO);
  lida_TempAllocatorCreate(128 * 1024);

  Arguments arguments;
  // default options for device
  arguments.device = (lida_DeviceDesc) {
    .enable_debug_layers = 1,
    .gpu_id = 0,
    .app_name = "tst",
    .app_version = VK_MAKE_VERSION(0, 0, 0),
  };
  // default options for window
  arguments.window = (lida_WindowDesc) {
    .name = "hello world",
    .x = SDL_WINDOWPOS_CENTERED,
    .y = SDL_WINDOWPOS_CENTERED,
    .w = 1080,
    .h = 720,
    .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR,
#ifdef __linux__
    // my tiling window manager immediately resizes the window at startup.
    // I don't like that behavior. We have an option whether the window is
    // resizable for debug purposes.
    .resizable = 0,
#else
    .resizable = 1,
#endif
  };
  arguments.msaa_samples = VK_SAMPLE_COUNT_4_BIT;

  struct argp argp = {
    .options = arg_options,
    .parser = parse_opt,
    .args_doc = args_doc,
    .doc = doc,
  };
  // parse options
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  lida_ProfilerBeginSession("results.json");
  lida_InitPlatformSpecificLoggers();

  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  arguments.device.device_extensions = device_extensions;
  arguments.device.num_device_extensions = LIDA_ARR_SIZE(device_extensions);

  lida_DeviceCreate(&arguments.device);

  lida_WindowCreate(&arguments.window);

  lida_ForwardPassCreate(lida_WindowGetExtent().width, lida_WindowGetExtent().height,
                         arguments.msaa_samples);
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



error_t
parse_opt(int key, char* arg, struct argp_state* state)
{
  Arguments* arguments = (Arguments*)state->input;
  switch (key)
    {
    case 'd':
      arguments->device.enable_debug_layers = atoi(arg);
      break;

    case 's':
      {
        int options[] = { 1, 2, 4, 8, 16, 32 };
        VkSampleCountFlagBits values[] = { VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT };
        int s = atoi(arg);
        for (size_t i = 0; i < LIDA_ARR_SIZE(options); i++) {
          if (s == options[i]) {
            arguments->msaa_samples = values[i];
            return 0;
          }
        }
        LIDA_LOG_FATAL("unknown sample count %d", s);
        argp_usage(state);
      }
      break;

    case 'w':
      arguments->window.w = atoi(arg);
      break;
    case 'h':
      arguments->window.h = atoi(arg);
      break;

    case 'r':
      arguments->window.resizable = atoi(arg);
      break;

    case 'g':
      arguments->device.gpu_id = atoi(arg);
      break;

    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
      // nothing for now
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}
