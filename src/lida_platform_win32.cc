/* -*- mode: c++ -*-
   lida_platform_win32.cc
   Platform functions implementation using SDL2 library for MS Windows.

 */
#include <SDL.h>
#include <SDL_vulkan.h>

#include "lida_platform.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

#include "lib/stb_sprintf.h"

/// Global variables

static int running = 1;

static struct {
  SDL_Window* handle;
  uint32_t w;
  uint32_t h;
  int resizable;
} window;

static struct {
  char path[64];
  // see https://www.linuxjournal.com/article/8478 for more detail
  int fd;
  int wd;
} data_dir;


/// Entrypoint

static void sdl_logger(const Log_Event* ev);

extern "C" int
main(int argc, char** argv)
{
  stbsp_sprintf(data_dir.path, "../data");

  EngineAddLogger(sdl_logger, 0, NULL);

  window.w = 1080;
  window.h = 720;

  Engine_Startup_Info engine_info = {};
  engine_info.enable_debug_layers = 1;
  engine_info.gpu_id = 0;
  engine_info.app_name = "test";
  engine_info.window_vsync = 0;
  engine_info.msaa_samples = 4;

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

        case SDL_TEXTINPUT:
          EngineTextInput(event.text.text);
          break;
        }
    }


    EngineUpdateAndRender();

  }

  EngineFree();

  SDL_DestroyWindow(window.handle);

  return 0;
}

void
sdl_logger(const Log_Event* ev)
{
  const char* const levels[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
  };
  printf("[%s] %s:%d %s\n", levels[ev->level], ev->file, ev->line, ev->str);
}


/// implementation of platform abstraction layer

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
  char real_path[128];
  stbsp_snprintf(real_path, sizeof(real_path), "%s/%s", data_dir.path, path);
  return SDL_LoadFile(real_path, buff_size);
}

void
PlatformFreeLoadedFile(void* data)
{
  SDL_free(data);
}

void*
PlatformOpenFileForWrite(const char* path)
{
  char real_path[128];
  stbsp_snprintf(real_path, sizeof(real_path), "%s/%s", data_dir.path, path);
  SDL_RWops* file = SDL_RWFromFile(real_path, "wb");
  return file;
}

void
PlatformWriteToFile(void* file, const void* bytes, size_t sz)
{
  SDL_RWwrite((SDL_RWops*)file, bytes, sz, 1);
}

void
PlatformCloseFileForWrite(void* file)
{
  SDL_RWclose((SDL_RWops*)file);
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

size_t
PlatformDataDirectoryModified(const char** filenames, size_t buff_size)
{
  return 0;
}
