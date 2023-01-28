#include "ecs.h"

typedef struct {
  float x;
  float y;
} Position;

typedef struct {
  int count;
  int flags;
} Health;

int
main(int argc, char** argv)
{
  lida_InitPlatformSpecificLoggers();

  lida_TypeInfo position_type_info = LIDA_TYPE_INFO(Position, lida_MallocAllocator(), 0);
  lida_TypeInfo health_type_info = LIDA_TYPE_INFO(Health, lida_MallocAllocator(), 0);

  lida_ECS* ecs = lida_ECS_Create(8, 8);
  lida_ComponentView* positions = lida_ECS_Components(ecs, &position_type_info);
  lida_ComponentView* healths = lida_ECS_Components(ecs, &health_type_info);
  LIDA_LOG_INFO("created ECS pool1=%p pool2=%p", positions, healths);

  {
    lida_ID entity1 = lida_CreateEntity(ecs);
    LIDA_LOG_INFO("created entity1=%u", entity1);
    Position* position1 = lida_ComponentAdd(positions, entity1);
    position1->x = 1.0f;
    position1->y = -2.0f;
    Health* health1 = lida_ComponentAdd(healths, entity1);
    health1->count = 10;
    health1->flags = 0;
    LIDA_LOG_TRACE("added components to it");
  }

  {
    lida_ID entity = lida_CreateEntity(ecs);
    LIDA_LOG_INFO("created entity2=%u", entity);
    Position* position = lida_ComponentAdd(positions, entity);
    position->x = 10.0f;
    position->y = -2.0f;
    Health* health = lida_ComponentAdd(healths, entity);
    health->count = 9;
    health->flags = 1;
    LIDA_LOG_TRACE("added components to it");
  }

  uint32_t count = lida_ComponentCount(positions);
  Position* ptr = lida_ComponentData(positions);
  lida_ID* entities = lida_ComponentIDs(positions);
  while (count--) {
    Health* health = lida_ComponentGet(healths, *entities);
    LIDA_LOG_INFO("id=%u; pos={.x=%f, .y=%f}; hp={%d, %d}",
                  *entities, ptr->x, ptr->y, health->count, health->flags);
    ptr++;
    entities++;
  }

  lida_ECS_Destroy(ecs);
  LIDA_LOG_INFO("success");
  return 0;
}
