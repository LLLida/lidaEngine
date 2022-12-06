#pragma once

#include "memory.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#Common-Function-Attributes
#define LIDA_ATTRIBUTE_PRINTF(i) __attribute__((format (printf, i, i+1)))
#define LIDA_ATTRIBUTE_NONNULL(...) __attribute__((nonnull (__VA_ARGS__)))
#else
#define LIDA_ATTRIBUTE_PRINTF(i)
#define LIDA_ATTRIBUTE_NONNULL(...)
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
  // log message
  const char* str;
  // name of file where message was send from
  const char* file;
  // line of file where message was send from
  int line;
  // precomputed message length
  int strlen;
  // log level
  int level;
  // user-defined data
  void* udata;
} lida_LogEvent;

typedef void(*lida_LogFunction)(const lida_LogEvent* event);

// log a message with level=[LIDA_LOG_LEVEL...LIDA_NUM_LOG_LEVELS]
void lida_Log(int level, const char* file, int line, const char* fmt, ...) LIDA_ATTRIBUTE_PRINTF(4);

#define LIDA_LOG(level, ...) lida_Log(level, __FILE__, __LINE__, __VA_ARGS__)
#define LIDA_LOG_TRACE(...)  LIDA_LOG(LIDA_LOG_LEVEL_TRACE, __VA_ARGS__)
#define LIDA_LOG_DEBUG(...)  LIDA_LOG(LIDA_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LIDA_LOG_INFO(...)   LIDA_LOG(LIDA_LOG_LEVEL_INFO, __VA_ARGS__)
#define LIDA_LOG_WARN(...)   LIDA_LOG(LIDA_LOG_LEVEL_WARN, __VA_ARGS__)
#define LIDA_LOG_ERROR(...)  LIDA_LOG(LIDA_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LIDA_LOG_FATAL(...)  LIDA_LOG(LIDA_LOG_LEVEL_FATAL, __VA_ARGS__)

// add a callback which will be called on every log message
// level - minimal level which logger accepts, set it to 0 so logger will be called on every message
// udata - user-defined data which will be passed to func
int lida_AddLogger(lida_LogFunction func, int level, void* udata) LIDA_ATTRIBUTE_NONNULL(1);
int lida_RemoveLogger(lida_LogFunction func) LIDA_ATTRIBUTE_NONNULL(1);
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

typedef struct {
  void* ptr;
  uint32_t size;
  uint32_t flags;
} lida_HT_Iterator;

#define LIDA_HT_EMPTY(type, hashEr, equAl, allocatOr, flaGs) (lida_HashTable) { .ptr = NULL, .allocated = 0, .size = 0, .flags = flaGs|sizeof(type), .hasher = hashEr, .equal = equAl, .allocator = allocatOr }
#define LIDA_HT_DATA(ht, type) (type*)((ht)->ptr)
#define LIDA_HT_CAPACITY(ht) ((ht)->allocated)
#define LIDA_HT_SIZE(ht) ((ht)->size)
#define LIDA_HT_INSERT(ht, key, type) (type*)lida_HT_Insert(ht, key)
#define LIDA_HT_SEARCH(ht, key, type) (type*)lida_HT_Search(ht, key)

// preallocate some space for hash table
// when used in right way this can significantly improve performance
int lida_HT_Reserve(lida_HashTable* ht, uint32_t capacity)
  LIDA_ATTRIBUTE_NONNULL(1);
// insert an element to hash table
// best case - O(1), worst case - O(N), average case - O(1)
void* lida_HT_Insert(lida_HashTable* ht, void* element)
  LIDA_ATTRIBUTE_NONNULL(1, 2);
// check if hash table has element
// returns pointer to an element in hash table if found and NULL otherwise
void* lida_HT_Search(const lida_HashTable* ht, void* element)
  LIDA_ATTRIBUTE_NONNULL(1, 2);
// same as lida_HT_Search but search with precomputed hash
void* lida_HT_SearchEx(const lida_HashTable* ht, void* element, uint32_t hash)
  LIDA_ATTRIBUTE_NONNULL(1, 2);
// free memory used by hash table
void lida_HT_Delete(lida_HashTable* ht)
  LIDA_ATTRIBUTE_NONNULL(1);


/// Dynamic array(std::vector)

enum {
  LIDA_DA_BUMP_ALLOCATOR = (1<<29),
};

typedef struct {
  void* ptr;
  uint32_t allocated;
  uint32_t size;
  // 0-15 bits - element size
  uint32_t flags;
  lida_Allocator* allocator;
} lida_DynArray;

#define LIDA_DA_EMPTY(type, allocatOr, flaGs) (lida_Array) { .ptr = NULL, .allocated = 0, .size = 0, .flags = flaGs|sizeof(type), .allocator = allocatOr }
#define LIDA_DA_DATA(array, type) (type*)((array)->ptr)
#define LIDA_DA_CAPACITY(array) ((array)->allocated)
#define LIDA_DA_SIZE(array) ((array)->size)
#define LIDA_DA_GET(array, type, index) (type*)lida_ArrayGet((array), index)

void* lida_DynArrayGet(lida_DynArray* array, uint32_t index)
  LIDA_ATTRIBUTE_NONNULL(1);
int lida_DynArrayReserve(lida_DynArray* array, uint32_t new_capacity)
  LIDA_ATTRIBUTE_NONNULL(1);
int lida_DynArrayResize(lida_DynArray* array, uint32_t new_size)
  LIDA_ATTRIBUTE_NONNULL(1);
void* lida_DynArrayPushBack(lida_DynArray* array)
  LIDA_ATTRIBUTE_NONNULL(1);
int lida_DynArrayPopBack(lida_DynArray* array)
  LIDA_ATTRIBUTE_NONNULL(1);
void* lida_DynArrayInsert(lida_DynArray* array, uint32_t index)
  LIDA_ATTRIBUTE_NONNULL(1);
void lida_DynArrayDelete(lida_DynArray* array)
  LIDA_ATTRIBUTE_NONNULL(1);


/// Some useful algorithms

// compute 32-bit hash for string
uint32_t lida_HashString32(const char* str);
// compute 64-bit hash for string
uint64_t lida_HashString64(const char* str);
#define lida_HashString(str) lida_HashString32(str)

#ifdef __cplusplus
}
#endif
