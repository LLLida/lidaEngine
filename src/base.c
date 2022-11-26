#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/// Logging

#define MAX_LOGGERS 32

typedef struct {
  lida_LogFunction func;
  int level;
  void* udata;
} Logger;

uint32_t g_num_loggers = 0;
Logger g_loggers[MAX_LOGGERS];
char g_log_buffer[1024];
lida_LogEvent g_log_event;

void
lida_Log(int level, const char* file, int line, const char* fmt, ...)
{
  g_log_event.str = g_log_buffer;
  g_log_event.file = file;
  g_log_event.line = line;
  g_log_event.level = level;
  va_list ap;
  va_start(ap, fmt);
  // write message to buffer
  g_log_event.strlen = vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, ap);
  va_end(ap);
  // call loggers
  for (uint32_t i = 0; i < g_num_loggers; i++) {
    // FIXME: I could optimise this check by sorting loggers by their level
    if (level >= g_loggers[i].level) {
      g_log_event.udata = g_loggers[i].udata;
      g_loggers[i].func(&g_log_event);
    }
  }
}

int
lida_AddLogger(lida_LogFunction func, int level, void* udata)
{
  if (g_num_loggers == MAX_LOGGERS) {
    return -1;
  }
  g_loggers[g_num_loggers].func = func;
  g_loggers[g_num_loggers].level = level;
  g_loggers[g_num_loggers].udata = udata;
  g_num_loggers++;
  return g_num_loggers;
}

int
lida_RemoveLogger(lida_LogFunction func)
{
  for (uint32_t i = 0; i < g_num_loggers; i++) {
    if (g_loggers[i].func == func) {
      if (i != g_num_loggers-1) {
        memcpy(&g_loggers[i], &g_loggers[g_num_loggers-1], sizeof(Logger));
      }
      g_num_loggers--;
      return g_num_loggers;
    }
  }
  return -1;
}

const char*
lida_GetLastLogEvent(int* length)
{
  if (length) {
    *length = g_log_event.strlen;
  }
  return g_log_event.str;
}

static void
printf_logger(const lida_LogEvent* ev) {
  const char* levels[] = { "TRACE",
                           "DEBUG",
                           "INFO",
                           "WARN",
                           "ERROR",
                           "FATAL" };
  const char* colors[] = { "\x1b[94m",
                           "\x1b[36m",
                           "\x1b[32m",
                           "\x1b[33m",
                           "\x1b[31m",
                           "\x1b[35m" };
  const char* white_color = "\x1b[0m";
  const char* gray_color = "\x1b[90m";
#ifdef _WIN32
  printf("[%s] %s:%d %s\n", levels[ev->level], ev->file, ev->line, ev->str);
#else
  printf("[%s%s%s] %s%s:%d%s %s\n", colors[ev->level], levels[ev->level], white_color,
         gray_color, ev->file, ev->line,
         white_color, ev->str);
#endif
}

void
lida_InitPlatformSpecificLoggers()
{
  lida_AddLogger(printf_logger, LIDA_LOG_LEVEL_TRACE, 0);
}


/// Hash table


/// Dynamic array

void*
lida_ArrayGet(lida_Array* array, uint32_t index)
{

}

int
lida_ArrayReserve(lida_Array* array, uint32_t new_size)
{

}

int
lida_ArrayResize(lida_Array* array, uint32_t new_size)
{

}

void*
lida_ArrayPushBack(lida_Array* array)
{

}

int
lida_ArrayPopBack(lida_Array* array)
{

}

void*
lida_ArrayInsert(lida_Array* array, uint32_t index)
{

}

int
lida_ArrayDelete(lida_Array* array, uint32_t index)
{

}
