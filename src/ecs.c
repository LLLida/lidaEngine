#include "ecs.h"

#include <string.h>


/// Declarations

typedef struct {

  uint32_t* sparse;
  lida_ID* dense;
  void* packed;
  lida_ID max_id;
  uint32_t capacity;
  uint32_t size;
  const lida_TypeInfo* type_info;
  lida_ConstructorFunction on_create;
  lida_DestructorFunction on_destroy;

} SparseSet;

struct lida_ECS {

  lida_DynArray entities;
  SparseSet* pools;
  uint32_t num_pools;
  uint32_t num_dead;
  uint32_t next_dead;
#define DEAD_MASK 0x8000
#define ALIVE_MASK (~DEAD_MASK)

};

lida_TypeInfo g_entity_type;
lida_TypeInfo g_sparse_set_type;

static void SparseSetClear(SparseSet* set);
static int SparseSetSetMaxID(SparseSet* set, lida_ID id);
static int SparseSetReserve(SparseSet* set, uint32_t capacity);
static void* SparseSetSearch(SparseSet* set, lida_ID entity);
static void* SparseSetInsert(SparseSet* set, lida_ID entity);
static int SparseSetErase(SparseSet* set, lida_ID entity);
static uint32_t SparseSetSort(SparseSet* set, lida_LessFunc less);
static SparseSet* GetSparseSet(lida_ECS* ecs, const lida_TypeInfo* type_info);



lida_ECS*
lida_ECS_Create(uint32_t init_num_types, uint32_t init_num_entities)
{
  lida_ECS* ecs = lida_Malloc(sizeof(lida_ECS));
  g_entity_type = LIDA_TYPE_INFO(lida_ID, lida_MallocAllocator(), 0);
  ecs->num_pools = init_num_types;
  ecs->entities = LIDA_DA_EMPTY(&g_entity_type);
  ecs->pools = lida_Malloc(ecs->num_pools * sizeof(SparseSet));
  lida_DynArrayReserve(&ecs->entities, init_num_entities);
  return ecs;
}

void
lida_ECS_Destroy(lida_ECS* ecs)
{
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    if (ecs->pools[i].type_info) {
      SparseSetClear(&ecs->pools[i]);
    }
  }
  lida_MallocFree(ecs->pools);
  lida_DynArrayClear(&ecs->entities);
  lida_MallocFree(ecs);
}

lida_ID
lida_CreateEntity(lida_ECS* ecs)
{
  uint32_t* entities = (uint32_t*)ecs->entities.ptr;
  if (ecs->num_dead == 0) {
    lida_ID entity = ecs->entities.size;
    *(uint32_t*)lida_DynArrayPushBack(&ecs->entities) = 0;
    return entity;
  }
  lida_ID entity = ecs->next_dead & ALIVE_MASK;
  ecs->next_dead = entities[entity];
  entities[entity] = 0;
  ecs->num_dead--;
  return entity;
}

void
lida_DestroyEntity(lida_ECS* ecs, lida_ID entity)
{
  if (entity < ecs->entities.size) {
    LIDA_LOG_WARN("entity doesn't belong to this storage");
    return;
  }
  uint32_t* entities = (uint32_t*)ecs->entities.ptr;
  for (uint32_t i = 0; entities[entity] > 0; i++) {
    SparseSet* set = &ecs->pools[i];
    if (SparseSetErase(set, entity) == 0)
      entities[entity]--;
  }
  entities[entity] = ecs->next_dead;
  ecs->next_dead = entity | DEAD_MASK;
  ecs->num_dead++;
}

lida_ComponentView*
lida_ECS_Components(lida_ECS* ecs, const lida_TypeInfo* type)
{
  return (lida_ComponentView*)GetSparseSet(ecs, type);
}

void*
lida_ComponentGet(lida_ComponentView* view, lida_ID entity)
{
  return SparseSetSearch((SparseSet*)view, entity);
}

void*
lida_ComponentAdd(lida_ComponentView* view, lida_ID entity)
{
  return SparseSetInsert((SparseSet*)view, entity);
}

void
lida_ComponentRemove(lida_ComponentView* view, lida_ID entity)
{
  SparseSetErase((SparseSet*)view, entity);
}

void
lida_ComponentSort(lida_ComponentView* view, lida_LessFunc less)
{
  SparseSetSort((SparseSet*)view, less);
}

void
lida_ComponentClear(lida_ComponentView* view)
{
  SparseSetClear((SparseSet*)view);
}

uint32_t
lida_ComponentCount(lida_ComponentView* view)
{
  SparseSet* set = (SparseSet*)view;
  return set->size;
}

void*
lida_ComponentData(lida_ComponentView* view)
{
  SparseSet* set = (SparseSet*)view;
  return set->packed;
}

lida_ID*
lida_ComponentIDs(lida_ComponentView* view)
{
  SparseSet* set = (SparseSet*)view;
  return set->dense;
}



void
SparseSetClear(SparseSet* set)
{
  if (set->on_destroy) {
    char* ptr = set->packed;
    for (uint32_t i = 0; i < set->size; i++)
      set->on_destroy(ptr + i * set->type_info->elem_size);
  }
  lida_Allocator* allocator = set->type_info->allocator;
  lida_Free(allocator, set->packed);
  lida_Free(allocator, set->dense);
  lida_Free(allocator, set->sparse);
  set->max_id = 0;
  set->capacity = 0;
  set->size = 0;
}

