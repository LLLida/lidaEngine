/* -*- mode: c -*-

   Platform abstraction layer.

 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// list of files we need to implement platform for:
// DONE memory.c NOTE: we didn't write realloc
// DONE main.cpp TODO: events
// DONE ui.cc
// DONE voxel.cc
// DONE window.c NOTE: need to write imgui initialisation
// DONE init.c NOTE: initialisation
// DONE device.c
// DONE base.c NOTE: file operations


/// Common hooks provided by the engine

typedef struct {

  const char* str;
  const char* file;
  int line;
  int strlen;
  int level;
  void* udata;

} Log_Event;

typedef void(*Log_Function)(const Log_Event* event);

typedef struct {

  int enable_debug_layers;
  uint32_t gpu_id;
  const char* app_name;
  uint32_t app_version;
  int window_vsync;

} Engine_Startup_Info;

void EngineLog(int level, const char* file, int line, const char* fmt, ...);
void EngineAddLogger(Log_Function logger, int level, void* udata);
void EngineInit(const Engine_Startup_Info* info);
void EngineFree();


/// Functions that platform need to implement

// allocate memory chunk of mem->size bytes
void* PlatformAllocateMemory(size_t bytes);
// free a memory chunk
void PlatformFreeMemory(void* bytes);

uint32_t PlatformGetTicks();
uint64_t PlatformGetPerformanceCounter();
uint64_t PlatformGetPerformanceFrequency();
size_t PlatformThreadId();

void PlatformHideCursor();
void PlatformShowCursor();

void* PlatformLoadEntireFile(const char* path, size_t* buff_size);
void PlatformFreeFile(void* data);

int PlatformCreateWindow();
void PlatformDestroyWindow();

// create VkSurfaceKHR needed for swapchain creation. It is guaranteed
// that this function will be called once at startup after
// platform_create_window().
VkSurfaceKHR PlatformCreateVkSurface(VkInstance instance);

#ifdef __cplusplus
}
#endif
