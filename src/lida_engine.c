/* -*- mode: c -*-
   lida_engine.c
  lida engine - portable and small 3D Vulkan engine.

  ===============================
  Author: Adil Mokhammad
  Email: 0adilmohammad0@gmail.com
 */

#include "lib/volk.h"
#include "lib/ogt_vox.h"

#include "stdalign.h"
#include "string.h"
#include "math.h"

#include "lida_platform.h"

#define LIDA_ENGINE_VERSION 202302
#define INTERNAL static
#define GLOBAL static

#include "lida_base.c"
#include "lida_device.c"
#include "lida_window.c"
#include "lida_ecs.c"
#include "lida_algebra.c"
#include "lida_render.c"
#include "lida_voxel.c"
#include "lida_ui.c"
#include "lida_asset.c"
#include "lida_input.c"

typedef struct {

  Allocator entity_allocator;
  Allocator vox_allocator;
  ECS ecs;
  Forward_Pass forward_pass;
  Shadow_Pass shadow_pass;
  Bitmap_Renderer bitmap_renderer;
  Font_Atlas font_atlas;
  Camera camera;
  Voxel_Drawer vox_drawer;
  Deletion_Queue deletion_queue;
  Asset_Manager asset_manager;
  Keymap root_keymap;
  Keymap camera_keymap;
  EID rect_pipeline;
  EID triangle_pipeline;
  EID voxel_pipeline;
  EID shadow_pipeline;
  uint32_t prev_time;
  uint32_t curr_time;
  int render_mode;

} Engine_Context;

enum {
  RENDER_MODE_DEFAULT,
  RENDER_MODE_SHADOW_MAP,
  RENDER_MODE_COUNT,
};

GLOBAL Engine_Context* g_context;

GLOBAL EID grid_1;
GLOBAL EID grid_2;


/// Engine general functions

INTERNAL void RootKeymap_Pressed(PlatformKeyCode key, void* udata);
INTERNAL void CameraKeymap_Pressed(PlatformKeyCode key, void* udata);
INTERNAL void CameraKeymap_Released(PlatformKeyCode key, void* udata);
INTERNAL void CameraKeymap_Mouse(int x, int y, int xrel, int yrel, void* udata);

INTERNAL void CreateRectPipeline(Pipeline_Desc* description);
INTERNAL void CreateTrianglePipeline(Pipeline_Desc* description);
INTERNAL void CreateVoxelPipeline(Pipeline_Desc* description);
INTERNAL void CreateShadowPipeline(Pipeline_Desc* description);

