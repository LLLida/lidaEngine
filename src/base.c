#include "base.h"

#include <assert.h>
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

static const char* const levels[] = {
  "TRACE",
  "DEBUG",
  "INFO",
  "WARN",
  "ERROR",
  "FATAL"
};
static const char* const colors[] = {
  "\x1b[94m",
  "\x1b[36m",
  "\x1b[32m",
  "\x1b[33m",
  "\x1b[31m",
  "\x1b[35m"
};
static const char* const white_color = "\x1b[0m";
static const char* const gray_color = "\x1b[90m";

static void
printf_logger(const lida_LogEvent* ev) {
  size_t filename_offset = (size_t)ev->udata;
#ifdef _WIN32
  // monochromatic
  printf("[%s] %s:%d %s\n", levels[ev->level], ev->file + filename_offset, ev->line, ev->str);
#else
  // colored
  printf("[%s%s%s] %s%s:%d%s %s\n", colors[ev->level], levels[ev->level], white_color,
         gray_color, ev->file + filename_offset, ev->line,
         white_color, ev->str);
#endif
}

void
lida_InitPlatformSpecificLoggers()
{
  // here we make assumption that all calls to loggers are made from engine's source files
  size_t filename_offset = 0;
#ifndef LIDA_SOURCE_DIR
#  define LIDA_SOURCE_DIR ""
#endif
  while (LIDA_SOURCE_DIR[filename_offset] == __FILE__[filename_offset]) {
    filename_offset++;
  }
  lida_AddLogger(printf_logger, LIDA_LOG_LEVEL_TRACE, (void*)filename_offset);
}


/// Hash table

/* Hash table node layout:
  [ ht->flags & 0xFFFF bytes for actual data]
  hash - 8 bytes
  magic - 1 byte
 */

typedef struct {
  uint32_t hash;
  char magic;
} NodeData;

#define HT_NUM_BYTES(ht, number) (((ht)->desc->elem_size + sizeof(uint32_t) + sizeof(char)) * number)
#define HT_PTR_GET(ht, ptr, i) ((char*)ptr + HT_NUM_BYTES(ht, i))
#define HT_PTR_GET_MAGIC(ht, ptr, i) (HT_PTR_GET(ht, ptr, i) + (ht)->desc->elem_size + sizeof(uint32_t))
#define HT_PTR_GET_HASH(ht, ptr, i) (uint32_t*)(HT_PTR_GET(ht, ptr, i) + (ht)->desc->elem_size)
#define HT_GET(ht, i) HT_PTR_GET(ht, ht->ptr, i)
#define HT_GET_MAGIC(ht, i) HT_PTR_GET_MAGIC(ht, ht->ptr, i)
#define HT_GET_HASH(ht, i) HT_PTR_GET_HASH(ht, ht->ptr, i)
#define HT_NODE_NULL 0
#define HT_NODE_VALID 1
#define HT_NODE_DELETED 2

static void*
HT_Insert_no_check(lida_HashTable* ht, void* element, const uint32_t hash)
{
  uint32_t id = hash % ht->allocated;
  while (*HT_GET_MAGIC(ht, id) == HT_NODE_VALID)
    id = (id+1) % ht->allocated;
  void* node = HT_GET(ht, id);
  memcpy(node, element, ht->desc->elem_size);
  *HT_GET_HASH(ht, id) = hash;
  *HT_GET_MAGIC(ht, id) = HT_NODE_VALID;
  ht->size++;
  return node;
}

int
lida_HT_Reserve(lida_HashTable* ht, uint32_t capacity)
{
  if (capacity <= ht->allocated) return 0;
  void* tmp;
  if (ht->desc->flags & LIDA_HT_BUMP_ALLOCATOR) {
    // TODO:
    assert(0);
  } else {
    tmp = ht->ptr;
    ht->ptr = lida_Allocate(ht->desc->allocator, HT_NUM_BYTES(ht, capacity));
    if (ht->ptr == NULL) {
      ht->ptr = tmp;
      return -1;
    }
  }
  // reinsert all elements
  for (uint32_t i = 0; i < capacity; i++)
    *HT_GET_MAGIC(ht, i) = HT_NODE_NULL;
  uint32_t old_capacity = ht->allocated;
  ht->allocated = capacity;
  if (old_capacity > 0) {
    if (ht->size > 0) {
      uint32_t old_size = ht->size;
      ht->size = 0;
      for (uint32_t i = 0; i < old_capacity; i++) {
        if (*HT_PTR_GET_MAGIC(ht, tmp, i) == HT_NODE_VALID) {
          HT_Insert_no_check(ht, HT_PTR_GET(ht, tmp, i), *HT_PTR_GET_HASH(ht, tmp, i));
          if (ht->size == old_size)
            break;
        }
      }
    }
    if (ht->desc->flags & LIDA_HT_BUMP_ALLOCATOR) {
      assert(0);
    } else {
      lida_Free(ht->desc->allocator, tmp);
    }
  }
  return 0;
}

void*
lida_HT_Insert(lida_HashTable* ht, void* element)
{
  if (ht->size >= ht->allocated) {
    if (lida_HT_Reserve(ht, (ht->allocated == 0) ? 1 : (ht->allocated * 2)) != 0)
      return NULL;
  }
  uint32_t hash = ht->desc->hasher(element);
  return HT_Insert_no_check(ht, element, hash);
}

void*
lida_HT_Search(const lida_HashTable* ht, void* element)
{
  uint32_t hash = ht->desc->hasher(element);
  return lida_HT_SearchEx(ht, element, hash);
}

