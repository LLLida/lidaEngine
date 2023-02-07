#include "ui.h"

#include "base.h"
#include "device.h"
#include "linalg.h"
#include "memory.h"
#include "window.h"

#include "lib/imgui.h"
#include "lib/imgui_impl_sdl.h"
#include "lib/imgui_impl_vulkan.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "lib/stb_rect_pack.h"

#include <ft2build.h>
#include FT_FREETYPE_H

static ImGuiContext* im_context;
static FT_Library freetype;

typedef struct {
  lida_Vec2 pos;
  lida_Vec2 uv;
  lida_Vec4 color;
} TextureVertex;

typedef struct
{
  FT_Vector advance;
  lida_iVec2 bearing;
  uint32_t width;
  uint32_t height;
  lida_Vec2 offset;
  lida_Vec2 size;
} Glyph;

typedef struct {
  Glyph glyphs[128];
  uint32_t pixel_size;
} Font;

struct lida_FontAtlas {
  lida_VideoMemory gpu_memory;
  lida_VideoMemory cpu_memory;
  VkBuffer vertex_buffer;
  VkImage image;
  VkImageView image_view;
  VkExtent2D extent;
  VkDescriptorSet descriptor_set;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline;
  uint32_t max_vertices;
  TextureVertex* vertices_mapped;
  uint32_t lines;
  Font fonts[4];
};

static void LoadVertex(Font* font, const lida_Vec2* pos, const lida_Vec2* size, const lida_Vec4* color, char c, TextureVertex* vertices);
static int LoadFace(FT_Face* face, const char* name);



void
lida_Init_ImGui()
{
  im_context = ImGui::CreateContext();
  ImGui::SetCurrentContext(im_context);
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = lida_GetVulkanInstance();
  init_info.PhysicalDevice = lida_GetPhysicalDevice();
  init_info.Device = lida_GetLogicalDevice();
  init_info.QueueFamily = lida_GetGraphicsQueueFamily();
  init_info.Queue = lida_GetGraphicsQueue();
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = lida_GetDescriptorPool();
  init_info.Subpass = 0;
  init_info.MinImageCount = 2;
  init_info.ImageCount = lida_WindowGetNumImages();
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = NULL;
  ImGui_ImplSDL2_InitForVulkan(lida_WindowGet_SDL_Handle());
  ImGui_ImplVulkan_Init(&init_info, lida_WindowGetRenderPass());
  auto io = &ImGui::GetIO();
  io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io->Fonts->AddFontDefault();
}

void
lida_Free_ImGui()
{
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}

int
lida_UI_NewFrame()
{
  if (lida_WindowGetFrameNo() == 0) {
    return -1;
  }
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  return 0;
}

void
lida_UI_Prepare(VkCommandBuffer cmd, lida_FontAtlas* atlas)
{
  if (lida_WindowGetFrameNo() == 0) {
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    // lida_FontAtlasLoad(atlas, cmd, "../assets/Serat.ttf", 32);
    // lida_FontAtlasLoad(atlas, cmd, "../assets/pixel1.ttf", 32);
    lida_FontAtlasLoad(atlas, cmd, "../assets/arial.ttf", 32);
  } else if (lida_WindowGetFrameNo() == 2) {
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }
}

void
lida_UI_Render(VkCommandBuffer cmd)
{
  if (lida_WindowGetFrameNo() > 0) {
    auto draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
  }
}

