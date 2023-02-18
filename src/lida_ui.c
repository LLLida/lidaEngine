/*

  Text rendering and management.

 */
#include "ft2build.h"
#include FT_FREETYPE_H

// this is for creating font atlas
#define STB_RECT_PACK_IMPLEMENTATION
#include "lib/stb_rect_pack.h"

GLOBAL FT_Library g_ft_library;

// TODO: code below needs refactoring

typedef struct {

  Vec2 pos;
  Vec2 uv;
  Vec4 color;

} Bitmap_Vertex;

typedef struct {

  FT_Vector advance;
  iVec2 bearing;
  uint32_t width;
  uint32_t height;
  Vec2 offset;
  Vec2 size;

} Glyph_Info;

typedef struct {

  Glyph_Info glyphs[128];
  uint32_t pixel_size;

} Font;

typedef struct {

  Video_Memory gpu_memory;
  Video_Memory cpu_memory;
  VkBuffer vertex_buffer;
  VkImage image;
  VkImageView image_view;
  VkExtent2D extent;
  VkDescriptorSet descriptor_set;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline;
  uint32_t max_vertices;
  Bitmap_Vertex* vertices_mapped;
  uint32_t lines;
  Font fonts[4];

} Font_Atlas;


/// internal functions

INTERNAL void
LoadVertex(Font* font, const Vec2* pos_, const Vec2* size, const Vec4* color, char c, Bitmap_Vertex* dst)
{
  Glyph_Info* glyph = &font->glyphs[(int)c];
  Vec2 pos;
  pos.x = pos_->x + glyph->bearing.x * size->x;
  pos.y = pos_->y - glyph->bearing.y * size->y;
  Vec2 offset;
  offset.x = size->x * glyph->width;
  offset.y = size->y * glyph->height;
  const Vec2 muls[] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 0.0f, 1.0f },
    { 1.0f, 1.0f }
  };
  Bitmap_Vertex vertices[4];
  for (int i = 0; i < 4; i++) {
    vertices[i].pos.x = pos.x + offset.x*muls[i].x;
    vertices[i].pos.y = pos.y + offset.y*muls[i].y;
    vertices[i].uv.x = glyph->offset.x + glyph->size.x*muls[i].x;
    vertices[i].uv.y = glyph->offset.y + glyph->size.y*muls[i].y;
    memcpy(&vertices[i].color, color, sizeof(Vec4));
  }
  const int indices[6] = { /*1st triangle*/0, 1, 3, /*2nd triangle*/3, 2, 0 };
  for (size_t i = 0; i < ARR_SIZE(indices); i++) {
    memcpy(dst++, &vertices[indices[i]], sizeof(Bitmap_Vertex));
  }
}


/// public functions

