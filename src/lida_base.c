/*
  lida_base.c
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
#define ALIGN_MASK(number, mask) (((number)+(mask))&~(mask))
#define ALIGN_TO(number, alignment) ALIGN_MASK(number, (alignment)-1)

typedef struct {
  void* ptr;
  size_t size;
  size_t left;
  size_t right;
} Memory_Chunk;

INTERNAL void
InitMemoryChunk(Memory_Chunk* chunk, void* ptr, size_t size)
{
  chunk->ptr = ptr;
  chunk->size = size;
  chunk->left = 0;
  chunk->right = size;
}

// allocate memory from the left of the chunk. Memory adress is aligned to 8 bytes.
// FIXME: this looks kinda ugly.. What if we need our memory to be aligned to 16? 64? 256?
// Should we introduce some kind of macro?
INTERNAL void*
MemoryAllocateLeft(Memory_Chunk* chunk, uint32_t size)
{
  Assert(chunk->left + size < chunk->size && "out of memory");
  uint8_t* start = (uint8_t*)chunk->ptr + chunk->left;
  void* ATTRIBUTE_ALIGNED(8) bytes = start + MEM_ALIGN_OFF(start, 8);
  chunk->left += size + MEM_ALIGN_OFF(start, 8);
  return bytes;
}

INTERNAL void*
MemoryAllocateRight(Memory_Chunk* chunk, uint32_t size)
{
  Assert(chunk->right >= chunk->left + size && "out of memory");
  chunk->right -= size + (chunk->right - size) % 8;
  void* ATTRIBUTE_ALIGNED(8) bytes = (uint8_t*)chunk->ptr + chunk->right;
  return bytes;
}

INTERNAL void
MemoryReleaseLeft(Memory_Chunk* chunk, void* ptr)
{
  Assert(ptr > chunk->ptr && (size_t)((uint8_t*)ptr - (uint8_t*)chunk->ptr) <= chunk->size);
  chunk->left = (uint8_t*)ptr - (uint8_t*)chunk->ptr;
}

INTERNAL void
MemoryReleaseRight(Memory_Chunk* chunk, uint32_t size)
{
  Assert(chunk->right + size < chunk->size);
  chunk->right += size;
}

INTERNAL void
MemoryChunkReset(Memory_Chunk* chunk)
{
  chunk->left = 0;
}

GLOBAL Memory_Chunk g_persistent_memory;

#define PersistentAllocate(size) MemoryAllocateLeft(&g_persistent_memory, size)
#define PersistentRelease(size) MemoryReleaseLeft(&g_persistent_memory, size)

typedef struct Allocation Allocation;

struct Allocation {

  // NOTE: never store this pointer; there's always possibility for
  // relocation and very bad things might happen.
  void* ptr;
  uint32_t size;
  // for internal usage
  // TODO: use uint32_t offsets to not waste space
  Allocation* left;
  Allocation* right;
  // TODO: debug information: function from where this allocation made, when it was made etc.

};

typedef struct {

  // NOTE: you might wonder why we have so many fields for that simple
  // allocator. That is because we might need to gather some
  // information for profiling, debug etc.
  void* ptr;
  uint32_t size;
  uint32_t effective_size;
  uint32_t offset;
  uint32_t num_allocations;
  uint32_t alloc_offset;
  Allocation* first_allocation;
  Allocation* last_allocation;
  Allocation* free_allocation;

} Allocator;

INTERNAL void
InitAllocator(Allocator* allocator, void* ptr, uint32_t size)
{
  allocator->ptr = ptr;
  allocator->effective_size = 0;
  allocator->size = size;
  allocator->offset = 0;
  allocator->num_allocations = 0;
  allocator->alloc_offset = size;
  allocator->first_allocation = NULL;
  allocator->last_allocation = NULL;
  allocator->free_allocation = NULL;
}

// check if we're not leaking memory.
// NOTE: this should be called when exiting application
INTERNAL int
ReleaseAllocator(Allocator* allocator)
{
  if (allocator->num_allocations != 0)
    return -1;
  return 0;
}

// shrink.
// number of saved bytes is returned.
INTERNAL uint32_t
FixFragmentation(Allocator* allocator)
{
  uint32_t counter = 0;
  for (Allocation* it = allocator->first_allocation; it; it = it->right) {
    uint32_t offset = (uint8_t*)it->ptr - (uint8_t*)allocator->ptr;
    if (offset > counter) {
      memmove((uint8_t*)allocator->ptr, it->ptr, it->size);
    }
    counter += it->size;
  }
  uint32_t old = allocator->offset - counter;
  allocator->offset = counter;
  return old;
}

// O(1) best case
// O(1) common case
// O(N) worst case, relocation happens
INTERNAL Allocation*
DoAllocation(Allocator* allocator, uint32_t size)
{
  Assert(size > 0);
  if (allocator->effective_size + size > allocator->alloc_offset) {
    // out of space
    return NULL;
  }
  void* ptr = (uint8_t*)allocator->ptr + allocator->offset;
  if (allocator->offset + size > allocator->alloc_offset) {
    FixFragmentation(allocator);
    // allocator->offset certainly changed, need to reassign ptr
    ptr = (uint8_t*)allocator->ptr + allocator->offset;
  }
  Allocation* ret;
  if (allocator->free_allocation == NULL) {
    ret = (Allocation*)((uint8_t*)allocator->ptr + allocator->alloc_offset - sizeof(Allocation));
    allocator->alloc_offset -= sizeof(Allocation);
  } else {
    // pop free allocation list
    ret = allocator->free_allocation;
    allocator->free_allocation = allocator->free_allocation->ptr;
  }
  ret->ptr = ptr;
  ret->size = size;
  ret->left = allocator->last_allocation;
  if (ret->left) {
    ret->left->right = ret;
  }
  ret->right = NULL;
  allocator->num_allocations++;
  allocator->effective_size += size;
  allocator->offset += size;
  allocator->last_allocation = ret;
  if (allocator->first_allocation == NULL) {
    allocator->first_allocation = ret;
  }
  return ret;
}

// O(1) always
INTERNAL void
FreeAllocation(Allocator* allocator, Allocation* allocation)
{
  // TODO: various checks that allocation is valid
  // important optimization: if last allocation is freed then we can
  // decrease stack cursor
  if (allocation == allocator->last_allocation) {
    allocator->offset -= allocation->size;
  }
  // remove from linked list
  if (allocation->left) {
    allocation->left->right = allocation->right;
  } else {
    allocator->first_allocation = allocation->right;
  }
  if (allocation->right) {
    allocation->right->left = allocation->left;
  } else {
    allocator->last_allocation = allocation->left;
  }
  if ((uint8_t*)allocation == (uint8_t*)allocator->ptr + allocator->alloc_offset) {
    // decrease size of allocation stack
    allocator->alloc_offset += sizeof(Allocation);
  } else {
    // add to linked list of free allocations
    allocation->ptr = allocator->free_allocation;
    allocator->free_allocation = allocation;
  }
  allocator->effective_size -= allocation->size;
  allocator->num_allocations--;
}

// behaves same as realloc()
INTERNAL Allocation*
ChangeAllocationSize(Allocator* allocator, Allocation* allocation, uint32_t new_size)
{
  Assert(new_size > 0);
  // TODO: checks for failures
  if (allocation == NULL) {
    return DoAllocation(allocator, new_size);
  }
  if (new_size <= allocation->size) {
    allocator->effective_size -= allocation->size - new_size;
    allocation->size = new_size;
    return allocation;
  }
  Allocation* next = allocation->right;
  if (next) {
    if ((uint8_t*)next->ptr - (uint8_t*)allocation->ptr >= new_size) {
      allocator->effective_size += new_size - allocation->size;
      allocation->size = new_size;
      return allocation;
    }
  } else {
    // allocation is at the end, we can grow it easily
    if (allocator->offset + new_size - allocation->size > allocator->alloc_offset) {
      FixFragmentation(allocator);
    }
    allocation->size = new_size;
    return allocation;
  }
  // worst case: make a new allocation
  Allocation* new_allocation = DoAllocation(allocator, new_size);
  memcpy(new_allocation->ptr, allocation->ptr, allocation->size);
  FreeAllocation(allocator, allocation);
  return new_allocation;
}


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

void ATTRIBUTE_PRINTF(4)
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


/// Type info

typedef uint32_t(*Hash_Function)(const void* obj);
typedef int(*Compare_Function)(const void* lhs, const void* rhs);

typedef struct {

  const char* name;
  uint64_t type_hash;
  uint16_t size;
  uint16_t alignment;
  Hash_Function hash;
  Compare_Function cmp;

} Type_Info;

#define TYPE_INFO(type, hash_func, cmp_func) (Type_Info) { .name = #type, .type_hash = HashString64(#type), .size = sizeof(type), .alignment = alignof(type), .hash = hash_func, .cmp = cmp_func }


/// Some useful algorithms

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

// compare two integers: -1, 0 or 1 is returned
#define COMPARE(lhs, rhs) ((lhs) > (rhs)) - ((lhs) < (rhs))

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

// swap contents of 2 memory buffers.
// this doesn't produce any allocations.
INTERNAL void
MemorySwap(void* lhs, void* rhs, size_t size)
{
  // size of buffer needs tweeking
  char buff[64];
  uint8_t* l = lhs, *r = rhs;
  while (size >= sizeof(buff)) {
    memcpy(buff, l, sizeof(buff));
    memcpy(l, r, sizeof(buff));
    memcpy(r, buff, sizeof(buff));
    size -= sizeof(buff);
    l += sizeof(buff);
    r += sizeof(buff);
  }
  if (size > 0) {
    memcpy(buff, l, size);
    memcpy(l, r, size);
    memcpy(r, buff, size);
  }
}

// Simple old quick sort
INTERNAL void
QuickSortHelper(void* ptr, size_t size, size_t left, size_t right, Compare_Function cmp)
{
  // Not gonna lie, I stole this code from
  // https://www.geeksforgeeks.org/generic-implementation-of-quicksort-algorithm-in-c/
  void *vt, *v3;
  size_t i, last, mid = (left + right) / 2;
  if (left >= right)
    return;
  void* vl = (uint8_t*)ptr + (left * size);
  void* vr = (uint8_t*)ptr + (mid * size);
  MemorySwap(vl, vr, size);
  last = left;

  for (i = left + 1; i <= right; i++) {
    vt = (uint8_t*)ptr + (i * size);
    if (cmp(vl, vt) > 0) {
      ++last;
      v3 = (uint8_t*)ptr + (last * size);
      MemorySwap(vt, v3, size);
    }
  }

  v3 = (uint8_t*)ptr + (last * size);
  MemorySwap(vl, v3, size);
  if (last > 0)
    QuickSortHelper(ptr, size, left, last - 1, cmp);
  QuickSortHelper(ptr, size, last + 1, right, cmp);
}

INTERNAL void
QuickSort(void* ptr, size_t num, size_t sizeof_, Compare_Function cmp)
{
  if (num > 0) {
    QuickSortHelper(ptr, sizeof_, 0, num-1, cmp);
  }
}

INTERNAL void
QuickSort2(void* ptr, size_t num, const Type_Info* type)
{
  QuickSortHelper(ptr, type->size, 0, num-1, type->cmp);
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


/// Hash tables

typedef struct {
/* ptr is contigous array of cells.
Each cell has following fields:
1. element - value which is stored;
2. hash - precomputed hash of element;
3. psl - counter for robin-hood hashing.
 */
  void* ptr;
  size_t max;
  size_t size;
} Fixed_Hash_Table;