lida_FontAtlas*
lida_FontAtlasCreate(uint32_t width, uint32_t height)
{
  if (freetype == NULL) {
    FT_Error error = FT_Init_FreeType(&freetype);
    if (error != 0) {
      LIDA_LOG_ERROR("failed to init freetype library with error '%s'",
                     FT_Error_String(error));
    }
  }
  lida_FontAtlas* atlas = (lida_FontAtlas*)lida_Malloc(sizeof(lida_FontAtlas));
  {
    atlas->extent = { width, height };
    atlas->lines = 0;
    // NOTE: 4 megabytes may be too much for UI and fonts
    VkDeviceSize font_bytes = 4 * 1024 * 1024;
    VkResult err = lida_VideoMemoryAllocate(&atlas->gpu_memory, font_bytes, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, UINT32_MAX,
                                            "font/main-memory");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate memory for fonts with error '%s'", lida_VkResultToString(err));
      goto error;
    }
    // create image
    VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = {width, height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    err = lida_ImageCreate(&atlas->image, &image_info, "font/atlas-image");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create font atlas image with error '%s'", lida_VkResultToString(err));
      goto error;
    }
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(lida_GetLogicalDevice(), atlas->image, &image_requirements);
    err = lida_ImageBindToMemory(&atlas->gpu_memory, atlas->image,
                                 &image_requirements);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to bind font atlas image to memory with error '%s'",
                     lida_VkResultToString(err));
      goto error;
    }
    VkImageViewCreateInfo image_view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = atlas->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = image_info.format,
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    err = lida_ImageViewCreate(&atlas->image_view, &image_view_info, "font/atlas-image-view");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create image view with error %s", lida_VkResultToString(err));
      goto error;
    }
    // create vertex buffer
    atlas->max_vertices = 64 * 1024;
    err = lida_BufferCreate(&atlas->vertex_buffer, atlas->max_vertices * sizeof(TextureVertex),
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            "font/vertex-staging-buffer");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create buffer vertex buffer with error '%s'", lida_VkResultToString(err));
      goto error;
    }
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(lida_GetLogicalDevice(), atlas->vertex_buffer, &requirements);
    err = lida_VideoMemoryAllocate(&atlas->cpu_memory, requirements.size,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, requirements.memoryTypeBits,
                                   "font/staging-memory");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate memory for vertex buffer with error '%s'",
                     lida_VkResultToString(err));
      goto error;
    }
    // bind vertex buffer to memory
    err = lida_BufferBindToMemory(&atlas->cpu_memory, atlas->vertex_buffer, &requirements,
                                  (void**)&atlas->vertices_mapped, NULL);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to bind vertex buffer to memory with error '%s'",
                     lida_VkResultToString(err));
      goto error;
    }
    // allocate descriptor set
    VkDescriptorImageInfo ds_image_info = {
      .sampler = lida_GetSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      .imageView = atlas->image_view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    lida_DescriptorBindingInfo binding = {
      .binding = 0,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .shader_stages = VK_SHADER_STAGE_FRAGMENT_BIT,
      .data = { .image = ds_image_info }
    };
    err = lida_AllocateAndUpdateDescriptorSet(&binding, 1, &atlas->descriptor_set, 0, "font/descriptor-set");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate descriptor set with error '%s'",
                     lida_VkResultToString(err));
      goto error;
    }
    // create pipeline
    VkVertexInputBindingDescription input_binding = { 0, sizeof(TextureVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription input_attributes[3];
    input_attributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TextureVertex, pos) };
    input_attributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TextureVertex, uv) };
    input_attributes[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextureVertex, color) };
    VkPipelineColorBlendAttachmentState colorblend_attachment = {
      .blendEnable = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
    };
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    lida_PipelineDesc pipeline_desc = {
      .vertex_shader = "shaders/text.vert.spv",
      .fragment_shader = "shaders/text.frag.spv",
      .vertex_binding_count = 1,
      .vertex_bindings = &input_binding,
      .vertex_attribute_count = LIDA_ARR_SIZE(input_attributes),
      .vertex_attributes = input_attributes,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .depthBiasEnable = VK_FALSE,
      .msaa_samples = VK_SAMPLE_COUNT_1_BIT,
      .blend_logic_enable = VK_FALSE,
      .attachment_count = 1,
      .attachments = &colorblend_attachment,
      .dynamic_state_count = LIDA_ARR_SIZE(dynamic_states),
      .dynamic_states = dynamic_states,
      .render_pass = lida_WindowGetRenderPass(),
      .subpass = 0,
      .marker = "text-render"
    };
    err = lida_CreateGraphicsPipelines(&atlas->pipeline, 1, &pipeline_desc, &atlas->pipeline_layout);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create graphics pipeline with error '%s'",
                     lida_VkResultToString(err));
      goto error;
    }
    return atlas;
  }
 error:
  lida_MallocFree(atlas);
  return NULL;
}

void
lida_FontAtlasDestroy(lida_FontAtlas* atlas)
{
  VkDevice dev = lida_GetLogicalDevice();

  vkDestroyPipeline(dev, atlas->pipeline, NULL);

  vkDestroyImageView(dev, atlas->image_view, NULL);
  vkDestroyImage(dev, atlas->image, NULL);
  vkDestroyBuffer(dev, atlas->vertex_buffer, NULL);

  lida_VideoMemoryFree(&atlas->cpu_memory);
  lida_VideoMemoryFree(&atlas->gpu_memory);
  lida_MallocFree(atlas);
}

