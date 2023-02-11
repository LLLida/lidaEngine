/*
  Basic core of utilities and algorithms that whole engine uses.
 */

#define STB_SPRINTF_IMPLEMENTATION
#include "lib/stb_sprintf.h"


/// Attributes
#ifdef __GNUC__
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#Common-Function-Attributes
#define ATTRIBUTE_PRINTF(i) __attribute__((format (printf, i, i+1)))
#define ATTRIBUTE_NONNULL(...) __attribute__((nonnull (__VA_ARGS__)))
#define ATTRIBUTE_FALLTHROUGH() __attribute__((fallthrough))
#define ATTRIBUTE_ALIGNED(a) __attribute__((aligned(a)))
#else
#define ATTRIBUTE_PRINTF(i)
#define ATTRIBUTE_NONNULL(...)
#define ATTRIBUTE_FALLTHROUGH()
#define ATTRIBUTE_ALIGNED(a)
#endif


/// Assert

// TODO: use own implementation of assert
#include <assert.h>

#define Assert(expr) assert(expr)
#define StaticAssert(expr) _StaticAssert(expr)


/// Memory

#define MEM_ALIGN_OFF(ptr, pow2) (((~(uintptr_t)(ptr))+1) & (pow2-1))

typedef struct {
  void* ptr;
  size_t size;
  size_t left;
  //size_t right;
} Memory_Chunk;

// allocate memory that is aligned to 8
// FIXME: this looks kinda ugly.. What if we need our memory to be aligned to 16? 64? 256?
// Should we introduce some kind of macro?
INTERNAL void*
MemoryAllocateLeft(Memory_Chunk* chunk, uint32_t size)
{
  Assert(chunk->left + size < chunk->size);
  uint8_t* start = (uint8_t*)chunk->ptr + chunk->left;
  void* ATTRIBUTE_ALIGNED(8) bytes = start + MEM_ALIGN_OFF(start, 8);
  chunk->left += size + MEM_ALIGN_OFF(start, 8);
  return bytes;
}

INTERNAL void
MemoryPopLeft(Memory_Chunk* chunk, void* ptr)
{
  Assert(ptr > chunk->ptr && (size_t)((uint8_t*)ptr - (uint8_t*)chunk->ptr) <= chunk->size);
  chunk->left = (uint8_t*)ptr - (uint8_t*)chunk->ptr;
}

INTERNAL void
MemoryChunkReset(Memory_Chunk* chunk)
{
  chunk->left = 0;
}

GLOBAL Memory_Chunk g_persistent_memory;

#define PersistentAllocate(size) MemoryAllocateLeft(&g_persistent_memory, size)
#define PersistentPop(size) MemoryPopLeft(&g_persistent_memory, size)


/// Logging

#define MAX_LOGGERS 16

typedef struct {
  Log_Function func;
  int level;
  void* udata;
} Logger;

GLOBAL uint32_t g_num_loggers;
GLOBAL Logger g_loggers[MAX_LOGGERS];
GLOBAL char g_log_buffer[1024];

void
EngineLog(int level, const char* file, int line, const char* fmt, ...)
{
  Log_Event log_event = {
    .str = g_log_buffer,
    .file = file,
    .line = line,
    .level = level,
  };
  va_list ap;
  va_start(ap, fmt);
  // write message to buffer
  log_event.strlen = stbsp_vsnprintf(g_log_buffer, sizeof(g_log_buffer), fmt, ap);
  va_end(ap);
  // call loggers
  for (uint32_t i = 0; i < g_num_loggers; i++) {
    // FIXME: should we sort loggers by their level, so we can skip a lot of them?
    if (level >= g_loggers[i].level) {
      log_event.udata = g_loggers[i].udata;
      g_loggers[i].func(&log_event);
    }
  }
}

void
EngineAddLogger(Log_Function func, int level, void* udata)
{
  if (g_num_loggers == MAX_LOGGERS) {
    return;
  }
  g_loggers[g_num_loggers] = (Logger) {
    .func = func,
    .level = level,
    .udata = udata,
  };
  g_num_loggers++;
}

