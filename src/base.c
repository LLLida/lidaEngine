#include "base.h"

#include <assert.h>
#include <stdarg.h>
#include <SDL_rwops.h>
#include <SDL_stdinc.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <stdio.h>
#include <string.h>

#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"


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
  // g_log_event.strlen = vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, ap);
  g_log_event.strlen = stbsp_vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, ap);
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
  // here we make assumption that all calls to loggers are called from engine's source files
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

#define HT_NUM_BYTES(ht, number) (((ht)->type->elem_size + sizeof(uint32_t) + sizeof(char)) * number)
#define HT_PTR_GET(ht, ptr, i) ((char*)ptr + HT_NUM_BYTES(ht, i))
#define HT_PTR_GET_MAGIC(ht, ptr, i) (HT_PTR_GET(ht, ptr, i) + (ht)->type->elem_size + sizeof(uint32_t))
#define HT_PTR_GET_HASH(ht, ptr, i) (uint32_t*)(HT_PTR_GET(ht, ptr, i) + (ht)->type->elem_size)
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
  memcpy(node, element, ht->type->elem_size);
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
  if (ht->type->flags & LIDA_TYPE_INFO_USE_BUMP_ALLOCATOR) {
    // TODO:
    assert(0);
  } else {
    tmp = ht->ptr;
    ht->ptr = lida_Allocate(ht->type->allocator, HT_NUM_BYTES(ht, capacity));
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
    if (ht->type->flags & LIDA_TYPE_INFO_USE_BUMP_ALLOCATOR) {
      assert(0);
    } else {
      lida_Free(ht->type->allocator, tmp);
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
  uint32_t hash = lida_HashMemory32(element, ht->type->elem_size);
  return HT_Insert_no_check(ht, element, hash);
}

void*
lida_HT_Search(const lida_HashTable* ht, void* element)
{
  uint32_t hash = lida_HashMemory32(element, ht->type->elem_size);
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
          memcmp(HT_GET(ht, id), element, ht->type->elem_size) == 0)
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
    lida_Free(ht->type->allocator, ht->ptr);
    ht->ptr = NULL;
    ht->allocated = 0;
    ht->size = 0;
  }
}

void
lida_HT_Iterator_Begin(const lida_HashTable* ht, lida_HT_Iterator* it)
{
  it->ht = ht;
  it->id = 0;
  it->remaining = ht->size;
  while (it->id < ht->size &&
         *HT_GET_MAGIC(ht, it->id) != HT_NODE_VALID) {
    it->id++;
  }
}

int
lida_HT_Iterator_Empty(lida_HT_Iterator* it)
{
  return it->remaining == 0;
}

void
lida_HT_Iterator_Next(lida_HT_Iterator* it)
{
  it->id++;
  while (it->id < it->ht->allocated &&
         *HT_GET_MAGIC(it->ht, it->id) != HT_NODE_VALID) {
    it->id++;
  }
  it->remaining--;
}

void*
lida_HT_Iterator_Get(lida_HT_Iterator* it)
{
  return HT_GET(it->ht, it->id);
}



/// Dynamic array