int
SparseSetSetMaxID(SparseSet* set, lida_ID id)
{
  if (id <= set->max_id)
    return 1;
  lida_Allocator* allocator = set->type_info->allocator;
  uint32_t* old_sparse = set->sparse;
  set->sparse = lida_Reallocate(allocator, set->sparse, sizeof(uint32_t) * id);
  if (set->sparse == NULL) {
    set->sparse = old_sparse;
    return -1;
  }
  memset(set->sparse + set->max_id, -1, sizeof(lida_ID) * (id - set->max_id));
  set->max_id = id;
  return 0;
}

int
SparseSetReserve(SparseSet* set, uint32_t capacity)
{
  void* old_packed = set->packed;
  lida_Allocator* allocator = set->type_info->allocator;
  set->packed = lida_Reallocate(allocator, set->packed, capacity * set->type_info->elem_size);
  if (set->packed == NULL) {
    set->packed = old_packed;
    return -1;
  }
  lida_ID* old_dense = set->dense;
  set->dense = lida_Reallocate(allocator, set->dense, capacity * sizeof(lida_ID));
  if (set->dense == NULL) {
    set->dense = old_dense;
    return -1;
  }
  set->capacity = capacity;
  return 0;
}

void*
SparseSetSearch(SparseSet* set, lida_ID entity)
{
  if (entity < set->max_id &&
      set->sparse[entity] < set->size &&
      set->dense[set->sparse[entity]] == entity) {
    return (char*)set->packed + set->sparse[entity] * set->type_info->elem_size;
  }
  return NULL;
}

void*
SparseSetInsert(SparseSet* set, lida_ID entity)
{
  if (entity >= set->max_id) {
    SparseSetSetMaxID(set, (entity+1) * 3 / 2);
  } else if (SparseSetSearch(set, entity)) {
    return NULL;
  }
  if (set->size == set->capacity) {
    SparseSetReserve(set, (set->capacity+1) * 3 / 2);
  }
  void* component = (char*)set->packed + set->size * set->type_info->elem_size;
  if (set->on_create) {
    set->on_create(component);
  }
  set->dense[set->size] = entity;
  set->sparse[entity] = set->size;
  set->size++;
  return component;
}

int
SparseSetErase(SparseSet* set, lida_ID entity)
{
  if (SparseSetSearch(set, entity) == NULL) {
    return -1;
  }
  uint16_t component_size = set->type_info->elem_size;
  void* dst = (char*)set->packed + set->sparse[entity] * component_size;
  void* src = (char*)set->packed + (set->size - 1) * component_size;
  lida_ID end = set->dense[set->size - 1];
  set->dense[set->sparse[entity]] = end;
  set->sparse[end] = set->sparse[entity];
  // call destructor
  if (set->on_destroy) {
    set->on_destroy(dst);
  }
  memcpy(dst, src, component_size);
  set->size--;
  return 0;
}

uint32_t
SparseSetSort(SparseSet* set, lida_LessFunc less)
{
  // https://skypjack.github.io/2019-09-25-ecs-baf-part-5/
  char* packed = set->packed;
  uint32_t* sparse = set->sparse;
  lida_ID* dense = set->dense;
  uint16_t type_size = set->type_info->elem_size;
  // insertion sort
  for (uint32_t i = 0; i < set->size; i++) {
    lida_ID key = dense[i];
    uint32_t j = i;
    while (j > 0 && less(packed + i*type_size, packed + (j-1)*type_size)) {
      dense[j] = dense[j-1];
      j--;
    }
    dense[j] = key;
  }
  // do swaps
  uint32_t num_swaps = 0;
  void* buffer = lida_TempAllocate(type_size);
  for (uint32_t pos = 0; pos < set->size; pos++) {
    lida_ID entity = dense[pos];
    uint32_t curr = pos;
    uint32_t next = sparse[entity];
    while (curr != next) {
      uint32_t lhs = sparse[dense[curr]];
      uint32_t rhs = sparse[dense[next]];
      memcpy(buffer, packed + lhs*type_size, type_size);
      memcpy(packed + lhs*type_size, packed + rhs*type_size, type_size);
      memcpy(packed + rhs*type_size, buffer, type_size);
      num_swaps++;
      sparse[dense[curr]] = curr;
      curr = next;
      next = sparse[dense[curr]];
    }
  }
  lida_TempFree(buffer);
  return num_swaps;
}

SparseSet*
GetSparseSet(lida_ECS* ecs, const lida_TypeInfo* type_info)
{
  uint32_t id = type_info->type_hash % ecs->num_pools;
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    SparseSet* set = &ecs->pools[id];
    if (set->type_info == NULL) {
      // insert
      *set = (SparseSet) {
        .type_info = type_info,
      };
      return set;
    } else if (set->type_info->type_hash == type_info->type_hash) {
      return set;
    }
    id = (id+1) % ecs->num_pools;
  }
  // reallocate pools
  SparseSet* temp = ecs->pools;
  uint32_t old_num = ecs->num_pools;
  ecs->num_pools = 3 * (ecs->num_pools+1) / 2;
  ecs->pools = lida_Malloc(ecs->num_pools);
  if (ecs->pools == NULL) {
    // out of memory
    ecs->num_pools = old_num;
    ecs->pools = temp;
    return NULL;
  }
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    ecs->pools[i].type_info = NULL;
  }
  // insert
  for (uint32_t i = 0; i < old_num; i++) {
    if (temp[i].type_info) {
      id = temp[i].type_info->type_hash % ecs->num_pools;
      while (ecs->pools[id].type_info) {
        id = (id+1) % ecs->num_pools;
      }
      memcpy(&ecs->pools[id], &temp[i], sizeof(SparseSet));
    }
  }
  lida_MallocFree(temp);
  return GetSparseSet(ecs, type_info);
}
