#pragma once

#include "linalg.h"
#include "volk.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct {
  lida_Mat4 camera_projview;
  lida_Mat4 camera_projection;
  lida_Mat4 camera_view;
  lida_Mat4 camera_invproj;
} lida_SceneDataStruct;

VkResult lida_ForwardPassCreate(uint32_t width, uint32_t height);
void lida_ForwardPassDestroy();

lida_SceneDataStruct* lida_ForwardPassGetSceneData();
VkDescriptorSet lida_ForwardPassGetDS0();
void lida_ForwardPassSendData();

#ifdef __cplusplus
}
#endif
