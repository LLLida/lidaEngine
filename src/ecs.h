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


/// C++ wrapper

#ifdef __cplusplus

#include <utility> // for std::pair

namespace lida {

  // This looks so ugly... maybe we should use only C API?
  template<typename T>
  struct ComponentView {

    lida_ComponentView* raw;

    inline T* get(lida_ID entity) {
      return (T*)lida_ComponentGet(raw, entity);
    }

    inline T* add(lida_ECS* ecs, lida_ID entity) {
      return (T*)lida_ComponentAdd(ecs, raw, entity);
    }

    inline void remove(lida_ECS* ecs, lida_ID entity) {
      lida_ComponentRemove(ecs, raw, entity);
    }

    inline void sort(lida_LessFunc less) {
      lida_ComponentSort(raw, less);
    }

    inline void clear() {
      lida_ComponentClear(raw);
    }

    inline uint32_t count() {
      return lida_ComponentCount(raw);
    }

    inline T* data() {
      return (T*)lida_ComponentData(raw);
    }

    inline lida_ID* ids() {
      return lida_ComponentIDs(raw);
    }

    struct Iterator {
      lida_ID* ids;
      T* components;

      std::pair<lida_ID, T*> operator*() {
        return { *ids, components };
      }

      Iterator& operator++ () {
        components++;
        ids++;
        return *this;
      }
      
      Iterator operator++(int) {
        Iterator tmp = *this;
        ++(*this);
        return tmp;
      }
      
      bool operator==(const Iterator& lhs) const {
        return components == lhs.components && ids == lhs.ids;
      }

      bool operator!=(const Iterator& lhs) const {
        return !(*this == lhs);
      }

    };

    inline Iterator begin() {
      return { ids(), data() };
    }

    inline Iterator end() {
      return { ids() + count(), data() + count() };
    }

    inline void set_destructor(lida_DestructorFunction ds) {
      lida_ComponentSetDestructor(raw, ds);
    }

  };

  template<typename T>
  ComponentView<T> components(lida_ECS* ecs, const lida_TypeInfo* type_info) {
    ComponentView<T> ret;
    ret.raw = lida_ECS_Components(ecs, type_info);
    return ret;
  }

}

#endif

// Local Variables:
// mode: c++
// End:
