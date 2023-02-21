/*

  Entity component system.

 */

// entity ID
typedef uint32_t EID;

typedef struct {

  Allocation* sparse;
  // entities
  Allocation* dense;
  // components
  Allocation* packed;
  EID max_id;
  uint32_t capacity;
  uint32_t size;
  const Type_Info* type_info;

} Sparse_Set;

typedef struct {

  Allocator* allocator;
  Allocation* entities;
  Allocation* pools;
  uint32_t num_entities;
  uint32_t max_entities;
  uint32_t num_pools;
  uint32_t num_dead;
  uint32_t next_dead;

} ECS;

#define ENTITY_DEAD_MASK 0x8000
#define ENTITY_ALIVE_MASK (~ENTITY_DEAD_MASK)


/// internal functions

INTERNAL void
ClearSparseSet(Allocator* allocator, Sparse_Set* set)
{
  FreeAllocation(allocator, set->dense);
  FreeAllocation(allocator, set->packed);
  FreeAllocation(allocator, set->sparse);
  set->max_id = 0;
  set->capacity = 0;
  set->size = 0;
}

INTERNAL int
SetSparseSetMaxID(Allocator* allocator, Sparse_Set* set, EID id)
{
  if (id <= set->max_id)
    return 1;
  Allocation* old = set->sparse;
  set->sparse = ChangeAllocationSize(allocator, set->sparse, sizeof(uint32_t) * id);
  LOG_TRACE("%s: sparse=%p", set->type_info->name, set->sparse->ptr);
  if (set->sparse == NULL) {
    set->sparse = old;
    LOG_WARN("entity component system: out of memory");
    return -1;
  }
  set->max_id = id;
  return 0;
}

INTERNAL int
ReserveSparseSet(Allocator* allocator, Sparse_Set* set, uint32_t capacity)
{
  if (capacity <= set->size)
    return 1;
  Allocation* old = set->packed;
  set->packed = ChangeAllocationSize(allocator, set->packed, capacity * set->type_info->size);
  if (set->packed == NULL) {
    set->packed = old;
    return -1;
  }
  old = set->dense;
  set->dense = ChangeAllocationSize(allocator, set->dense, capacity * sizeof(EID));
  if (set->dense == NULL) {
    set->dense = old;
    LOG_WARN("entity component system: out of memory");
    return -1;
  }
  set->capacity = capacity;
  return 0;
}

INTERNAL void*
SearchSparseSet(Sparse_Set* set, EID entity)
{
  uint32_t* sparse = set->sparse->ptr;
  EID* dense = set->dense->ptr;
  if (entity < set->max_id &&
      sparse[entity] < set->size &&
      dense[sparse[entity]] == entity) {
    uint8_t* packed = set->packed->ptr;
    return packed + sparse[entity] * set->type_info->size;
  }
  return NULL;
}

INTERNAL void*
InsertToSparseSet(Allocator* allocator, Sparse_Set* set, EID entity)
{
  if (entity >= set->max_id) {
    SetSparseSetMaxID(allocator, set, entity+8);
  } else if (SearchSparseSet(set, entity)) {
    // sparse set already has this entity
    return NULL;
  }
  if (set->size == set->capacity) {
    // TODO: pick a better grow policy
    ReserveSparseSet(allocator, set, set->capacity+8);
  }
  uint32_t* sparse = set->sparse->ptr;
  EID* dense = set->dense->ptr;
  uint8_t* packed = set->packed->ptr;
  void* component = packed + set->size * set->type_info->size;
  dense[set->size] = entity;
  sparse[entity] = set->size;
  set->size++;
  return component;
}

INTERNAL int
EraseFromSparseSet(Sparse_Set* set, EID entity)
{
  if (SearchSparseSet(set, entity) == NULL) {
    return -1;
  }
  uint16_t component_size = set->type_info->size;
  uint32_t* sparse = set->sparse->ptr;
  EID* dense = set->dense->ptr;
  uint8_t* packed = set->packed->ptr;

  // move last element to this position
  void* dst = packed + sparse[entity] * component_size;
  void* src = packed + (set->size - 1) * component_size;
  EID end = dense[set->size - 1];
  dense[sparse[entity]] = end;
  sparse[end] = sparse[entity];
  memcpy(dst, src, component_size);
  set->size--;

  return 0;
}

// TODO: component sort

INTERNAL Sparse_Set*
GetSparseSet(ECS* ecs, const Type_Info* type_info)
{
  uint32_t id = type_info->type_hash & (ecs->num_pools-1);
  Sparse_Set* pools = ecs->pools->ptr;
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    Sparse_Set* set = &pools[id];
    if (set->type_info == NULL) {
      // insert
      *set = (Sparse_Set) {
        .type_info = type_info,
      };
      return set;
    } else if (set->type_info->type_hash == type_info->type_hash) {
      return set;
    }
    // ecs->num_pools is guaranteed to be power of 2
    id = (id+1) & (ecs->num_pools-1);
  }
  // reallocate pools
  Allocation* old = ecs->pools;
  uint32_t old_num = ecs->num_pools;
  // TODO: pick a better grow policy
  ecs->num_pools = ecs->num_pools * 2;
  ecs->pools = DoAllocation(ecs->allocator, ecs->num_pools);
  if (ecs->pools == NULL) {
    // out of memory
    ecs->num_pools = old_num;
    ecs->pools = old;
    return NULL;
  }
  Sparse_Set* old_pools = old->ptr;
  pools = ecs->pools->ptr;
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    pools[i].type_info = NULL;
  }
  // insert
  for (uint32_t i = 0; i < old_num; i++) {
    if (old_pools[i].type_info) {
      id = old_pools[i].type_info->type_hash & (ecs->num_pools-1);
      while (pools[id].type_info) {
        id = (id+1) & (ecs->num_pools - 1);
      }
      memcpy(&pools[id], &old_pools[i], sizeof(Sparse_Set));
    }
  }
  FreeAllocation(ecs->allocator, old);
  return GetSparseSet(ecs, type_info);
}


