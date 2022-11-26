#include "memory.h"

#include <stdlib.h>
#include <assert.h>


/// Temp allocator - also known as bump allocator

lida_Allocator* g_temp_allocator;

typedef struct MemoryChunk MemoryChunk;

struct MemoryChunk {
  void* ptr;
  MemoryChunk* parent;
  uint32_t offset;
  uint32_t size;
};

static void*
TempAllocate(lida_Allocator* a, uint32_t bytes)
{
  MemoryChunk* chunk = (MemoryChunk*)a->udata;
  if (chunk->size - chunk->offset + sizeof(MemoryChunk) <= bytes) {
    uint32_t sz = chunk->size * 2;
    if (sz < bytes) sz = bytes;
    void* ptr = malloc(sz);
    MemoryChunk* tmp = (MemoryChunk*)ptr;
    tmp->parent = chunk;
    chunk = tmp;
    chunk->ptr = ptr;
    chunk->size = sz;
    chunk->offset = sizeof(MemoryChunk);
    a->udata = chunk;
  }
  void* ret = (char*)chunk->ptr + chunk->offset;
  chunk->offset += bytes;
  return ret;
}

static void
TempFree(lida_Allocator* a, void* ptr)
{
  MemoryChunk* chunk = (MemoryChunk*)a;
  uint32_t tmp = chunk->offset;
  chunk->offset = (char*)ptr - (char*)chunk->ptr;
  tmp -= chunk->offset;
  if (chunk->offset == 0) {
    a->udata = chunk->parent;
    free(chunk->ptr);
  }
}

static void*
TempRealloc(lida_Allocator* a, void* ptr, uint32_t bytes)
{
  MemoryChunk* chunk = (MemoryChunk*)a;
  if (ptr < chunk->ptr || (char*)ptr > (char*)chunk->ptr + chunk->offset) {
    return NULL;
  }
  TempFree(a, ptr);
  return TempAllocate(a, bytes);
}

lida_Allocator*
lida_TempAllocator()
{
  return g_temp_allocator;
}

int
lida_TempAllocatorCreate(uint32_t initial_size)
{
  assert(initial_size > 1024);
  void* bytes = malloc(initial_size);
  if (bytes == NULL) {
    return 0;
  }
  g_temp_allocator = (lida_Allocator*)bytes;
  MemoryChunk* chunk = (MemoryChunk*)(g_temp_allocator+1);
  chunk->ptr = bytes;
  chunk->parent = NULL;
  chunk->offset = sizeof(MemoryChunk) + sizeof(lida_Allocator);
  chunk->size = initial_size;
  g_temp_allocator->udata = chunk;
  g_temp_allocator->alloc = TempAllocate;
  g_temp_allocator->free = TempFree;
  g_temp_allocator->realloc = TempRealloc;
  return 1;
}

void
lida_TempAllocatorDestroy()
{
  MemoryChunk* chunk = (MemoryChunk*)g_temp_allocator->udata;
  MemoryChunk* parent;
  while (chunk) {
    parent = chunk->parent;
    free(chunk->ptr);
    chunk = parent;
  }
  g_temp_allocator = NULL;
}

void*
lida_TempAllocate(uint32_t bytes)
{
  return TempAllocate(g_temp_allocator, bytes);
}

uint32_t
lida_TempFree(void* ptr)
{
  MemoryChunk* chunk = (MemoryChunk*)g_temp_allocator->udata;
  if (ptr < chunk->ptr || (char*)ptr > (char*)chunk->ptr + chunk->offset) {
    return UINT32_MAX;
  }
  uint32_t tmp = chunk->offset;
  TempFree(g_temp_allocator, ptr);
  return tmp - chunk->offset;
}


/// Malloc allocator

lida_Allocator malloc_allocator;

void*
MallocWrapper(lida_Allocator* a, uint32_t bytes)
{
  (void)a;
  return lida_Malloc(bytes);
}

void
FreeWrapper(lida_Allocator* a, void* ptr)
{
  (void)a;
  lida_MallocFree(ptr);
}

void*
ReallocWrapper(lida_Allocator* a, void* ptr, uint32_t bytes)
{
  (void)a;
  return lida_Realloc(ptr, bytes);
}

lida_Allocator*
lida_MallocAllocator()
{
  malloc_allocator.alloc = MallocWrapper;
  malloc_allocator.realloc = ReallocWrapper;
  malloc_allocator.free = FreeWrapper;
  return &malloc_allocator;
}

void*
lida_Malloc(uint32_t bytes)
{
  return malloc(bytes);
}

void
lida_MallocFree(void* ptr)
{
  free(ptr);
}

void*
lida_Realloc(void* ptr, uint32_t bytes)
{
  return realloc(ptr, bytes);
}
