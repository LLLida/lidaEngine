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


/// Generic container base

typedef uint32_t(*lida_HashFunction)(const void* data);
typedef int(*lida_CompareFunction)(const void* lhs, const void* rhs);
typedef void(*lida_ConstructorFunction)(void* obj);
typedef void(*lida_DestructorFunction)(void* obj);

enum {
  LIDA_TYPE_INFO_USE_BUMP_ALLOCATOR = (1<<29),
};

typedef struct {

  // type's name
  const char* name;
  uint64_t type_hash;
  lida_Allocator* allocator;
  // a pure function which returns a 32 bit unsigned integer based on input
  lida_HashFunction hasher;
  // a pure function which compares two objects
  lida_CompareFunction compare;
  uint16_t elem_size;
  uint16_t flags;

} lida_TypeInfo;

#define LIDA_TYPE_INFO(type, allocator_, hasher_, equal_, flags_) (lida_TypeInfo) { .name = #type, .allocator = allocator_, .hasher = hasher_, .compare = equal_, .type_hash = lida_HashString64(#type), .elem_size = sizeof(type), .flags = flags_ }


/// Hash table

typedef struct {

  void* ptr;
  uint32_t allocated;
  uint32_t size;
  lida_TypeInfo* type;

} lida_HashTable;

typedef struct {

  const lida_HashTable* ht;
  uint32_t id;

} lida_HT_Iterator;

#define LIDA_HT_EMPTY(type_) (lida_HashTable) { .ptr = NULL, .allocated = 0, .size = 0, .type = type_ }
#define LIDA_HT_DATA(ht, type) (type*)((ht)->ptr)
#define LIDA_HT_CAPACITY(ht) ((ht)->allocated)
#define LIDA_HT_SIZE(ht) ((ht)->size)
#define LIDA_HT_INSERT(ht, key, type) (type*)lida_HT_Insert(ht, key)
#define LIDA_HT_SEARCH(ht, key, type) (type*)lida_HT_Search(ht, key)
#define LIDA_HT_FOREACH(ht, it) for (lida_HT_Iterator_Begin(ht, it); !lida_HT_Iterator_Empty(it); lida_HT_Iterator_Next(it))

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

void lida_HT_Iterator_Begin(const lida_HashTable* ht, lida_HT_Iterator* it)
  LIDA_ATTRIBUTE_NONNULL(1, 2);
int lida_HT_Iterator_Empty(lida_HT_Iterator* it)
  LIDA_ATTRIBUTE_NONNULL(1);
void lida_HT_Iterator_Next(lida_HT_Iterator* it)
  LIDA_ATTRIBUTE_NONNULL(1);
void* lida_HT_Iterator_Get(lida_HT_Iterator* it)
  LIDA_ATTRIBUTE_NONNULL(1);


/// Dynamic array(std::vector)

enum {
  LIDA_DA_BUMP_ALLOCATOR = (1<<0),
};

typedef struct {

  void* ptr;
  uint32_t allocated;
  uint32_t size;
  lida_TypeInfo* type;

} lida_DynArray;

#define LIDA_DA_EMPTY(type_) (lida_DynArray) { .ptr = NULL, .allocated = 0, .size = 0, .type = type_ }
#define LIDA_DA_DATA(array, type) (type*)((array)->ptr)
#define LIDA_DA_CAPACITY(array) ((array)->allocated)
#define LIDA_DA_SIZE(array) ((array)->size)
#define LIDA_DA_GET(array, type, index) (type*)lida_ArrayGet((array), index)
#define LIDA_DA_PUSH_BACK(array, type_name, ...) memcpy(lida_DynArrayPushBack(array), &(type_name) { __VA_ARGS__ }, (array)->type->elem_size)
#define LIDA_DA_INSERT(array, i, type_name, ...) memcpy(lida_DynArrayInsert(array, i), &(type_name) { __VA_ARGS__ }, (array)->type->elem_size)

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

#define LIDA_ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define LIDA_ALIGN_TO(number, alignment) ((number) % (alignment)) ? \
  ((number) + (alignment) - (number) % (alignment)) : (number)

// compute 32-bit hash for string
uint32_t lida_HashString32(const char* str);
// compute 64-bit hash for string
uint64_t lida_HashString64(const char* str);
#define lida_HashString(str) lida_HashString32(str)
uint32_t lida_HashCombine32(const uint32_t* hashes, uint32_t num_hashes);
uint64_t lida_HashCombine64(const uint64_t* hashes, uint32_t num_hashes);
#define lida_HashCombine(hashes, num_hashes) lida_HashCombine32(hashes, num_hashes)

void lida_qsort(void* data, uint32_t num_elements, lida_TypeInfo* type);

#ifdef __cplusplus
}
#endif
