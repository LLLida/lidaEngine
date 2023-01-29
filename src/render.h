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

VkResult lida_ForwardPassCreate(uint32_t width, uint32_t height, VkSampleCountFlagBits samples);
void lida_ForwardPassDestroy();

lida_SceneDataStruct* lida_ForwardPassGetSceneData();
VkDescriptorSet lida_ForwardPassGetDS0();
VkDescriptorSet lida_ForwardPassGetDS1();
VkRenderPass lida_ForwardPassGetRenderPass();
VkSampleCountFlagBits lida_ForwardPassGet_MSAA_Samples();
void lida_ForwardPassSendData();
void lida_ForwardPassBegin(VkCommandBuffer cmd, float clear_color[4]);
void lida_ForwardPassResize(uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