void
EngineInit(const Engine_Startup_Info* info)
{
  const size_t total_memory = 16 * 1024 * 1024;
  InitMemoryChunk(&g_persistent_memory, PlatformAllocateMemory(total_memory), total_memory);
  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  CreateDevice(info->enable_debug_layers,
               info->gpu_id,
               info->app_name, info->app_version,
               device_extensions, ARR_SIZE(device_extensions));
  CreateWindow(info->window_vsync);

  g_context = PersistentAllocate(sizeof(Engine_Context));
#define INIT_ALLOCATOR(alloc, mb) InitAllocator(&g_context->alloc, MemoryAllocateRight(&g_persistent_memory, mb * 1024 * 1024), mb*1024*1024)
  INIT_ALLOCATOR(vox_allocator, 4);
  INIT_ALLOCATOR(entity_allocator, 1);

  int options[] = { 1, 2, 4, 8, 16, 32 };
  VkSampleCountFlagBits values[] = { VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT };
  VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_4_BIT;
  for (size_t i = 0; i < ARR_SIZE(values); i++)
    if (info->msaa_samples == options[i]) {
      msaa_samples = values[i];
      break;
    }
  CreateForwardPass(&g_context->forward_pass,
                    g_window->swapchain_extent.width, g_window->swapchain_extent.height,
                    msaa_samples);

  CreateShadowPass(&g_context->shadow_pass, &g_context->forward_pass,
                   1024, 1024);

  InitAssetManager(&g_context->asset_manager);

  CreateBitmapRenderer(&g_context->bitmap_renderer);
  CreateFontAtlas(&g_context->bitmap_renderer, &g_context->font_atlas, 512, 128);

  g_context->camera.z_near = 0.01f;
  g_context->camera.position = VEC3_CREATE(0.0f, 0.0f, -2.0f);
  g_context->camera.rotation = VEC3_CREATE(0.0f, 3.141f, 0.0f);
  g_context->camera.up = VEC3_CREATE(0.0f, 1.0f, 0.0f);
  g_context->camera.fovy = RADIANS(45.0f);
  g_context->camera.rotation_speed = 0.005f;
  g_context->camera.movement_speed = 1.0f;

  g_context->prev_time = PlatformGetTicks();
  g_context->curr_time = g_context->prev_time;

  CreateECS(&g_context->entity_allocator, &g_context->ecs, 8, 8);

  REGISTER_COMPONENT(Voxel_Grid, NULL, NULL);
  REGISTER_COMPONENT(Transform, NULL, NULL);
  REGISTER_COMPONENT(Pipeline_Program, NULL, NULL);

  g_context->rect_pipeline = CreateEntity(&g_context->ecs);
  g_context->triangle_pipeline = CreateEntity(&g_context->ecs);
  g_context->voxel_pipeline = CreateEntity(&g_context->ecs);
  g_context->shadow_pipeline = CreateEntity(&g_context->ecs);
  AddPipelineProgramComponent(&g_context->ecs, &g_context->asset_manager, g_context->rect_pipeline,
                              "rect.vert.spv", "rect.frag.spv",
                              &CreateRectPipeline, &g_context->deletion_queue);
  AddPipelineProgramComponent(&g_context->ecs, &g_context->asset_manager, g_context->triangle_pipeline,
                              "triangle.vert.spv", "triangle.frag.spv",
                              &CreateTrianglePipeline, &g_context->deletion_queue);
  AddPipelineProgramComponent(&g_context->ecs, &g_context->asset_manager, g_context->voxel_pipeline,
                              "voxel.vert.spv", "voxel.frag.spv",
                              &CreateVoxelPipeline, &g_context->deletion_queue);
  AddPipelineProgramComponent(&g_context->ecs, &g_context->asset_manager, g_context->shadow_pipeline,
                              "shadow_voxel.vert.spv", NULL,
                              &CreateShadowPipeline, &g_context->deletion_queue);

  CreateVoxelDrawer(&g_context->vox_drawer, 128*1024, 32);

  // create some entities
  grid_1 = CreateEntity(&g_context->ecs);
  grid_2 = CreateEntity(&g_context->ecs);

  // entity 1
  Voxel_Grid* vox;
  AddVoxelGridComponent(&g_context->ecs, &g_context->asset_manager, &g_context->vox_allocator,
                        grid_1, "3x3x3.vox");
  Transform* transform = AddComponent(&g_context->ecs, grid_1, &type_info_Transform);
  transform->rotation = QUAT_IDENTITY();
  transform->position = VEC3_CREATE(3.1f, 2.6f, 1.0f);
  transform->scale = 3.0f;
  // entity 2
  AddVoxelGridComponent(&g_context->ecs, &g_context->asset_manager, &g_context->vox_allocator,
                        grid_2, "chr_beau.vox");
  transform = AddComponent(&g_context->ecs, grid_2, &type_info_Transform);
  transform->rotation = QUAT_IDENTITY();
  transform->position = VEC3_CREATE(-1.1f, -1.6f, 7.0f);
  transform->scale = 0.9f;
  // floor
  EID floor = CreateEntity(&g_context->ecs);
  vox = AddComponent(&g_context->ecs, floor, &type_info_Voxel_Grid);
  transform = AddComponent(&g_context->ecs, floor, &type_info_Transform);
  // manually write voxels
  AllocateVoxelGrid(&g_context->vox_allocator, vox, 128, 1, 128);
  // арбузовое счастье
  vox->palette[1] = 0x00004C00;
  vox->palette[2] = 0x00003C00;
  for (size_t i = 0; i < 128 * 128; i++) {
    Voxel* voxels = vox->data->ptr;
    if ((i >> 3) & 1) voxels[i] = 1;
    else voxels[i] = 2;
  }
  vox->hash = HashMemory64(vox->data->ptr, 128*128);
  transform->rotation = QUAT_IDENTITY();
  transform->position = VEC3_CREATE(0.0f, -4.0f, 0.0f);
  transform->scale = 1.0f;

  // TODO: this line is for testing asset manager
  AddAsset(&g_context->asset_manager, 0, "file.txt", NULL, NULL, NULL);

  g_context->deletion_queue.left = 0;
  g_context->deletion_queue.count = 0;

  // keybindings
  g_context->root_keymap = (Keymap) { &RootKeymap_Pressed, NULL, NULL, NULL };
  g_context->camera_keymap = (Keymap) { &CameraKeymap_Pressed, &CameraKeymap_Released,
                                        &CameraKeymap_Mouse, &g_context->camera };
  BindKeymap(&g_context->root_keymap);

  // create pipelines
  BatchCreatePipelines(&g_context->ecs);
}

