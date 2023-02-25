/*

  Entity component system.

  I think this is one of the fastest and easiest ECS in world. We
  highly utilize preprocessor capabilities to access components.

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
  if (old) {
    set->sparse = ChangeAllocationSize(allocator, set->sparse, sizeof(uint32_t) * id);
  } else {
    set->sparse = DoAllocation(allocator, sizeof(uint32_t)*id, set->type_info->name);
  }
  // LOG_TRACE("%s: sparse=%p", set->type_info->name, set->sparse->ptr);
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
  if (old) {
    set->packed = ChangeAllocationSize(allocator, set->packed, capacity * set->type_info->size);
  } else {
    set->packed = DoAllocation(allocator, capacity * set->type_info->size, set->type_info->name);
  }
  if (set->packed == NULL) {
    set->packed = old;
    return -1;
  }
  old = set->dense;
  if (old) {
    set->dense = ChangeAllocationSize(allocator, set->dense, capacity * sizeof(EID));
  } else {
    set->dense = DoAllocation(allocator, capacity * sizeof(EID), set->type_info->name);
  }
  if (set->dense == NULL) {
    set->dense = old;
    LOG_WARN("entity component system: out of memory");
    return -1;
  }
  set->capacity = capacity;
  return 0;
}

INTERNAL void*
SearchSparseSet(const Sparse_Set* set, EID entity)
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


/// public functions

INTERNAL void
CreateECS(Allocator* allocator, ECS* ecs, uint32_t init_num_entities)
{
  Assert(init_num_entities > 0);
  ecs->num_dead = 0;
  ecs->num_entities = 0;
  ecs->max_entities = init_num_entities;
  ecs->entities = DoAllocation(allocator, init_num_entities * sizeof(EID), "ecs-entities");
  if (ecs->entities == NULL) {
    LOG_FATAL("entity component system: out of memory at initialization");
    return;
  }
  ecs->allocator = allocator;
}

INTERNAL void
DestroyECS(ECS* ecs)
{
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
  if (entities[entity] > 0) {
    LOG_WARN("entity %u still has %u components, this is a memory leak",
             entity, entities[entity]);
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
AddComponent_ECS(ECS* ecs, Sparse_Set* set, EID entity)
{
  void* ret = InsertToSparseSet(ecs->allocator, set, entity);
  if (ret) {
    uint32_t* entities = ecs->entities->ptr;
    entities[entity]++;
  }
  return ret;
}

INTERNAL void
RemoveComponent_ECS(ECS* ecs, Sparse_Set* set, EID entity)
{
  if (EraseFromSparseSet(set, entity) == 0) {
    uint32_t* entities = ecs->entities->ptr;
    entities[entity]--;
  }
}

#define DECLARE_COMPONENT(type) DECLARE_TYPE(type); \
  GLOBAL Sparse_Set g_sparse_set_##type
#define REGISTER_COMPONENT(type) REGISTER_TYPE(type, NULL, NULL);    \
  g_sparse_set_##type .type_info = GET_TYPE_INFO(type)
#define UNREGISTER_COMPONENT(ecs, type) ClearSparseSet((ecs)->allocator, &g_sparse_set_##type)

#define GetComponent(type, entity) (type*)SearchSparseSet(&g_sparse_set_##type, entity)
#define AddComponent(ecs, type, entity) (type*)AddComponent_ECS(ecs, &g_sparse_set_##type, entity)
#define RemoveComponent(ecs, type, entity) RemoveComponent_ECS(ecs, &g_sparse_set_##type, entity)
#define ComponentCount(type) (g_sparse_set_##type .size)
#define ComponentData(type) (type*)(g_sparse_set_##type .packed->ptr)
#define ComponentIDs(type) (EID*)(g_sparse_set_##type .dense->ptr)

// TODO: figure out how we can append __LINE__ to set's name so we can
// call multipli FOREACH_COMPONENT()s in 1 scope
#define FOREACH_COMPONENT(type) type* components = ComponentData(type); \
  EID* entities = ComponentIDs(type);                                   \
  (void)entities;                                                       \
  for (uint32_t i = 0; i < ComponentCount(type); i++)
