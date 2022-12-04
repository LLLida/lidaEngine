#pragma once

#include "memory.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/// Logging

enum {
  LIDA_LOG_LEVEL_TRACE,
  LIDA_LOG_LEVEL_DEBUG,
  LIDA_LOG_LEVEL_INFO,
  LIDA_LOG_LEVEL_WARN,
  LIDA_LOG_LEVEL_ERROR,
  LIDA_LOG_LEVEL_FATAL,
  LIDA_NUM_LOG_LEVELS
};

typedef struct {
  const char* str;
  const char* file;
  int line;
  int strlen;
  int level;
  void* udata;
} lida_LogEvent;

typedef void(*lida_LogFunction)(const lida_LogEvent* event);

void lida_Log(int level, const char* file, int line, const char* fmt, ...);

#define LIDA_LOG(level, ...) lida_Log(level, __FILE__, __LINE__, __VA_ARGS__)
#define LIDA_LOG_TRACE(...)  LIDA_LOG(LIDA_LOG_LEVEL_TRACE, __VA_ARGS__)
#define LIDA_LOG_DEBUG(...)  LIDA_LOG(LIDA_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LIDA_LOG_INFO(...)   LIDA_LOG(LIDA_LOG_LEVEL_INFO, __VA_ARGS__)
#define LIDA_LOG_WARN(...)   LIDA_LOG(LIDA_LOG_LEVEL_WARN, __VA_ARGS__)
#define LIDA_LOG_ERROR(...)  LIDA_LOG(LIDA_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LIDA_LOG_FATAL(...)  LIDA_LOG(LIDA_LOG_LEVEL_FATAL, __VA_ARGS__)

int lida_AddLogger(lida_LogFunction func, int level, void* udata);
int lida_RemoveLogger(lida_LogFunction func);
const char* lida_GetLastLogMessage(int* length);
void lida_InitPlatformSpecificLoggers();


/// Hash table

typedef uint32_t(*lida_HashFunction)(void* data);
typedef int(*lida_EqualFunction)(void* lhs, void* rhs);

enum {
  LIDA_HT_BUMP_ALLOCATOR = (1<<29),
  LIDA_HT_NO_DELETIONS = (1<<30),
};

typedef struct {
  void* ptr;
  uint32_t allocated;
  uint32_t size;
  // 0-15 bits - element size
  // 31 bit - whether hash table supports deletions
  uint32_t flags;
  // a pure function which returns a 32 bit unsigned integer based on input
  lida_HashFunction hasher;
  // a pure function which compares two objects
  lida_EqualFunction equal;
  lida_Allocator* allocator;
} lida_HashTable;

#define LIDA_HT_EMPTY(type, hashEr, equAl, allocatOr, flaGs) (lida_HashTable) { .ptr = NULL, .allocated = 0, .size = 0, .flags = flaGs|sizeof(type), .hasher = hashEr, .equal = equAl, .allocator = allocatOr }
#define LIDA_HT_DATA(ht, type) (type*)((ht)->ptr)
#define LIDA_HT_CAPACITY(ht) ((ht)->allocated)
#define LIDA_HT_SIZE(ht) ((ht)->size)
#define LIDA_HT_INSERT(ht, key, type) (type*)lida_HT_Insert(ht, key)
#define LIDA_HT_SEARCH(ht, key, type) (type*)lida_HT_Search(ht, key)

int lida_HT_Reserve(lida_HashTable* ht, uint32_t capacity);
void* lida_HT_Insert(lida_HashTable* ht, void* element);
void* lida_HT_Search(const lida_HashTable* ht, void* element);
void* lida_HT_SearchEx(const lida_HashTable* ht, void* element, uint32_t hash);
void lida_HT_Delete(lida_HashTable* ht);


/// Dynamic array(std::vector)

typedef struct {
  void* ptr;
  uint32_t allocated;
  uint32_t size;
  // 0-15 bits - element size
  uint32_t flags;
  lida_Allocator* allocator;
} lida_Array;

#define LIDA_ARRAY_EMPTY(type, allocatOr, flaGs) (lida_Array) { .ptr = NULL, .allocated = 0, .size = 0, .flags = flaGs|sizeof(type), .allocator = allocatOr }
#define LIDA_ARRAY_DATA(array, type) (type*)((array)->ptr)
#define LIDA_ARRAY_CAPACITY(array) ((array)->allocated)
#define LIDA_ARRAY_SIZE(array) ((array)->size)
#define LIDA_ARRAY_GET(array, type, index) (type*)lida_ArrayGet((array), index)

void* lida_ArrayGet(lida_Array* array, uint32_t index);
int lida_ArrayReserve(lida_Array* array, uint32_t new_size);
int lida_ArrayResize(lida_Array* array, uint32_t new_size);
void* lida_ArrayPushBack(lida_Array* array);
int lida_ArrayPopBack(lida_Array* array);
void* lida_ArrayInsert(lida_Array* array, uint32_t index);
int lida_ArrayDelete(lida_Array* array, uint32_t index);

#ifdef __cplusplus
}
#endif