void
EngineFree()
{
  {
    FOREACH_COMPONENT(&g_context->ecs, Voxel_Grid, &type_info_Voxel_Grid) {
      FreeVoxelGrid(&g_context->vox_allocator, &components[i]);
    }
  }

  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  DestroyVoxelDrawer(&g_context->vox_drawer);

  DestroyFontAtlas(&g_context->font_atlas);
  DestroyBitmapRenderer(&g_context->bitmap_renderer);

  {
    FOREACH_COMPONENT(&g_context->ecs, Pipeline_Program, &type_info_Pipeline_Program) {
      vkDestroyPipeline(g_device->logical_device, components[i].pipeline, NULL);
    }
  }

  DestroyECS(&g_context->ecs);

  if (ReleaseAllocator(&g_context->vox_allocator)) {
    LOG_WARN("vox: memory leak detected");
  }

  if (ReleaseAllocator(&g_context->entity_allocator)) {
    LOG_WARN("entity: memory leak detected");
    DebugListAllocations(&g_context->entity_allocator);
  }

  // FreeAssetManager(&g_context->asset_manager);

  DestroyShadowPass(&g_context->shadow_pass);

  DestroyForwardPass(&g_context->forward_pass);

  // PersistentRelease(g_context);

  DestroyWindow(0);
  DestroyDevice(0);
  PlatformFreeMemory(g_persistent_memory.ptr);
}

