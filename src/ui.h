#pragma once

#include "lib/volk.h"

#ifdef __cplusplus
extern "C" {
#endif

void lida_Init_ImGui();
void lida_Free_ImGui();

int lida_UI_NewFrame();
void lida_UI_Prepare(VkCommandBuffer cmd);
void lida_UI_Render(VkCommandBuffer cmd);

#ifdef __cplusplus
}
#endif
