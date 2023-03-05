/* -*- mode: c++ -*-
   lida_platform1.cc
   Platform functions implementation using SDL2 library.

 */
#include <SDL.h>
#include <SDL_vulkan.h>

#include "lida_platform.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

#include "lib/stb_sprintf.h"

#ifdef __linux__
#include <unistd.h>
// for command line argument parsing
#include <argp.h>
// for hot resource reloading
#include <sys/inotify.h>
#endif

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

// argument parsing stuff
static char doc[] = "lida engine";
static char args_doc[] = "";

static argp_option arg_options[] = {
  { "data", 'd', "DIRECTORY", 0, "Data directory where all assets are stored. This must come without '/' at the end.", 0 },
  { "debug-layers", 'l', "BOOLEAN", 0, "Enable vulkan validation layers", 0 },
  { "msaa", 's', "INTEGER", 0, "Number of MSAA samples", 0 },
  { "width", 'w', "INTEGER", 0, "Window width in pixels", 0 },
  { "height", 'h', "INTEGER", 0, "Window height in pixels", 0 },
  { "vsync", 'v', "BOOLEAN", 0, "Whether vsync is enabled", 0 },
  { "resizable", 'r', "BOOLEAN", 0, "Whether window is resizable", 0 },
  { "gpu", 'g', "INDEX", 0, "Index of GPU to use", 0 },
  { },
};
static error_t parse_opt(int key, char* arg, struct argp_state* state);

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

  argp argp = {};
  argp.options = arg_options;
  argp.parser = parse_opt;
  argp.args_doc = args_doc;
  argp.doc = doc;
  argp_parse(&argp, argc, argv, 0, 0, &engine_info);

  data_dir.fd = inotify_init();
  if (data_dir.fd < 0) {
    LOG_ERROR("inotify_init() returned %d", data_dir.fd);
  }
  data_dir.wd = inotify_add_watch(data_dir.fd, "../data", IN_MODIFY|IN_CREATE);
  if (data_dir.wd < 0) {
    LOG_ERROR("inotify_add_watch() returned %d", data_dir.fd);
  }

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

  // shutdown inotify
  close(data_dir.fd);

  SDL_DestroyWindow(window.handle);

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
      stbsp_sprintf(data_dir.path, "%s", arg);
      break;

    case 'l':
      info->enable_debug_layers = atoi(arg);
      break;

    case 's':
      {
        int options[] = { 1, 2, 4, 8, 16, 32 };
        int s = atoi(arg);
        for (size_t i = 0; i < sizeof(options) / sizeof(int); i++) {
          if (s == options[i]) {
            info->msaa_samples = options[i];
            return 0;
          }
        }
        LOG_FATAL("unknown sample count %d", s);
        argp_usage(state);
      }
      break;

    case 'w':
      window.w = atoi(arg);
      break;
    case 'h':
      window.h = atoi(arg);
      break;

    case 'v':
      info->window_vsync = atoi(arg);
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
  if (data_dir.fd < 0 ||
      data_dir.wd < 0) {
    return 0;
  }
  size_t counter = 0;
  timeval time;
  fd_set rfds;
  int ret;
  time.tv_sec = 0;
  time.tv_usec = 10;

  FD_ZERO (&rfds);
  FD_SET (data_dir.fd, &rfds);


  ret = select(data_dir.fd + 1, &rfds, NULL, NULL, &time);
  if (ret < 0) {
    LOG_WARN("error in select()");
    return 0;
  } else if (!ret) {
    return 0;
  } else if (FD_ISSET (data_dir.fd, &rfds)) {

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

    // process events
    char buffer[BUF_LEN];
    int length = read(data_dir.fd, buffer, BUF_LEN), i = 0;
    if (length > 0) {
      while (i < length && counter < buff_size) {
        // https://developer.ibm.com/tutorials/l-ubuntu-inotify/
        inotify_event *event = (inotify_event*) &buffer[ i ];
        if (event->len) {
          if ( event->mask & (IN_CREATE|IN_MODIFY) ) {
            filenames[counter++] = event->name;
          }
        }
        i += EVENT_SIZE + event->len;
      }
    }

  }
  return counter;
}