INTERNAL VkResult
CreateFontAtlas(Font_Atlas* atlas, uint32_t width, uint32_t height)
{
  if (g_ft_library == NULL) {
    FT_Error error = FT_Init_FreeType(&g_ft_library);
    if (error != 0) {
      LOG_ERROR("failed to init freetype library with error '%s'",
                FT_Error_String(error));
    }
  }

  atlas->extent = (VkExtent2D) { width, height };
  atlas->lines = 0;
  // NOTE: 4 megabytes may be too much for font bitmaps
  VkDeviceSize font_bytes = 4 * 1024 * 1024;
  VkResult err = AllocateVideoMemory(&atlas->gpu_memory, font_bytes, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, UINT32_MAX,
                                     "font/main-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory for fonts with error '%s'", ToString_VkResult(err));
    return err;
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
  err = CreateImage(&atlas->image, &image_info, "font/atlas-image");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create font atlas image with error '%s'", ToString_VkResult(err));
    return err;
  }
  VkMemoryRequirements image_requirements;
  vkGetImageMemoryRequirements(g_device->logical_device, atlas->image, &image_requirements);
  err = ImageBindToMemory(&atlas->gpu_memory, atlas->image,
                          &image_requirements);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind font atlas image to memory with error '%s'",
              ToString_VkResult(err));
    return err;
  }
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = atlas->image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = image_info.format,
    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
  err = CreateImageView(&atlas->image_view, &image_view_info, "font/atlas-image-view");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create image view with error %s", ToString_VkResult(err));
    return err;
  }
  // create vertex buffer
  atlas->max_vertices = 64 * 1024;
  err = CreateBuffer(&atlas->vertex_buffer, atlas->max_vertices * sizeof(Bitmap_Vertex),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     "font/vertex-staging-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create buffer vertex buffer with error '%s'", ToString_VkResult(err));
    return err;
  }
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(g_device->logical_device, atlas->vertex_buffer, &requirements);
  err = AllocateVideoMemory(&atlas->cpu_memory, requirements.size,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, requirements.memoryTypeBits,
                            "font/staging-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory for vertex buffer with error '%s'",
              ToString_VkResult(err));
    return err;
  }
  // bind vertex buffer to memory
  err = BufferBindToMemory(&atlas->cpu_memory, atlas->vertex_buffer, &requirements,
                           (void**)&atlas->vertices_mapped, NULL);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind vertex buffer to memory with error '%s'",
              ToString_VkResult(err));
    return err;
  }
  // allocate descriptor set
  VkDescriptorSetLayoutBinding binding = {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  err = AllocateDescriptorSets(&binding, 1, &atlas->descriptor_set, 1, 0, "font/descriptor-set");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor set with error '%s'",
              ToString_VkResult(err));
    return err;
  }
  VkDescriptorImageInfo ds_image_info = {
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
    .imageView = atlas->image_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  VkWriteDescriptorSet write_set = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = atlas->descriptor_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &ds_image_info,
  };
  UpdateDescriptorSets(&write_set, 1);
  // create pipeline
  VkVertexInputBindingDescription input_binding = { 0, sizeof(Bitmap_Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
  VkVertexInputAttributeDescription input_attributes[3];
  input_attributes[0] = (VkVertexInputAttributeDescription) { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Bitmap_Vertex, pos) };
  input_attributes[1] = (VkVertexInputAttributeDescription) { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Bitmap_Vertex, uv) };
  input_attributes[2] = (VkVertexInputAttributeDescription) { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Bitmap_Vertex, color) };
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
  Pipeline_Desc pipeline_desc = {
    .vertex_shader = "text.vert.spv",
    .fragment_shader = "text.frag.spv",
    .vertex_binding_count = 1,
    .vertex_bindings = &input_binding,
    .vertex_attribute_count = ARR_SIZE(input_attributes),
    .vertex_attributes = input_attributes,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .depth_bias_enable = VK_FALSE,
    .msaa_samples = VK_SAMPLE_COUNT_1_BIT,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = g_window->render_pass,
    .subpass = 0,
    .marker = "text-render"
  };
  err = CreateGraphicsPipelines(&atlas->pipeline, 1, &pipeline_desc, &atlas->pipeline_layout);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create graphics pipeline with error '%s'",
              ToString_VkResult(err));
    return err;
  }
  return err;
}

INTERNAL void
DestroyFontAtlas(Font_Atlas* atlas)
{
  vkDestroyPipeline(g_device->logical_device, atlas->pipeline, NULL);

  vkDestroyImageView(g_device->logical_device, atlas->image_view, NULL);
  vkDestroyImage(g_device->logical_device, atlas->image, NULL);
  vkDestroyBuffer(g_device->logical_device, atlas->vertex_buffer, NULL);

  FreeVideoMemory(&atlas->cpu_memory);
  FreeVideoMemory(&atlas->gpu_memory);
}

