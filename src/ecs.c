#include "ecs.h"

typedef struct {

  uint32_t* sparse;
  lida_ID* dense;
  void* packed;
  lida_ID max_val;
  uint32_t capacity;
  uint32_t size;
  uint32_t elem_size;
  lida_ConstructorFunction on_create;
  lida_DestructorFunction on_destroy;

} SparseSet;

lida_TypeInfo g_entity_type;
lida_TypeInfo g_sparse_set_type;

int
lida_ECS_Create(lida_ECS* ecs)
{

}

void
lida_ECS_Destroy(lida_ECS* ecs)
{

}
