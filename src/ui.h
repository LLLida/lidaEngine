#pragma once

#include "linalg.h"

#include "lib/volk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lida_FontAtlas lida_FontAtlas;

void lida_Init_ImGui();
void lida_Free_ImGui();

int lida_UI_NewFrame();
void lida_UI_Prepare(VkCommandBuffer cmd, lida_FontAtlas* atlas);
void lida_UI_Render(VkCommandBuffer cmd);

lida_FontAtlas* lida_FontAtlasCreate(uint32_t width, uint32_t height);
void lida_FontAtlasDestroy(lida_FontAtlas* atlas);
uint32_t lida_FontAtlasLoad(lida_FontAtlas* atlas, VkCommandBuffer cmd, const char* font_name, uint32_t pixel_size);
void lida_FontAtlasResetFonts(lida_FontAtlas* atlas);
uint32_t lida_FontAtlasAddText(lida_FontAtlas* atlas, const char* text, uint32_t font_id, const lida_Vec2* size, const lida_Vec4* color, const lida_Vec2* pos);
void lida_FontAtlasTextDraw(lida_FontAtlas* atlas, VkCommandBuffer cmd, uint32_t num_vertices);

#ifdef __cplusplus
}
#endif