uint32_t
lida_FontAtlasLoad(lida_FontAtlas* atlas, VkCommandBuffer cmd, const char* font_name, uint32_t pixel_size)
{
  // load font, process it with Freetype and then send updated data to GPU
  FT_Face face;
  LoadFace(&face, font_name);
  FT_Set_Pixel_Sizes(face, 0, pixel_size);
  // FT_GlyphSlot glyph_slot = face->glyph;
  #define glyph_slot face->glyph
  stbrp_rect rects[128];
  for (uint32_t i = 32; i < 128; i++) {
    FT_Error err = FT_Load_Char(face, i, FT_LOAD_RENDER);
    if (err) {
      LIDA_LOG_WARN("freetype: failed to load char '%c' with error(%d) %s", i, err, FT_Error_String(err));
      continue;
    }
    atlas->fonts[0].glyphs[i].advance = { glyph_slot->advance.x >> 6, glyph_slot->advance.y >> 6 };
    atlas->fonts[0].glyphs[i].bearing = { glyph_slot->bitmap_left, glyph_slot->bitmap_top };
    atlas->fonts[0].glyphs[i].width = glyph_slot->bitmap.width;
    atlas->fonts[0].glyphs[i].height = glyph_slot->bitmap.rows;
    atlas->fonts[0].glyphs[i].size.x = glyph_slot->bitmap.width / (float)atlas->extent.width;
    atlas->fonts[0].glyphs[i].size.y = glyph_slot->bitmap.rows / (float)atlas->extent.height;
    rects[i-32].id = i;
    rects[i-32].w = glyph_slot->bitmap.width;
    rects[i-32].h = glyph_slot->bitmap.rows;
    // LIDA_LOG_WARN("glyph = [bitmap=%d %d advance=%f %f]",
    //               glyph_slot->bitmap.rows, glyph_slot->bitmap.width,
    //               glyph_slot->advance.x, glyph_slot->advance.y);
  }
  // pack rects
  stbrp_context rect_packing;
  uint32_t NODES = 1024;
  stbrp_node* rect_nodes = (stbrp_node*)lida_TempAllocate(sizeof(stbrp_node)*NODES);
  stbrp_init_target(&rect_packing, atlas->extent.width, atlas->extent.height, rect_nodes, NODES);
  stbrp_setup_heuristic(&rect_packing, STBRP_HEURISTIC_Skyline_default);
  if (stbrp_pack_rects(&rect_packing, rects, LIDA_ARR_SIZE(rects)-32) == 0) {
    LIDA_LOG_ERROR("failed to pack glyphs to bitmap :( maybe try to pick smaller font size?");
    return UINT32_MAX;
  }
  lida_TempFree(rect_nodes);
  // load glyphs to staging buffer
  uint8_t* tmp = (uint8_t*)atlas->vertices_mapped;
  uint32_t max_height = 0;
  for (uint32_t i = 0; i < 128-32; i++) {
    int c = rects[i].id;
    FT_Error err = FT_Load_Char(face, c, FT_LOAD_RENDER);
    if (err != 0) {
      LIDA_LOG_WARN("freetype: failed to load char '%c' with error %s", i, FT_Error_String(err));
      continue;
    }
    if (rects[i].y + rects[i].h + atlas->lines)
      max_height = rects[i].y + rects[i].h + atlas->lines;
    if (max_height > atlas->extent.height) {
      LIDA_LOG_ERROR("not enough space in font atlas; required extent is at least [%u, %u]",
                     atlas->extent.width, max_height);
      return UINT32_MAX;
    }
    // NOTE: we multiply here by 4 because format is RGBA8 - 4 bytes
    uint32_t offset = (rects[i].x + rects[i].y * atlas->extent.width) << 2;
    uint8_t* data = tmp + offset;
    for (uint32_t y = 0; y < glyph_slot->bitmap.rows; y++)
      for (uint32_t x = 0; x < glyph_slot->bitmap.width; x++) {
        uint32_t pos = (y * atlas->extent.width + x) << 2;
        // for now we fill everything with 1
        data[pos + 0] = 255;
        data[pos + 1] = 255;
        data[pos + 2] = 255;
        data[pos + 3] = glyph_slot->bitmap.buffer[y * glyph_slot->bitmap.width + x];
      }
    atlas->fonts[0].glyphs[c].offset.x = rects[i].x / (float)atlas->extent.width;
    atlas->fonts[0].glyphs[c].offset.y = (rects[i].y + atlas->lines) / (float)atlas->extent.height;
  }
  FT_Done_Face(face);
  // record commands to GPU...
  VkImageMemoryBarrier barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .image = atlas->image,
    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
  vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       0, NULL,
                       0, NULL,
                       1, &barrier);
  VkBufferImageCopy copy_info = {
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    .imageOffset = VkOffset3D{0, atlas->lines, 0},
    .imageExtent = VkExtent3D{atlas->extent.width, atlas->extent.height-atlas->lines, 1}
  };
  vkCmdCopyBufferToImage(cmd, atlas->vertex_buffer, atlas->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &copy_info);
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0,
                       0, NULL,
                       0, NULL,
                       1, &barrier);
  atlas->lines = max_height;
  return 0;
}