void
EngineUpdateAndRender()
{
  // calculate time difference
  g_context->prev_time = g_context->curr_time;
  g_context->curr_time = PlatformGetTicks();
  const float dt = (g_context->curr_time - g_context->prev_time) / 1000.0f;

  switch (g_window->frame_counter & 31)
    {
    case 31:
      // get file notifications every 32 frame
      UpdateAssets(&g_context->asset_manager, &g_context->ecs);
      break;

    case 30:
      // destroy objects needed to be destroyed
      UpdateDeletionQueue(&g_context->deletion_queue);
      break;
    }

  // update camera position, rotation etc.
  Camera* camera = &g_context->camera;
  CameraUpdate(camera, dt, g_window->swapchain_extent.width, g_window->swapchain_extent.height);
  CameraUpdateProjection(camera);
  CameraUpdateView(camera);

  Scene_Data_Struct* sc_data = g_context->forward_pass.uniform_buffer_mapped;
  memcpy(&sc_data->camera_projection, &camera->projection_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_view, &camera->view_matrix, sizeof(Mat4));
  Mat4_Mul(&sc_data->camera_projection, &sc_data->camera_view, &sc_data->camera_projview);
  sc_data->sun_dir = VEC3_CREATE(0.03f, 0.9f, 0.09f);
  sc_data->sun_ambient = 0.1f;
  Vec3_Normalize(&sc_data->sun_dir, &sc_data->sun_dir);

  Mat4 light_proj, light_view;
  // const float b = 4.0f;
  // OrthographicMatrix(-b, b, -b, b, 1.0f, 10.0f, &light_proj);
  GLOBAL float light_fov = 3.14f / 5.0f;
  PerspectiveMatrix(light_fov, 1.0f, 1.0f, &light_proj);

  GLOBAL Vec3 light_off = {0.03f, -0.95f, 0.0f};
  Vec3 light_pos = VEC3_MUL(sc_data->sun_dir, 3.0f);
  Vec3 light_target = VEC3_SUB(light_pos, light_off);
  LookAtMatrix(&light_pos, &light_target, &camera->up,
               &light_view);

  Mat4_Mul(&light_proj, &light_view, &sc_data->light_space);

  NewVoxelDrawerFrame(&g_context->vox_drawer);

  FOREACH_COMPONENT(&g_context->ecs, Voxel_Grid, &type_info_Voxel_Grid) {
    Transform* transform = GetComponent(&g_context->ecs, entities[i], &type_info_Transform);
    PushMeshToVoxelDrawer(&g_context->vox_drawer, &components[i], transform);
  }

  VkCommandBuffer cmd = BeginCommands();

  if (g_window->frame_counter == 0) {
    LoadToFontAtlas(&g_context->bitmap_renderer, &g_context->font_atlas, cmd, "arial.ttf", 32);
  } else {
    NewBitmapFrame(&g_context->bitmap_renderer);
    Vec2 pos = { 0.04f, 0.4f };
    Vec2 text_size = { 0.05f, 0.05f };
    Vec4 color = { 1.0f, 0.3f, 0.24f, 0.95f };
    DrawText(&g_context->bitmap_renderer, &g_context->font_atlas, "Banana", 0, &text_size, &color, &pos);
    pos = (Vec2) { 0.04f, 0.7f };
    text_size = (Vec2) { 0.05f, 0.05f };
    color = (Vec4) { 1.0f, 0.3f, 0.24f, 0.95f };
    DrawText(&g_context->bitmap_renderer, &g_context->font_atlas, "Nice!", 0, &text_size, &color, &pos);
  }

  VkDescriptorSet ds_set;

  // render to shadow map
  BeginShadowPass(&g_context->shadow_pass, cmd);
  {
    Pipeline_Program* prog = GetComponent(&g_context->ecs, g_context->shadow_pipeline, &type_info_Pipeline_Program);
    ds_set = g_context->shadow_pass.scene_data_set;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->layout,
                        0, 1, &ds_set, 0, NULL);
    vkCmdSetDepthBias(cmd, 1.0f, 0.0f, 2.0f);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
    DrawVoxels(&g_context->vox_drawer, cmd);
  }
  vkCmdEndRenderPass(cmd);

  // render to offscreen buffer
  float clear_color[4] = { 0.08f, 0.2f, 0.25f, 1.0f };
  BeginForwardPass(&g_context->forward_pass, cmd, clear_color);
  {
    // draw triangles
    Pipeline_Program* prog = GetComponent(&g_context->ecs, g_context->triangle_pipeline, &type_info_Pipeline_Program);
    ds_set = g_context->forward_pass.scene_data_set;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            // g_context->triangle_pipeline_layout, 0, 1, &ds_set, 0, NULL);
                            prog->layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
    // 1st draw
    Vec4 colors[] = {
      VEC4_CREATE(1.0f, 0.2f, 0.2f, 1.0f),
      VEC4_CREATE(0.0f, 0.9f, 0.4f, 1.0f),
      VEC4_CREATE(0.2f, 0.35f, 0.76f, 1.0f),
      VEC4_CREATE(0.0f, 0.0f, 0.0f, 0.0f)
    };
    vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Vec4)*3 + sizeof(Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // 2nd draw
    colors[1] = VEC4_CREATE(1.0f, 0.2f, 0.6f, 0.0f);
    colors[2] = VEC4_CREATE(0.1f, 0.3f, 1.0f, 0.0f);
    colors[3] = VEC4_CREATE(-1.0f, 4.0f, 6.7f, 0.0f);
    vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Vec4)*3 + sizeof(Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // draw voxels
    prog = GetComponent(&g_context->ecs, g_context->voxel_pipeline, &type_info_Pipeline_Program);
    VkDescriptorSet ds_sets[] = { ds_set, g_context->shadow_pass.shadow_set };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            prog->layout, 0, ARR_SIZE(ds_sets), ds_sets, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
    for (uint32_t i = 0; i < 6; i++) {
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &i);
      DrawVoxelsWithNormals(&g_context->vox_drawer, cmd, i);
    }
  }
  vkCmdEndRenderPass(cmd);

  // render to screen
  BeginRenderingToWindow();
  {
    switch (g_context->render_mode) {
    case RENDER_MODE_DEFAULT:
      ds_set = g_context->forward_pass.resulting_image_set;
      break;
    case RENDER_MODE_SHADOW_MAP:
      ds_set = g_context->shadow_pass.shadow_set;
      break;
    default:
      Assert(0);
    }
    Pipeline_Program* prog = GetComponent(&g_context->ecs, g_context->rect_pipeline, &type_info_Pipeline_Program);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            prog->layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
    vkCmdDraw(cmd, 4, 1, 0, 0);

    // draw text
    RenderBitmaps(&g_context->bitmap_renderer, cmd);
  }
  vkCmdEndRenderPass(cmd);

  // end of frame
  vkEndCommandBuffer(cmd);

  SendForwardPassData(&g_context->forward_pass);

  VkResult err = PresentToScreen();
  if (err == VK_SUBOPTIMAL_KHR) {
    // resize render attachments
    vkDeviceWaitIdle(g_device->logical_device);
    ResizeWindow();
    ResetDynamicDescriptorSets();
    ResizeForwardPass(&g_context->forward_pass,
                      g_window->swapchain_extent.width, g_window->swapchain_extent.height);
  }
}