// TODO: respect alignment
#define FHT_CALC_SIZE(type, n) (((type)->size + sizeof(uint32_t) + sizeof(uint32_t)) * n)
#define FHT_ELEM_SIZE(type) ((type)->size + sizeof(uint32_t) + sizeof(uint32_t))
#define FHT_GET(ht, type, i) ((uint8_t*)(ht)->ptr + FHT_ELEM_SIZE(type)*i)
#define FHT_GET_HASH(ht, type, i) (uint32_t*)(FHT_GET(ht, type, i) + type->size)
#define FHT_GET_PSL(ht, type, i) (uint32_t*)(FHT_GET(ht, type, i) + type->size + sizeof(uint32_t))
#define FHT_VALID(ht, type, i) (*FHT_GET_PSL(ht, type, i) != UINT32_MAX)

INTERNAL void
FHT_Init(Fixed_Hash_Table* ht, void* ptr, size_t max_elements, const Type_Info* type)
{
  ht->ptr = ptr;
  ht->max = NearestPow2(max_elements);
  ht->size = 0;
  for (size_t i = 0; i < ht->max; i++) {
    *FHT_GET_PSL(ht, type, i) = UINT32_MAX;
  }
}

// NOTE: because of how robin-hood hashing works, contents of elem
// become invalidated after call to this function
INTERNAL void*
FHT_Insert(Fixed_Hash_Table* ht, const Type_Info* type, void* elem)
{
  uint32_t temp_hash = type->hash(elem);
  uint32_t temp_psl = 0;
  size_t id = temp_hash & (ht->max-1);
  // find first invalid pos
  while (FHT_VALID(ht, type, id)) {
    void* curr = FHT_GET(ht, type, id);
    uint32_t* curr_psl = FHT_GET_PSL(ht, type, id);
    uint32_t* curr_hash = FHT_GET_HASH(ht, type, id);
    if (type->cmp(elem, curr) == 0) {
      // we already have this value
      return NULL;
    }
    if (temp_psl > *curr_psl) {
      // do swap
      MemorySwap(elem, curr, type->size);
      MemorySwap(&temp_psl, curr_psl, sizeof(uint32_t));
      MemorySwap(&temp_hash, curr_hash, sizeof(uint32_t));
      temp_psl = *curr_psl+1;
    } else {
      temp_psl++;
    }
    id = (id+1)&(ht->max-1);
  }
  // insert element
  void* ret = FHT_GET(ht, type, id);
  memcpy(ret, elem, type->size);
  *FHT_GET_PSL(ht, type, id) = temp_psl;
  *FHT_GET_HASH(ht, type, id) = temp_hash;
  // increment size counter
  ht->size++;
  return ret;
}

