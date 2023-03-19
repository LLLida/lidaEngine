/* -*- mode: c++ -*-
   lida_platform_android.cc

   Platform functions implementation using SDL2 library for android.
   TODO: don't use SDL2, use native-app-glue or write a bit of Java.
 */
#include <SDL.h>
#include <SDL_vulkan.h>

#include <android/log.h>

#include <unistd.h>
#include <unwind.h>
#include <dlfcn.h>

#include "lida_platform.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

#include "lib/stb_sprintf.h"

static int running = 1;

static struct {
  SDL_Window* handle;
  uint32_t w;
  uint32_t h;
  int resizable;
} window;

static const int signals_to_catch[] = {
  SIGABRT,
  SIGBUS,
  SIGFPE,
  SIGSEGV,
  SIGILL,
  SIGSTKFLT,
  SIGTRAP,
};

struct Crash_Context {
  struct sigaction old_handlers[NSIG];
};
static Crash_Context crash_context;

static void android_logger(const Log_Event* ev);
static void HandleCrash(int, siginfo*, void*);

extern "C" int
SDL_main(int argc, char** argv)
{
  EngineAddLogger(android_logger, 0, NULL);

  struct sigaction sigactionstruct;
  memset(&sigactionstruct, 0, sizeof(sigactionstruct));
  sigactionstruct.sa_flags = SA_SIGINFO;
  sigactionstruct.sa_sigaction = &HandleCrash;
  // Register new handlers for all signals
  for (int index = 0; index < sizeof(signals_to_catch)/sizeof(int); ++index) {
    const int signo = signals_to_catch[index];
    if (sigaction(signo, &sigactionstruct, &crash_context.old_handlers[signo])) {
      LOG_WARN("failed to do sigaction");
    }
  }

  window.w = 1080;
  window.h = 720;

  Engine_Startup_Info engine_info = {};
  engine_info.enable_debug_layers = 0;  // TODO: support validation layers
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


/// Logging
// https://stackoverflow.com/questions/8115192/android-ndk-getting-the-backtrace

struct BacktraceState {
  void** current;
  void** end;
};

static _Unwind_Reason_Code
unwindCallback(struct _Unwind_Context* context, void* arg)
{
  BacktraceState* state = (BacktraceState*)arg;
  uintptr_t pc = _Unwind_GetIP(context);
  if (pc) {
    if (state->current == state->end) {
      return _URC_END_OF_STACK;
    } else {
      *state->current++ = (void*)pc;
    }
  }
  return _URC_NO_REASON;
}

static size_t
captureBacktrace(void** buffer, size_t max)
{
    BacktraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}

static void
dumpBacktrace(void** buffer, size_t count)
{
  for (size_t idx = 0; idx < count; ++idx) {
    const void* addr = buffer[idx];
    const char* symbol = "";

    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname) {
      symbol = info.dli_sname;
    }

    __android_log_print(ANDROID_LOG_ERROR, "lida", "  #%lu: %p %s", idx, addr, symbol);
  }
}

void
android_logger(const Log_Event* ev)
{
  int log_level;
  switch (ev->level)
    {
    case 0: log_level = ANDROID_LOG_VERBOSE; break;
    case 1: log_level = ANDROID_LOG_DEBUG; break;
    case 2: log_level = ANDROID_LOG_INFO; break;
    case 3: log_level = ANDROID_LOG_WARN; break;
    case 4: log_level = ANDROID_LOG_ERROR; break;
    case 5: log_level = ANDROID_LOG_FATAL; break;
    }
  __android_log_print(log_level, "lida", "[%s:%d] %s", ev->file, ev->line, ev->str);
  // dump backtrace in bad situations, this is the way we debug by the moment
  if (log_level == ANDROID_LOG_WARN ||
      log_level == ANDROID_LOG_ERROR ||
      log_level == ANDROID_LOG_FATAL) {
    const size_t max = 64;
    void* buffer[max];
    dumpBacktrace(buffer, captureBacktrace(buffer, max));
  }
}

void
HandleCrash(int signo, siginfo* siginfo, void* ctx)
{
  // Restoring an old handler to make built-in Android crash mechanism work.
  sigaction(signo, &crash_context.old_handlers[signo], nullptr);
  // Log crash message
  // __android_log_print(ANDROID_LOG_ERROR, "lida", "%s", createCrashMessage(signo, siginfo));

  __android_log_print(ANDROID_LOG_ERROR, "lida", "lida engine crashed :( printin backtrace...");
  const size_t max = 64;
  void* buffer[max];
  dumpBacktrace(buffer, captureBacktrace(buffer, max));

  // In some cases we need to re-send a signal to run standard bionic handler.
  if (siginfo->si_code <= 0 || signo == SIGABRT) {
#define __NR_tgkill 270
    if (syscall(__NR_tgkill, getpid(), gettid(), signo) < 0) {
      _exit(1);
    }
  }
}



/// Platform functions

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
PlatformFreeLoadedFile(void* data)
{
  SDL_free(data);
}

void*
PlatformOpenFileForWrite(const char* path)
{
  SDL_RWops* file = SDL_RWFromFile(path, "wb");
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