void*
lida_HT_SearchEx(const lida_HashTable* ht, void* element, uint32_t hash)
{
  if (ht->allocated == 0)
    return NULL;
  uint32_t id = hash % ht->allocated;
  for (uint32_t i = 0; i < ht->size; id = (id+1) % ht->allocated) {
    if (*HT_GET_MAGIC(ht, id) == HT_NODE_VALID) {
      if (*HT_GET_HASH(ht, id) == hash &&
          ht->desc->equal(HT_GET(ht, id), element) == 0)
        return HT_GET(ht, id);
      i++;
    } else if (*HT_GET_MAGIC(ht, id) == HT_NODE_NULL) {
      break;
    }
  }
  return NULL;
}

void
lida_HT_Delete(lida_HashTable* ht)
{
  if (ht->ptr) {
    lida_Free(ht->desc->allocator, ht->ptr);
    memset(ht, 0, sizeof(lida_HashTable));
  }
}

void
lida_HT_Iterator_Begin(const lida_HashTable* ht, lida_HT_Iterator* it)
{
  it->ht = ht;
  it->id = 0;
  while (it->id < ht->size &&
         *HT_GET_MAGIC(ht, it->id) != HT_NODE_VALID) {
    it->id++;
  }
}

int
lida_HT_Iterator_Empty(lida_HT_Iterator* it)
{
  return it->id >= it->ht->size;
}

void
lida_HT_Iterator_Next(lida_HT_Iterator* it)
{
  it->id++;
  while (it->id < it->ht->size &&
         *HT_GET_MAGIC(it->ht, it->id) != HT_NODE_VALID) {
    it->id++;
  }
}

void*
lida_HT_Iterator_Get(lida_HT_Iterator* it)
{
  return HT_GET(it->ht, it->id);
}



/// Dynamic array

#define DA_NUM_BYTES(arr, num) ((arr)->desc->elem_size * (num))
#define DA_GET(arr, i) ((char*)((arr)->ptr) + (i) * (arr)->desc->elem_size)

void*
lida_DynArrayGet(lida_DynArray* array, uint32_t index)
{
  if (index >= LIDA_DA_SIZE(array)) {
    return NULL;
  }
  return DA_GET(array, index);
}

int
lida_DynArrayReserve(lida_DynArray* array, uint32_t capacity)
{
  if (capacity <= array->allocated) {
    return 0;
  }
  void* tmp;
  if (array->desc->flags & LIDA_DA_BUMP_ALLOCATOR) {
    // TODO:
    assert(0);
  } else {
    tmp = array->ptr;
    array->ptr = lida_Allocate(array->desc->allocator, DA_NUM_BYTES(array, capacity));
    if (array->ptr == NULL) {
      array->ptr = tmp;
      return -1;
    }
  }
  memcpy(array->ptr, tmp, DA_NUM_BYTES(array, array->size));
  array->allocated = capacity;
  if (array->desc->flags & LIDA_DA_BUMP_ALLOCATOR) {
    assert(0);
  } else {
    lida_Free(array->desc->allocator, tmp);
  }
  return 0;
}

int
lida_DynArrayResize(lida_DynArray* array, uint32_t new_size)
{
  if (lida_DynArrayReserve(array, new_size) != 0)
    return -1;
  // FIXME: probably we should call destructors if new_size < array->size?
  array->size = new_size;
  return 0;
}

void*
lida_DynArrayPushBack(lida_DynArray* array)
{
  if (array->size >= array->allocated) {
    if (lida_DynArrayReserve(array, (array->size == 0) ? 1 : array->size * 2) != 0)
      return NULL;
  }
  array->size++;
  return DA_GET(array, array->size-1);
}

int
lida_DynArrayPopBack(lida_DynArray* array)
{
  if (array->size == 0)
    return -1;
  array->size--;
  return 0;
}

void*
lida_DynArrayInsert(lida_DynArray* array, uint32_t index)
{
  if (array->size >= array->allocated) {
    if (lida_DynArrayReserve(array, (array->size == 0) ? 1 : array->size * 2) != 0)
      return NULL;
  }
  memmove(DA_GET(array, index+1), DA_GET(array, index), DA_NUM_BYTES(array, array->size));
  array->size++;
  return DA_GET(array, index);
}

void
lida_DynArrayDelete(lida_DynArray* array)
{
  if (array->ptr) {
    lida_Free(array->desc->allocator, array->ptr);
    memset(array, 0, sizeof(lida_DynArray));
  }
}


/// Some useful algorithms

#define HASH_P 31
#define HASH_M 1000009

uint32_t
lida_HashString(const char* str)
{
  // https://cp-algorithms.com/string/string-hashing.html
  uint32_t hash_value = 0;
  uint32_t p_pow = 1;
  char c;
  while ((c = *str)) {
    hash_value = (hash_value + (c - 'a' + 1) * p_pow) % HASH_M;
    p_pow = (p_pow * HASH_P) % HASH_M;
    str++;
  }
  return hash_value;
}

uint64_t
lida_HashString64(const char* str)
{
  // https://cp-algorithms.com/string/string-hashing.html
  uint64_t hash_value = 0;
  uint64_t p_pow = 1;
  char c;
  while ((c = *str)) {
    hash_value = (hash_value + (c - 'a' + 1) * p_pow) % HASH_M;
    p_pow = (p_pow * HASH_P) % HASH_M;
    str++;
  }
  return hash_value;
}
