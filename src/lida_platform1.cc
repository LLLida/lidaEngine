/* -*- mode: c++ -*-

   Platform functions implementation using SDL2 library.

 */
#include <SDL.h>
#include <SDL_vulkan.h>

#include "lida_platform.h"


/// implementation of platform abstraction layer

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

}

uint64_t
PlatformGetPerformanceCounter()
{

}

uint64_t
PlatformGetPerformanceFrequency()
{

}

size_t
PlatformThreadId()
{

}

void
PlatformHideCursor()
{

}

void
PlatformShowCursor()
{

}

void*
PlatformLoadEntireFile(const char* path, size_t* buff_size)
{

}

void
PlatformFreeFile(void* data)
{
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


/// Entrypoint

static void sdl_logger(const Log_Event* ev);

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

  EngineInit(&engine_info);

  SDL_Delay(1000);

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
