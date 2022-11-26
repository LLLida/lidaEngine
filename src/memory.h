#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lida_Allocator lida_Allocator;

typedef void*(*lida_AllocFunc)(lida_Allocator* a, uint32_t bytes);
typedef void(*lida_FreeFunc)(lida_Allocator* a, void* ptr);
typedef void*(*lida_ReallocFunc)(lida_Allocator* a, void* ptr, uint32_t bytes);

struct lida_Allocator {
  lida_AllocFunc alloc;
  lida_FreeFunc free;
  lida_ReallocFunc realloc;
  void* udata;
};

#define lida_Allocate(allocator, bytes) ((allocator)->alloc((allocator), bytes))
#define lida_Free(allocator, ptr) ((allocator)->free((allocator), ptr))
#define lida_Reallocate(allocator, ptr, bytes) ((allocator)->realloc((allocator), ptr, bytes))

lida_Allocator* lida_TempAllocator();

int lida_TempAllocatorCreate(uint32_t initial_size);

void lida_TempAllocatorDestroy();

void* lida_TempAllocate(uint32_t bytes);

// Allocation frees always must come in order;
// memory allocated first must be freed last, and memory allocated last must be freed first. Example:
/*
void* a1 = lida_TempAllocate(10);
void* a2 = lida_TempAllocate(10);
void* a3 = lida_TempAllocate(10);
lida_TempFree(a3);
lida_TempFree(a2);
lida_TempFree(a1);
 */
uint32_t lida_TempFree(void* ptr);

lida_Allocator* lida_MallocAllocator();
void* lida_Malloc(uint32_t bytes);
void lida_MallocFree(void* ptr);
void* lida_Realloc(void* ptr, uint32_t bytes);

#ifdef __cplusplus
}
#endif
