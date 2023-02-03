#pragma once

#include "linalg.h"
#include "lib/volk.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct {
  lida_Mat4 camera_projview;
  lida_Mat4 camera_projection;
  lida_Mat4 camera_view;
  lida_Mat4 camera_invproj;
  lida_Mat4 light_space;
  lida_Vec3 sun_dir;
  float sun_ambient;
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

VkResult lida_ShadowPassCreate(uint32_t width, uint32_t height);
void lida_ShadowPassDestroy();
VkRenderPass lida_ShadowPassGetRenderPass();
VkDescriptorSet lida_ShadowPassGetDS0();
VkDescriptorSet lida_ShadowPassGetDS1();
void lida_ShadowPassBegin(VkCommandBuffer cmd);
void lida_ShadowPassViewport(VkViewport** viewport, VkRect2D** scissor);

#ifdef __cplusplus
}
#endif