#define DA_NUM_BYTES(arr, num) ((arr)->type->elem_size * (num))
#define DA_GET(arr, i) ((char*)((arr)->ptr) + (i) * (arr)->type->elem_size)

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
  if (array->type->flags & LIDA_DA_BUMP_ALLOCATOR) {
    // TODO:
    assert(0);
  } else {
    tmp = array->ptr;
    array->ptr = lida_Allocate(array->type->allocator, DA_NUM_BYTES(array, capacity));
    if (array->ptr == NULL) {
      array->ptr = tmp;
      return -1;
    }
  }
  memcpy(array->ptr, tmp, DA_NUM_BYTES(array, array->size));
  array->allocated = capacity;
  if (array->type->flags & LIDA_DA_BUMP_ALLOCATOR) {
    assert(0);
  } else {
    lida_Free(array->type->allocator, tmp);
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
lida_DynArrayClear(lida_DynArray* array)
{
  array->size = 0;
}

void
lida_DynArrayDelete(lida_DynArray* array)
{
  if (array->ptr) {
    lida_Free(array->type->allocator, array->ptr);
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

uint32_t
lida_HashCombine32(const uint32_t* hashes, uint32_t num_hashes)
{
  // https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
  uint32_t hash = 0;
  for (uint32_t i = 0; i < num_hashes; i++) {
    hash ^= hashes[i] + 0x9e3779b9 + (hash<<6) + (hash>>2);
  }
  return hash;
}

uint64_t
lida_HashCombine64(const uint64_t* hashes, uint32_t num_hashes)
{
  // https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
  uint64_t hash = 0;
  for (uint32_t i = 0; i < num_hashes; i++) {
    hash ^= hashes[i] + 0x9e3779b9 + (hash<<6) + (hash>>2);
  }
  return hash;
}

uint32_t
lida_HashMemory32(const void* key, uint32_t bytes)
{
  // based on MurmurHash2: https://sites.google.com/site/murmurhash/
  const uint32_t seed = LIDA_ENGINE_VERSION;
  const uint32_t m = 0x5bd1e995;
  const int r = 24;
  uint32_t h = seed ^ bytes;
  const uint32_t* data = key;
  while (bytes >= 4) {
    uint32_t k = *(data++);

    k *= m;
    k ^= k >> r;
    k *= m;

    h *= m;
    h ^= k;

    bytes -= 4;
  }
  // handle remaining bytes
  const unsigned char* chars = (const unsigned char*)data;
  switch (bytes) {
  case 3: h ^= chars[2] << 16;
    LIDA_ATTRIBUTE_FALLTHROUGH();
  case 2: h ^= chars[1] << 8;
    LIDA_ATTRIBUTE_FALLTHROUGH();
  case 1: h ^= chars[0];
    h *= m;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

uint64_t
lida_HashMemory64(const void* key, uint32_t bytes)
{
  // based on MurmurHash2
  const uint32_t seed = LIDA_ENGINE_VERSION;
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;
  uint64_t h = seed ^ (bytes * m);
  const uint64_t * data = (const uint64_t *)key;
  const uint64_t * end = data + (bytes/8);
  while(data != end) {
    uint64_t k = *(data++);

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }
  // handle remaining bytes
  const unsigned char * chars = (const unsigned char*)data;
  switch(bytes & 7) {
    case 7: h ^= (uint64_t)chars[6] << 48;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 6: h ^= (uint64_t)chars[5] << 40;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 5: h ^= (uint64_t)chars[4] << 32;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 4: h ^= (uint64_t)chars[3] << 24;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 3: h ^= (uint64_t)chars[2] << 16;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 2: h ^= (uint64_t)chars[1] << 8;
      LIDA_ATTRIBUTE_FALLTHROUGH();
    case 1: h ^= (uint64_t)chars[0];
      h *= m;
  };
  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  return h;
}


/// Profiling

struct {

  // SDL_mutex* mutex;
  SDL_RWops* file;
  uint64_t frequency;

} g_profiler;

void
lida_ProfilerBeginSession(const char* results)
{
  if (g_profiler.file) {
    lida_ProfilerEndSession();
  }
  g_profiler.file = SDL_RWFromFile(results, "wb");
  if (!g_profiler.file) {
    LIDA_LOG_ERROR("failed to create file for writing profile results with error %s", SDL_GetError());
    return;
  }
  const char* header = "{\"otherData\": {},\"traceEvents\":[{}";
  SDL_RWwrite(g_profiler.file, header, 1, strlen(header));
   // we need microseconds, not seconds
  g_profiler.frequency = SDL_GetPerformanceFrequency() / 1000000;
}

void
lida_ProfilerEndSession()
{
  const char* footer = "]}";
  SDL_RWwrite(g_profiler.file, footer, strlen(footer), 1);
  SDL_RWclose(g_profiler.file);
  g_profiler.file = NULL;
}

void
lida_ProfilerStartFunc(lida_ProfileResult* profile, const char* name)
{
  profile->name = name;
  profile->start = SDL_GetPerformanceCounter();
  profile->thread_id = SDL_ThreadID();
}

void
lida_ProfilerEndFunc(lida_ProfileResult* profile)
{
  profile->duration = SDL_GetPerformanceCounter() - profile->start;
  if (g_profiler.file == NULL) {
    return;
  }
  char buff[128];
  size_t bytes = stbsp_snprintf(buff, sizeof(buff), ",\n{\"cat\":\"function\",\n");
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);

  bytes = stbsp_snprintf(buff, sizeof(buff), "\"dur\" : %I64d,\n", profile->duration / g_profiler.frequency);
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);

  bytes = stbsp_snprintf(buff, sizeof(buff), "\"name\" : \"%s\",\n", profile->name);
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);

  bytes = stbsp_snprintf(buff, sizeof(buff), "\"ph\":\"X\", \"pid\":0,\n");
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);

  bytes = stbsp_snprintf(buff, sizeof(buff), "\"tid\": %lu,", profile->thread_id);
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);

  bytes = stbsp_snprintf(buff, sizeof(buff), "\"ts\": %I64d\n}", profile->start / g_profiler.frequency);
  SDL_RWwrite(g_profiler.file, buff, 1, bytes);
}