INTERNAL void*
FHT_Search(Fixed_Hash_Table* ht, const Type_Info* type, const void* elem)
{
  if (ht->size > 0) {
    uint32_t hash = type->hash(elem);
    uint32_t psl = 0;
    size_t id = hash & (ht->max-1);
    while (1) {
      void* curr = FHT_GET(ht, type, id);
      if (!FHT_VALID(ht, type, id)) {
        return NULL;
      }
      if (hash == *FHT_GET_HASH(ht, type, id) &&
          type->cmp(curr, elem) == 0) {
        return curr;
      }
      if (psl > *FHT_GET_PSL(ht, type, id)) {
        return NULL;
      }
      id = (id+1) & (ht->max-1);
      psl++;
    }
    // unreachable
  }
  return NULL;
}

// NOTE: this returns pointer to element which sits in hash table. It
// means that it must be copied immediately if further use needed.
INTERNAL void*
FHT_Remove(Fixed_Hash_Table* ht, const Type_Info* type, const void* elem)
{
  void* curr = NULL;
  if (ht->size > 0) {
    uint32_t hash = type->hash(elem);
    uint32_t psl = 0;
    uint32_t id = hash & (ht->max-1);
    while (1) {
      curr = FHT_GET(ht, type, id);
      if (!FHT_VALID(ht, type, id)) {
        return NULL;
      }
      if (hash == *FHT_GET_HASH(ht, type, id) &&
          type->cmp(curr, elem) == 0) {
        break;
      }
      if (psl > *FHT_GET_PSL(ht, type, id)) {
        // 'elem' wasn't inserted in table, return
        return NULL;
      }
      id = (id+1) & (ht->max-1);
      psl++;
    }
    // invalidate
    *FHT_GET_PSL(ht, type, id) = UINT32_MAX;
    ht->size--;
  }
  return curr;
}