INTERNAL uint32_t
LoadToFontAtlas(Font_Atlas* atlas, VkCommandBuffer cmd, const char* font_name, uint32_t pixel_size)
{
  // load font, process it with Freetype and then send updated data to GPU
  FT_Face face;
  size_t buffer_size = 0;
  uint8_t* buffer = PlatformLoadEntireFile(font_name, &buffer_size);
  if (!buffer) {
    LOG_ERROR("failed to load font from file '%s' with error '%s'", font_name, PlatformGetError());
    return UINT32_MAX;
  }
  FT_Error error = FT_New_Memory_Face(g_ft_library,
                                      buffer,
                                      buffer_size,
                                      0, &face);
  if (error != 0) {
    LOG_ERROR("FreeType: failed to load face from file '%s' with error '%s'",
              font_name, FT_Error_String(error));
    PlatformFreeFile(buffer);
    return UINT32_MAX;
  }
  FT_Set_Pixel_Sizes(face, 0, pixel_size);
  FT_GlyphSlot glyph_slot = face->glyph;
  stbrp_rect rects[128];
  for (size_t i = 32; i < 128; i++) {
    FT_Error err = FT_Load_Char(face, i, FT_LOAD_RENDER);
    if (err) {
      LOG_WARN("freetype: failed to load char '%c' with error(%d) %s", (char)i, err, FT_Error_String(err));
      continue;
    }
    atlas->fonts[0].glyphs[i].advance.x = glyph_slot->advance.x >> 6;
    atlas->fonts[0].glyphs[i].advance.y = glyph_slot->advance.y >> 6;
    atlas->fonts[0].glyphs[i].bearing.x = glyph_slot->bitmap_left;
    atlas->fonts[0].glyphs[i].bearing.y = glyph_slot->bitmap_top;
    atlas->fonts[0].glyphs[i].width = glyph_slot->bitmap.width;
    atlas->fonts[0].glyphs[i].height = glyph_slot->bitmap.rows;
    atlas->fonts[0].glyphs[i].size.x = glyph_slot->bitmap.width / (float)atlas->extent.width;
    atlas->fonts[0].glyphs[i].size.y = glyph_slot->bitmap.rows / (float)atlas->extent.height;
    rects[i-32].id = i;
    rects[i-32].w = glyph_slot->bitmap.width;
    rects[i-32].h = glyph_slot->bitmap.rows;
  }
  // pack rects
  stbrp_context rect_packing;
  uint32_t NODES = 1024;
  stbrp_node* rect_nodes = (stbrp_node*)PersistentAllocate(sizeof(stbrp_node)*NODES);
  stbrp_init_target(&rect_packing, atlas->extent.width, atlas->extent.height, rect_nodes, NODES);
  stbrp_setup_heuristic(&rect_packing, STBRP_HEURISTIC_Skyline_default);
  if (stbrp_pack_rects(&rect_packing, rects, ARR_SIZE(rects)-32) == 0) {
    LOG_ERROR("failed to pack glyphs to bitmap :( maybe try to pick smaller font size?");
    PlatformFreeFile(buffer);
    return UINT32_MAX;
  }
  PersistentRelease(rect_nodes);
  // load glyphs to staging buffer
  uint8_t* tmp = (uint8_t*)atlas->vertices_mapped;
  uint32_t max_height = 0;
  for (uint32_t i = 0; i < 128-32; i++) {
    int c = rects[i].id;
    FT_Error err = FT_Load_Char(face, c, FT_LOAD_RENDER);
    if (err != 0) {
      LOG_WARN("freetype: failed to load char '%c' with error %s", i, FT_Error_String(err));
      continue;
    }
    if (rects[i].y + rects[i].h + atlas->lines)
      max_height = rects[i].y + rects[i].h + atlas->lines;
    if (max_height > atlas->extent.height) {
      LOG_ERROR("not enough space in font atlas; required extent is at least [%u, %u]",
                atlas->extent.width, max_height);
      PlatformFreeFile(buffer);
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
    .imageOffset = (VkOffset3D) {0, atlas->lines, 0},
    .imageExtent = (VkExtent3D) {atlas->extent.width, atlas->extent.height-atlas->lines, 1}
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
  PlatformFreeFile(buffer);
  return 0;
}

INTERNAL void
ResetFontAtlas(Font_Atlas* atlas)
{
  atlas->lines = 0;
}

INTERNAL uint32_t
AddTextToFontAtlas(Font_Atlas* atlas, const char* text, uint32_t font_id, const Vec2* size, const Vec4* color, const Vec2* pos_)
{
  Font* font = &atlas->fonts[font_id];
  // TODO: remember vertices position
  Bitmap_Vertex* vertices = atlas->vertices_mapped;
  Vec2 pos = *pos_;
  while (*text) {
    LoadVertex(font, &pos, size, color, *text, vertices);
    pos.x += font->glyphs[(int)*text].advance.x * size->x;
    pos.y += font->glyphs[(int)*text].advance.y * size->y;
    vertices += 6;
    text++;
  }
  return vertices - atlas->vertices_mapped;
}

INTERNAL void
DrawFontAtlasText(Font_Atlas* atlas, VkCommandBuffer cmd, uint32_t num_vertices)
{
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atlas->pipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &atlas->vertex_buffer, &offset);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atlas->pipeline_layout,
                          0, 1, &atlas->descriptor_set,
                          0, NULL);
  vkCmdDraw(cmd, num_vertices, 1, 0, 0);
}