void
EngineKeyPressed(PlatformKeyCode key)
{
  KeyPressed(key);
}

void
EngineKeyReleased(PlatformKeyCode key)
{
  KeyReleased(key);
}

void
EngineMouseMotion(int x, int y, int xrel, int yrel)
{
  MouseMotion(x, y, xrel, yrel);
}


/// keymaps

void
RootKeymap_Pressed(PlatformKeyCode key, void* udata)
{
  (void)udata;
  switch (key)
    {
      // 'escape' escapes
    case PlatformKey_ESCAPE:
      PlatformWantToQuit();
      break;

      // '1' prints FPS
    case PlatformKey_1:
      LOG_INFO("FPS=%f", g_window->frames_per_second);
      break;
      // '4' toggles render mode
    case PlatformKey_4:
      g_context->render_mode = (g_context->render_mode + 1) % RENDER_MODE_COUNT;
      break;
      // '7' shrinks memory
    case PlatformKey_7:
      {
        uint32_t s = FixFragmentation(&g_context->vox_allocator);
        LOG_INFO("just saved %u bytes", s);
      } break;
      // ALT-C goes to camera mode
    case PlatformKey_C:
      if (modkey_alt) {
        BindKeymap(&g_context->camera_keymap);
        PlatformHideCursor();
      }
      break;

    default:
      break;
    }
}