void lida_FontAtlasResetFonts(lida_FontAtlas* atlas)
{
  atlas->lines = 0;
}

uint32_t
lida_FontAtlasAddText(lida_FontAtlas* atlas, const char* text, uint32_t font_id, const lida_Vec2* size, const lida_Vec4* color, const lida_Vec2* pos_)
{
  Font* font = &atlas->fonts[font_id];
  // TODO: remember vertices position
  TextureVertex* vertices = atlas->vertices_mapped;
  lida_Vec2 pos = *pos_;
  while (*text) {
    LoadVertex(font, &pos, size, color, *text, vertices);
    pos.x += font->glyphs[*text].advance.x * size->x;
    pos.y += font->glyphs[*text].advance.y * size->y;
    vertices += 6;
    text++;
  }
  return vertices - atlas->vertices_mapped;
}

void
lida_FontAtlasTextDraw(lida_FontAtlas* atlas, VkCommandBuffer cmd, uint32_t num_vertices)
{
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atlas->pipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &atlas->vertex_buffer, &offset);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atlas->pipeline_layout,
                          0, 1, &atlas->descriptor_set,
                          0, NULL);
  vkCmdDraw(cmd, num_vertices, 1, 0, 0);
}



int
LoadFace(FT_Face* face, const char* name)
{
  size_t buffer_size = 0;
  uint8_t* buffer = (uint8_t*)SDL_LoadFile(name, &buffer_size);
  if (!buffer) {
    LIDA_LOG_ERROR("failed to load font from file '%s' with error '%s'", name, SDL_GetError());
    return -1;
  }
  FT_Error error = FT_New_Memory_Face(freetype,
                                      buffer,
                                      buffer_size,
                                      0, face);
  if (error != 0) {
    LIDA_LOG_ERROR("FreeType: failed to load face from file '%s' with error '%s'",
                   name, FT_Error_String(error));
  }
  SDL_free(buffer);
  return 0;
}

void
LoadVertex(Font* font, const lida_Vec2* pos_, const lida_Vec2* size, const lida_Vec4* color, char c, TextureVertex* dst)
{
  Glyph* glyph = &font->glyphs[c];
  lida_Vec2 pos;
  pos.x = pos_->x + glyph->bearing.x * size->x;
  pos.y = pos_->y - glyph->bearing.y * size->y;
  lida_Vec2 offset;
  offset.x = size->x * glyph->width;
  offset.y = size->y * glyph->height;
  const lida_Vec2 muls[] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 0.0f, 1.0f },
    { 1.0f, 1.0f }
  };
  TextureVertex vertices[4];
  for (int i = 0; i < 4; i++) {
    vertices[i].pos.x = pos.x + offset.x*muls[i].x;
    vertices[i].pos.y = pos.y + offset.y*muls[i].y;
    vertices[i].uv.x = glyph->offset.x + glyph->size.x*muls[i].x;
    vertices[i].uv.y = glyph->offset.y + glyph->size.y*muls[i].y;
    memcpy(&vertices[i].color, color, sizeof(lida_Vec4));
    // LIDA_LOG_DEBUG("vertex= [pos=[%f %f] uv=[%f %f] color=[%f %f %f %f]]",
    //                vertices[i].pos.x, vertices[i].pos.y,
    //                vertices[i].uv.x, vertices[i].uv.y,
    //                vertices[i].color.x, vertices[i].color.y, vertices[i].color.z, vertices[i].color.w);
  }
  // LIDA_LOG_DEBUG("=================================================");
  const int indices[6] = { /*1st triangle*/0, 1, 3, /*2nd triangle*/3, 2, 0 };
  for (size_t i = 0; i < LIDA_ARR_SIZE(indices); i++) {
    memcpy(dst++, &vertices[indices[i]], sizeof(TextureVertex));
  }
}
