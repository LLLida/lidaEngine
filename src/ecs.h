#pragma once

#include "base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t lida_ID;

typedef struct {

  lida_DynArray entities;
  lida_HashTable pools;
  uint32_t num_dead;
  uint32_t next_dead;

} lida_ECS;

int lida_ECS_Create(lida_ECS* ecs);
void lida_ECS_Destroy(lida_ECS* ecs);

#ifdef __cplusplus
}
#endif
