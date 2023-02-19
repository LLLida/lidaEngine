/* -*- mode: c -*-

   Platform abstraction layer interface.

   NOTE: platform must define
   entrypoint such main() or WinMain() and implement several functions
   beginning with 'Platform'.

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


/// Keyboard

typedef enum {
  // obviously I just copied values from SDL_keycode.h.

  PlatformKey_UNKNOWN = 0,
  PlatformKey_RETURN = '\r',
  PlatformKey_ESCAPE = '\x1B',
  PlatformKey_BACKSPACE = '\b',
  PlatformKey_TAB = '\t',
  PlatformKey_SPACE = ' ',
  PlatformKey_EXCLAIM = '!',
  PlatformKey_QUOTEDBL = '"',
  PlatformKey_HASH = '#',
  PlatformKey_PERCENT = '%',
  PlatformKey_DOLLAR = '$',
  PlatformKey_AMPERSAND = '&',
  PlatformKey_QUOTE = '\'',
  PlatformKey_LEFTPAREN = '(',
  PlatformKey_RIGHTPAREN = ')',
  PlatformKey_ASTERISK = '*',
  PlatformKey_PLUS = '+',
  PlatformKey_COMMA = ',',
  PlatformKey_MINUS = '-',
  PlatformKey_PERIOD = '.',
  PlatformKey_SLASH = '/',
  PlatformKey_0 = '0',
  PlatformKey_1 = '1',
  PlatformKey_2 = '2',
  PlatformKey_3 = '3',
  PlatformKey_4 = '4',
  PlatformKey_5 = '5',
  PlatformKey_6 = '6',
  PlatformKey_7 = '7',
  PlatformKey_8 = '8',
  PlatformKey_9 = '9',
  PlatformKey_COLON = ':',
  PlatformKey_SEMICOLON = ';',
  PlatformKey_LESS = '<',
  PlatformKey_EQUALS = '=',
  PlatformKey_GREATER = '>',
  PlatformKey_QUESTION = '?',
  PlatformKey_AT = '@',
  PlatformKey_LEFTBRACKET = '[',
  PlatformKey_BACKSLASH = '\\',
  PlatformKey_RIGHTBRACKET = ']',
  PlatformKey_CARET = '^',
  PlatformKey_UNDERSCORE = '_',
  PlatformKey_BACKQUOTE = '`',
  PlatformKey_A = 'a',
  PlatformKey_B = 'b',
  PlatformKey_C = 'c',
  PlatformKey_D = 'd',
  PlatformKey_E = 'e',
  PlatformKey_F = 'f',
  PlatformKey_G = 'g',
  PlatformKey_H = 'h',
  PlatformKey_I = 'i',
  PlatformKey_J = 'j',
  PlatformKey_K = 'k',
  PlatformKey_L = 'l',
  PlatformKey_M = 'm',
  PlatformKey_N = 'n',
  PlatformKey_O = 'o',
  PlatformKey_P = 'p',
  PlatformKey_Q = 'q',
  PlatformKey_R = 'r',
  PlatformKey_S = 's',
  PlatformKey_T = 't',
  PlatformKey_U = 'u',
  PlatformKey_V = 'v',
  PlatformKey_W = 'w',
  PlatformKey_X = 'x',
  PlatformKey_Y = 'y',
  PlatformKey_Z = 'z',

  // I calculated this myself
  PlatformKey_LSHIFT = 1073742049

} PlatformKeyCode;


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

// after called, application must exit from main loop
void PlatformWantToQuit();

// get error message from platform layer
// NOTE: this function can return just an empty string, it made for convenience
const char* PlatformGetError();

// get list of files from data directory that were modified.
// This helps to do fancy things like hot resource reloading, hot code reloading etc.
size_t PlatformDataDirectoryModified(const char** filenames, size_t buff_size);


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
  // value should be a power of 2 < 32
  int msaa_samples;

} Engine_Startup_Info;

// log a message using engine's builtin logger
void EngineLog(int level, const char* file, int line, const char* fmt, ...);

#define LOG_MSG(level, ...) EngineLog(level, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_TRACE(...)  LOG_MSG(0, __VA_ARGS__)
#define LOG_DEBUG(...)  LOG_MSG(1, __VA_ARGS__)
#define LOG_INFO(...)   LOG_MSG(2, __VA_ARGS__)
#define LOG_WARN(...)   LOG_MSG(3, __VA_ARGS__)
#define LOG_ERROR(...)  LOG_MSG(4, __VA_ARGS__)
#define LOG_FATAL(...)  LOG_MSG(5, __VA_ARGS__)

// add a logger. Platform must add a logger otherwise engine will be
// silent and won't produce any log messages.
void EngineAddLogger(Log_Function logger, int level, void* udata);

// Initialize engine. This function should be called at startup before
// main loop.
void EngineInit(const Engine_Startup_Info* info);

// Free resources used by engine. This function should be called after
// main loop.
void EngineFree();

// Render a frame. This function should be called inside main loop.
void EngineUpdateAndRender();

// platform calls this when a key pressed
void EngineKeyPressed(PlatformKeyCode key);

// platform calls this when a key released
void EngineKeyReleased(PlatformKeyCode key);

// platform calls this when mouse moved
void EngineMouseMotion(int x, int y, int xrel, int yrel);


#ifdef __cplusplus
}
#endif
