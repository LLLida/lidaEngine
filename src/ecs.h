#pragma once

#include "base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t lida_ID;
typedef int(*lida_LessFunc)(const void* lhs, const void* rhs);

typedef struct lida_ECS lida_ECS;
typedef struct lida_ComponentView lida_ComponentView;

lida_ECS* lida_ECS_Create(uint32_t init_num_types, uint32_t init_num_entities);
void lida_ECS_Destroy(lida_ECS* ecs);
lida_ID lida_CreateEntity(lida_ECS* ecs);
void lida_DestroyEntity(lida_ECS* ecs, lida_ID entity);

lida_ComponentView* lida_ECS_Components(lida_ECS* ecs, const lida_TypeInfo* type);
void* lida_ComponentGet(lida_ComponentView* view, lida_ID entity);
void* lida_ComponentAdd(lida_ECS* ecs, lida_ComponentView* view, lida_ID entity);
void lida_ComponentRemove(lida_ECS* ecs, lida_ComponentView* view, lida_ID entity);
void lida_ComponentSort(lida_ComponentView* view, lida_LessFunc less);
void lida_ComponentClear(lida_ComponentView* view);
void lida_ComponentSetDestructor(lida_ComponentView* view, lida_DestructorFunction on_destroy);
uint32_t lida_ComponentCount(lida_ComponentView* view);
void* lida_ComponentData(lida_ComponentView* view);
lida_ID* lida_ComponentIDs(lida_ComponentView* view);

#define LIDA_COMPONENT_FOREACH(view, component, entity) uint32_t count__##__LINE__ = lida_ComponentCount(view); \
  component = lida_ComponentData(view);                    \
  entity = lida_ComponentIDs(view);\
  for (; count__##__LINE__--; component++, entity++)

#ifdef __cplusplus
}
#endif
