/* -*- mode: c++ -*-
   lida_platform1.cc
   Platform functions implementation using SDL2 library.

 */
#include <SDL.h>
#include <SDL_vulkan.h>

#include "lida_platform.h"

#include <argp.h>

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"


/// implementation of platform abstraction layer

int running = 1;

static struct {
  SDL_Window* handle;
  uint32_t w;
  uint32_t h;
  int resizable;
} window;


void*
PlatformAllocateMemory(size_t bytes)
{
  return SDL_malloc(bytes);
}

void
PlatformFreeMemory(void* ptr)
{
  SDL_free(ptr);
}

uint32_t
PlatformGetTicks()
{
  return SDL_GetTicks();
}

uint64_t
PlatformGetPerformanceCounter()
{
  return SDL_GetPerformanceCounter();
}

uint64_t
PlatformGetPerformanceFrequency()
{
  return SDL_GetPerformanceFrequency();
}

size_t
PlatformThreadId()
{
  return SDL_ThreadID();
}

void
PlatformHideCursor()
{
  SDL_SetRelativeMouseMode(SDL_TRUE);
}

void
PlatformShowCursor()
{
  SDL_SetRelativeMouseMode(SDL_FALSE);
}

void*
PlatformLoadEntireFile(const char* path, size_t* buff_size)
{
  return SDL_LoadFile(path, buff_size);
}

void
PlatformFreeFile(void* data)
{
  SDL_free(data);
}

int
PlatformCreateWindow()
{
  Uint32 flags = SDL_WINDOW_VULKAN;
  if (window.resizable) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  window.handle = SDL_CreateWindow("window",
                                   SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                                   window.w, window.h,
                                   flags);
  return window.handle == NULL;
}

void
PlatformDestroyWindow()
{
  SDL_DestroyWindow(window.handle);
}

VkSurfaceKHR
PlatformCreateVkSurface(VkInstance instance)
{
  VkSurfaceKHR ret;
  SDL_Vulkan_CreateSurface(window.handle, instance, &ret);
  return ret;
}

void
PlatformWantToQuit()
{
  running = 0;
}

const char*
PlatformGetError()
{
  return SDL_GetError();
}


/// Entrypoint

static void sdl_logger(const Log_Event* ev);

// argument parsing stuff
static char doc[] = "lida engine";
static char args_doc[] = "";

static argp_option arg_options[] = {
  { "debug-layers", 'd', "BOOLEAN", 0, "Enable vulkan validation layers", 0 },
  { "msaa", 's', "INTEGER", 0, "Number of MSAA samples", 0 },
  { "width", 'w', "INTEGER", 0, "Window width in pixels", 0 },
  { "height", 'h', "INTEGER", 0, "Window height in pixels", 0 },
  { "resizable", 'r', "BOOLEAN", 0, "Whether window is resizable", 0 },
  { "gpu", 'g', "INDEX", 0, "Index of GPU to use", 0 },
  { },
};
static error_t parse_opt(int key, char* arg, struct argp_state* state);

extern "C" int
main(int argc, char** argv)
{
  EngineAddLogger(sdl_logger, 0, NULL);

  window.w = 1080;
  window.h = 720;

  Engine_Startup_Info engine_info = {};
  engine_info.enable_debug_layers = 1;
  engine_info.gpu_id = 0;
  engine_info.app_name = "test";
  engine_info.window_vsync = 0;

  argp argp = {};
  argp.options = arg_options;
  argp.parser = parse_opt;
  argp.args_doc = args_doc;
  argp.doc = doc;
  argp_parse(&argp, argc, argv, 0, 0, &engine_info);

  EngineInit(&engine_info);

  SDL_Event event;
  while (running) {

    while (SDL_PollEvent(&event)) {
      switch (event.type)
        {

        case SDL_QUIT: running = 0;
          break;

        case SDL_KEYDOWN:
          EngineKeyPressed((PlatformKeyCode)event.key.keysym.sym);
          break;

        case SDL_KEYUP:
          EngineKeyReleased((PlatformKeyCode)event.key.keysym.sym);
          break;

        case SDL_MOUSEMOTION:
          EngineMouseMotion(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
          break;

        }
    }

    EngineUpdateAndRender();

  }

  EngineFree();

  return 0;
}

void
sdl_logger(const Log_Event* ev)
{
#ifdef __linux__
  const char* const levels[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
  };
  const char* const colors[] = {
    "\x1b[94m",
    "\x1b[36m",
    "\x1b[32m",
    "\x1b[33m",
    "\x1b[31m",
    "\x1b[35m"
  };
  const char* const white_color = "\x1b[0m";
  const char* const gray_color = "\x1b[90m";
  printf("[%s%s%s] %s%s:%d%s %s\n", colors[ev->level], levels[ev->level], white_color,
         gray_color, ev->file, ev->line,
         white_color, ev->str);
#else
  #error Not implemented
#endif
}

static error_t
parse_opt(int key, char* arg, struct argp_state* state)
{
  auto info = (Engine_Startup_Info*)state->input;
  switch (key)
    {
    case 'd':
      info->enable_debug_layers = atoi(arg);
      break;

    case 's':
      {
        // int options[] = { 1, 2, 4, 8, 16, 32 };
        // VkSampleCountFlagBits values[] = { VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT };
        // int s = atoi(arg);
        // for (size_t i = 0; i < LIDA_ARR_SIZE(options); i++) {
        //   if (s == options[i]) {
        //     arguments->msaa_samples = values[i];
        //     return 0;
        //   }
        // }
        // LIDA_LOG_FATAL("unknown sample count %d", s);
        // argp_usage(state);
      }
      break;

    case 'w':
      window.w = atoi(arg);
      break;
    case 'h':
      window.h = atoi(arg);
      break;

    case 'r':
      window.resizable = atoi(arg);
      break;

    case 'g':
      info->gpu_id = atoi(arg);
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