// iterates over fixed hash table
typedef struct {

  const Fixed_Hash_Table* ht;
  const Type_Info* type;
  size_t id;
  size_t remaining;

} FHT_Iterator;

INTERNAL void
FHT_IteratorBegin(const Fixed_Hash_Table* ht, const Type_Info* type, FHT_Iterator* it)
{
  it->ht = ht;
  it->type = type;
  it->id = 0;
  it->remaining = ht->size;

  if (ht->size > 0) {
    // we assume that hash table data is not corrupted, otherwise we
    // will run into some very bad stuff...
    while (!FHT_VALID(ht, type, it->id)) {
      it->id++;
    }
  }
}

INTERNAL int
FHT_IteratorEmpty(FHT_Iterator* it)
{
  return it->remaining == 0;
}

INTERNAL void
FHT_IteratorNext(FHT_Iterator* it)
{
  it->id++;
  while (it->id < it->ht->max &&
         !FHT_VALID(it->ht, it->type, it->id)) {
    it->id++;
  }
  it->remaining--;
}

INTERNAL void*
FHT_IteratorGet(FHT_Iterator* it)
{
  return FHT_GET(it->ht, it->type, it->id);
}

// Usage example:
/*
  FHT_Iterator it;
  FHT_FOREACH(&hashtable, &type_info, &it) {
    Type* value = FHT_IteratorGet(&it);
    print(value.name);
  }
*/
// TODO: see generated assembly and optimise
#define FHT_FOREACH(ht, type, it) for (FHT_IteratorBegin(ht, type, it); !FHT_IteratorEmpty(it); FHT_IteratorNext(it))


/// Random number generator
// based on PCG: https://www.pcg-random.org/download.html

typedef struct {

  uint64_t state;
  uint64_t inc;

} Random_State;

INTERNAL uint32_t
Random(Random_State* rng)
{
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
  uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

INTERNAL void
SeedRandom(Random_State* rng, uint64_t initstate, uint64_t initseq)
{
  rng->state = 0U;
  rng->inc = (initseq << 1u) | 1u;
  Random(rng);
  rng->state += initstate;
  Random(rng);
}

// TODO: various convenience functions: generate range of random
// numbers, generate a uniformly distributed number, generate float
// number etc.