#define LOG_MSG(level, ...) EngineLog(level, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_TRACE(...)  LOG_MSG(0, __VA_ARGS__)
#define LOG_DEBUG(...)  LOG_MSG(1, __VA_ARGS__)
#define LOG_INFO(...)   LOG_MSG(2, __VA_ARGS__)
#define LOG_WARN(...)   LOG_MSG(3, __VA_ARGS__)
#define LOG_ERROR(...)  LOG_MSG(4, __VA_ARGS__)
#define LOG_FATAL(...)  LOG_MSG(5, __VA_ARGS__)


/// Some useful algorithms

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

INTERNAL uint32_t
NearestPow2(uint32_t v)
{
  // https://stackoverflow.com/questions/466204/rounding-up-to-next-power-of-2
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

INTERNAL uint32_t
HashString32(const char* str)
{
  // https://cp-algorithms.com/string/string-hashing.html
  uint32_t hash_value = 0;
  uint32_t p_pow = 1;
  char c;
#define HASH_P 31
#define HASH_M 1000009
  while ((c = *str)) {
    hash_value = (hash_value + (c - 'a' + 1) * p_pow) % HASH_M;
    p_pow = (p_pow * HASH_P) % HASH_M;
    str++;
  }
#undef HASH_P
#undef HASH_M
  return hash_value;
}

INTERNAL uint64_t
HashString64(const char* str)
{
  // https://cp-algorithms.com/string/string-hashing.html
  uint64_t hash_value = 0;
  uint64_t p_pow = 1;
  char c;
#define HASH_P 31
#define HASH_M 1000009
  while ((c = *str)) {
    hash_value = (hash_value + (c - 'a' + 1) * p_pow) % HASH_M;
    p_pow = (p_pow * HASH_P) % HASH_M;
    str++;
  }
#undef HASH_P
#undef HASH_M
  return hash_value;
}

INTERNAL uint32_t
HashCombine32(const uint32_t* hashes, uint32_t num_hashes)
{
  // https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
  uint32_t hash = 0;
  for (uint32_t i = 0; i < num_hashes; i++) {
    hash ^= hashes[i] + 0x9e3779b9 + (hash<<6) + (hash>>2);
  }
  return hash;
}

INTERNAL uint64_t
HashCombine64(const uint64_t* hashes, uint32_t num_hashes)
{
  // https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
  uint64_t hash = 0;
  for (uint32_t i = 0; i < num_hashes; i++) {
    hash ^= hashes[i] + 0x9e3779b9 + (hash<<6) + (hash>>2);
  }
  return hash;
}

INTERNAL uint32_t
HashMemory32(const void* key, uint32_t bytes)
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
    ATTRIBUTE_FALLTHROUGH();
  case 2: h ^= chars[1] << 8;
    ATTRIBUTE_FALLTHROUGH();
  case 1: h ^= chars[0];
    h *= m;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

INTERNAL uint64_t
HashMemory64(const void* key, uint32_t bytes)
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
      ATTRIBUTE_FALLTHROUGH();
    case 6: h ^= (uint64_t)chars[5] << 40;
      ATTRIBUTE_FALLTHROUGH();
    case 5: h ^= (uint64_t)chars[4] << 32;
      ATTRIBUTE_FALLTHROUGH();
    case 4: h ^= (uint64_t)chars[3] << 24;
      ATTRIBUTE_FALLTHROUGH();
    case 3: h ^= (uint64_t)chars[2] << 16;
      ATTRIBUTE_FALLTHROUGH();
    case 2: h ^= (uint64_t)chars[1] << 8;
      ATTRIBUTE_FALLTHROUGH();
    case 1: h ^= (uint64_t)chars[0];
      h *= m;
  };
  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  return h;
}


/// Type info

typedef struct {

  const char* name;
  uint64_t type_hash;
  uint16_t size;
  uint16_t alignment;

} Type_Info;

#define TYPE_INFO(type) (Type_Info) { .name = #type, .type_hash = HashString64(#type), .size = sizeof(type), .alignment = alignof(type) }