void
CameraKeymap_Pressed(PlatformKeyCode key, void* udata)
{
  Camera* camera = udata;
  switch (key)
    {
      // camera movement
    case PlatformKey_W:
      CameraPressed(camera, CAMERA_PRESSED_FORWARD);
      break;
    case PlatformKey_S:
      CameraPressed(camera, CAMERA_PRESSED_BACK);
      break;
    case PlatformKey_A:
      CameraPressed(camera, CAMERA_PRESSED_LEFT);
      break;
    case PlatformKey_D:
      CameraPressed(camera, CAMERA_PRESSED_RIGHT);
      break;
    case PlatformKey_LSHIFT:
      CameraPressed(camera, CAMERA_PRESSED_DOWN);
      break;
    case PlatformKey_SPACE:
      CameraPressed(camera, CAMERA_PRESSED_UP);
      break;

      // Escape goes to developer mode
    case PlatformKey_ESCAPE:
      UnbindKeymap();
      PlatformShowCursor();
      break;

    default:
      break;
    }
}

void
CameraKeymap_Released(PlatformKeyCode key, void* udata)
{
  Camera* camera = udata;
  switch (key)
    {

    // camera movement
    case PlatformKey_W:
      CameraUnpressed(camera, CAMERA_PRESSED_FORWARD);
      break;
    case PlatformKey_S:
      CameraUnpressed(camera, CAMERA_PRESSED_BACK);
      break;
    case PlatformKey_A:
      CameraUnpressed(camera, CAMERA_PRESSED_LEFT);
      break;
    case PlatformKey_D:
      CameraUnpressed(camera, CAMERA_PRESSED_RIGHT);
      break;
    case PlatformKey_LSHIFT:
      CameraUnpressed(camera, CAMERA_PRESSED_DOWN);
      break;
    case PlatformKey_SPACE:
      CameraUnpressed(camera, CAMERA_PRESSED_UP);
      break;

    default:
      break;

    }
}

void
CameraKeymap_Mouse(int x, int y, int xrel, int yrel, void* udata)
{
  Camera* camera = udata;
  (void)x;
  (void)y;
  CameraRotate(camera, yrel, xrel, 0.0f);
}


/// pipeline creation

void CreateRectPipeline(Pipeline_Desc* description)
{
  // TODO: get rid of static here
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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
    .marker = "blit-3D-scene-fullscreen"
  };
}

void CreateTrianglePipeline(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .depth_bias_enable = VK_FALSE,
    .msaa_samples = g_context->forward_pass.msaa_samples,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = g_context->forward_pass.render_pass,
    .subpass = 0,
    .marker = "draw-triangle-pipeline"
  };
}

void CreateVoxelPipeline(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depth_bias_enable = VK_FALSE,
    .msaa_samples = g_context->forward_pass.msaa_samples,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = g_context->forward_pass.render_pass,
    .subpass = 0,
    .marker = "forward/voxel-pipeline"
  };
  PipelineVoxelVertices(&description->vertex_attributes, &description->vertex_attribute_count,
                        &description->vertex_bindings, &description->vertex_binding_count,
                        1);
}

void CreateShadowPipeline(Pipeline_Desc* description)
{
  static VkDynamicState dynamic_state = VK_DYNAMIC_STATE_DEPTH_BIAS;
  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .depth_bias_enable = VK_TRUE,
    .msaa_samples = VK_SAMPLE_COUNT_1_BIT,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 0,
    .dynamic_state_count = 1,
    .dynamic_states = &dynamic_state,
    .render_pass = g_context->shadow_pass.render_pass,
    .subpass = 0,
    .marker = "voxels-to-shadow-map",
  };
  PipelineVoxelVertices(&description->vertex_attributes, &description->vertex_attribute_count,
                        &description->vertex_bindings, &description->vertex_binding_count,
                        0);
  ShadowPassViewport(&g_context->shadow_pass, &description->viewport, &description->scissor);
}