/// public functions

INTERNAL void
CreateECS(Allocator* allocator, ECS* ecs, uint32_t init_num_types, uint32_t init_num_entities)
{
  Assert(init_num_types > 0);
  Assert(init_num_entities > 0);
  ecs->num_dead = 0;
  ecs->num_entities = 0;
  ecs->num_pools = NearestPow2(init_num_types);
  ecs->max_entities = init_num_entities;
  ecs->entities = DoAllocation(allocator, init_num_entities * sizeof(EID));
  ecs->pools = DoAllocation(allocator, ecs->num_pools * sizeof(Sparse_Set));
  if (ecs->entities == NULL || ecs->pools == NULL) {
    LOG_FATAL("entity component system: out of memory at initialization");
    return;
  }
  Sparse_Set* pools = ecs->pools->ptr;
  for (uint32_t i = 0; i < ecs->num_pools; i++) {
    pools[i].type_info = NULL;
  }
  ecs->allocator = allocator;
}

INTERNAL void
DestroyECS(ECS* ecs)
{
  Sparse_Set* pools = ecs->pools->ptr;
  for (uint32_t i = 0; i < ecs->num_pools; i++)
    if (pools[i].type_info) {
      ClearSparseSet(ecs->allocator, &pools[i]);
    }
  FreeAllocation(ecs->allocator, ecs->pools);
  FreeAllocation(ecs->allocator, ecs->entities);
}

INTERNAL EID
CreateEntity(ECS* ecs)
{
  if (ecs->num_dead == 0) {
    if (ecs->num_entities == ecs->max_entities) {
      // TODO: pick a better grow policy
      ecs->entities = ChangeAllocationSize(ecs->allocator, ecs->entities, ecs->max_entities * 2 * sizeof(uint32_t));
      ecs->max_entities *= 2;
    }
    uint32_t* entities = ecs->entities->ptr;
    EID entity = ecs->num_entities;
    ecs->num_entities++;
    entities[entity] = 0;
    return entity;
  }
  uint32_t* entities = ecs->entities->ptr;
  EID entity = ecs->next_dead & ENTITY_ALIVE_MASK;
  ecs->next_dead = entities[entity];
  entities[entity] = 0;
  ecs->num_dead--;
  return entity;
}

INTERNAL void
DestroyEntity(ECS* ecs, EID entity)
{
  if (entity < ecs->max_entities) {
    LOG_WARN("entity component system: invalid entity");
    return;
  }
  uint32_t* entities = ecs->entities->ptr;
  Sparse_Set* pools = ecs->pools->ptr;
  for (uint32_t i = 0; entities[entity] > 0; i++) {
    Sparse_Set* set = &pools[i];
    if (EraseFromSparseSet(set, entity) == 0)
      entities[entity]--;
  }
  entities[entity] = ecs->next_dead;
  ecs->next_dead = entity | ENTITY_DEAD_MASK;
  ecs->num_dead++;
}

INTERNAL int
IsEntityValid(ECS* ecs, EID entity)
{
  uint32_t* entities = ecs->entities->ptr;
  return
    (entity < ecs->num_entities) &&
    ((entities[entity] & ENTITY_DEAD_MASK) == 0);
}

INTERNAL void*
GetComponent(ECS* ecs, EID entity, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  return SearchSparseSet(set, entity);
}

INTERNAL void*
AddComponent(ECS* ecs, EID entity, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  void* ret = InsertToSparseSet(ecs->allocator, set, entity);
  if (ret) {
    uint32_t* entities = ecs->entities->ptr;
    entities[entity]++;
  }
  return ret;
}

INTERNAL void
RemoveComponent(ECS* ecs, EID entity, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  if (EraseFromSparseSet(set, entity) == 0) {
    uint32_t* entities = ecs->entities->ptr;
    entities[entity]--;
  }
}

INTERNAL uint32_t
ComponentCount(ECS* ecs, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  return set->size;
}

INTERNAL void*
ComponentData(ECS* ecs, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  return set->packed->ptr;
}

INTERNAL EID*
ComponentIDs(ECS* ecs, const Type_Info* type)
{
  Sparse_Set* set = GetSparseSet(ecs, type);
  return set->dense->ptr;
}

// TODO: figure out how we can append __LINE__ to set's name so we can
// call multipli FOREACH_COMPONENT()s in 1 scope
#define FOREACH_COMPONENT(ecs, type, type_info) Sparse_Set* set = GetSparseSet(ecs, type_info); \
  type* components = set->packed->ptr;\
  EID* entities = set->dense->ptr;\
  (void)entities;\
  for (uint32_t i = 0; i < set->size; i++)

#define DECLARE_COMPONENT(type) GLOBAL Type_Info type_info_##type
#define REGISTER_COMPONENT(type, hash_func, compare_func) type_info_##type = TYPE_INFO(type, hash_func, compare_func)
