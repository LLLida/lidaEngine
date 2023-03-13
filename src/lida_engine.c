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

#define LIDA_ENGINE_VERSION 202303
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
#include "lida_config.c"
#include "lida_script.c"
#include "lida_package.c"
#include "lida_console.c"

typedef struct {

  Allocator entity_allocator;
  Allocator vox_allocator;
  ECS ecs;
  Forward_Pass forward_pass;
  Shadow_Pass shadow_pass;
  Quad_Renderer quad_renderer;
  Font_Atlas font_atlas;
  Camera camera;
  Voxel_Drawer vox_drawer;
  Debug_Drawer debug_drawer;
  Deletion_Queue deletion_queue;
  Asset_Manager asset_manager;
  Script_Manager script_manager;
  Keymap root_keymap;
  Keymap camera_keymap;

  Mesh_Pass shadow_cull;
  Mesh_Pass main_cull;

  // pipelines
  EID rect_pipeline;
  EID triangle_pipeline;
  EID voxel_pipeline;
  EID shadow_pipeline;
  EID debug_pipeline;
  VkPipeline compute_pipeline;
  VkPipelineLayout compute_pipeline_layout;

  // fonts
  EID arial_font;
  EID pixel_font;

  uint32_t prev_time;
  uint32_t curr_time;
  int render_mode;
  uint32_t voxel_draw_calls;

} Engine_Context;

enum {
  RENDER_MODE_DEFAULT,
  RENDER_MODE_SHADOW_MAP,
  RENDER_MODE_COUNT,
};

GLOBAL Engine_Context* g_context;

// simple X macro to do operation on all components
#define X_ALL_COMPONENTS()                      \
  X(Voxel_Grid);                                \
  X(Transform);                                 \
  X(Voxel_Cached);                              \
  X(OBB);                                       \
  X(Pipeline_Program);                          \
  X(Font);                                      \
  X(Config_File);                               \
  X(Script)


/// Engine general functions

INTERNAL int RootKeymap_Pressed(PlatformKeyCode key, void* udata);
INTERNAL int CameraKeymap_Pressed(PlatformKeyCode key, void* udata);
INTERNAL int CameraKeymap_Released(PlatformKeyCode key, void* udata);
INTERNAL int CameraKeymap_Mouse(int x, int y, float xrel, float yrel, void* udata);

INTERNAL void CreateRectPipeline(Pipeline_Desc* description);
INTERNAL void CreateTrianglePipeline(Pipeline_Desc* description);
INTERNAL void CreateVoxelPipeline(Pipeline_Desc* description);
INTERNAL void CreateShadowPipeline(Pipeline_Desc* description);
INTERNAL void CreateDebugDrawPipeline(Pipeline_Desc* description);

void
EngineInit(const Engine_Startup_Info* info)
{
  const size_t total_memory = 16 * 1024 * 1024;
  InitMemoryChunk(&g_persistent_memory, PlatformAllocateMemory(total_memory), total_memory);

  ProfilerStart();
  PROFILE_FUNCTION();

  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  CreateDevice(info->enable_debug_layers,
               info->gpu_id,
               info->app_name, info->app_version,
               device_extensions, ARR_SIZE(device_extensions));
  CreateWindow(info->window_vsync);

  g_context = PersistentAllocate(sizeof(Engine_Context));
#define INIT_ALLOCATOR(alloc, mb) InitAllocator(&g_context->alloc, MemoryAllocateRight(&g_persistent_memory, mb * 1024 * 1024), 1024*1024*mb)
  INIT_ALLOCATOR(vox_allocator, 4);
  INIT_ALLOCATOR(entity_allocator, 1);

  CreateECS(&g_context->entity_allocator, &g_context->ecs, 8);

  // register all components
#define X(a) REGISTER_COMPONENT(a)
  X_ALL_COMPONENTS();
#undef X

  InitAssetManager(&g_context->asset_manager);

  g_config = CreateConfig(&g_context->ecs, &g_context->asset_manager,
                          CreateEntity(&g_context->ecs), "variables.ini");
  g_profiler.enabled = *GetVar_Int(g_config, "Misc.profiling");

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

  {
    // TODO: check for null
    int dim = *GetVar_Int(g_config, "Render.shadow_map_dim");
    CreateShadowPass(&g_context->shadow_pass, &g_context->forward_pass,
                     dim, dim);
  }

  CreateBitmapRenderer(&g_context->quad_renderer);
  CreateFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, 512, 128);

  g_context->camera.z_near = 0.01f;
  g_context->camera.position = VEC3_CREATE(0.0f, 0.0f, -2.0f);
  g_context->camera.rotation = VEC3_CREATE(0.0f, 3.141f, 0.0f);
  g_context->camera.up = VEC3_CREATE(0.0f, 1.0f, 0.0f);
  g_context->camera.fovy = RADIANS(*GetVar_Float(g_config, "Camera.fovy"));
  g_context->camera.rotation_speed = *GetVar_Float(g_config, "Camera.rotation_speed");
  g_context->camera.movement_speed = *GetVar_Float(g_config, "Camera.movement_speed");

  g_context->prev_time = PlatformGetTicks();
  g_context->curr_time = g_context->prev_time;

  g_context->rect_pipeline = CreateEntity(&g_context->ecs);
  g_context->triangle_pipeline = CreateEntity(&g_context->ecs);
  g_context->voxel_pipeline = CreateEntity(&g_context->ecs);
  g_context->shadow_pipeline = CreateEntity(&g_context->ecs);
#define ADD_PIPELINE(pipeline, vertex_sh, fragment_sh, func) AddPipelineProgramComponent(&g_context->ecs, &g_context->asset_manager, g_context->pipeline, \
                                                                                         vertex_sh, fragment_sh, \
                                                                                         func, &g_context->deletion_queue)

  ADD_PIPELINE(rect_pipeline, "rect.vert.spv", "rect.frag.spv", CreateRectPipeline);
  ADD_PIPELINE(triangle_pipeline, "triangle.vert.spv", "triangle.frag.spv", CreateTrianglePipeline);
  ADD_PIPELINE(voxel_pipeline, "voxel.vert.spv", "voxel.frag.spv", CreateVoxelPipeline);
  ADD_PIPELINE(shadow_pipeline, "shadow_voxel.vert.spv", NULL, CreateShadowPipeline);
  ADD_PIPELINE(debug_pipeline, "debug_draw.vert.spv", "debug_draw.frag.spv", CreateDebugDrawPipeline);

  const uint32_t max_vertices = 1024*1024;
  const uint32_t max_draws = 32;
  CreateVoxelDrawer(&g_context->vox_drawer, &g_context->vox_allocator, max_vertices, max_draws);

  CreateDebugDrawer(&g_context->debug_drawer, 1024);

  InitScripts(&g_context->script_manager);

  g_context->arial_font = CreateEntity(&g_context->ecs);
  g_context->pixel_font = CreateEntity(&g_context->ecs);

  {
    // run CMD by hand. I know this looks ugly but it gets job done.
    const char* args[] = { GetVar_String(g_config, "Misc.initial_scene") };
    CMD_load_scene(1, args);
  }

  g_context->deletion_queue.left = 0;
  g_context->deletion_queue.count = 0;

  // keybindings
  g_context->root_keymap = (Keymap) { &RootKeymap_Pressed, NULL, NULL, NULL, NULL };
  g_context->camera_keymap = (Keymap) { &CameraKeymap_Pressed, &CameraKeymap_Released,
                                        &CameraKeymap_Mouse, NULL, &g_context->camera };
  BindKeymap(&g_context->root_keymap);

  InitConsole();
  g_console->font = g_context->pixel_font;

  g_context->shadow_cull.cull_mask = 1;
  g_context->main_cull.cull_mask = 2;

  // create pipelines
  BatchCreatePipelines();

  const char* shaders[] = { "vox_cull.comp.spv" };
  CreateComputePipelines(&g_context->compute_pipeline, 1, shaders, &g_context->compute_pipeline_layout);
}

void
EngineFree()
{
  PROFILE_FUNCTION();
  {
    FOREACH_COMPONENT(Voxel_Grid) {
      FreeVoxelGrid(&g_context->vox_allocator, &components[i]);
    }
  }

  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  DestroyDebugDrawer(&g_context->debug_drawer);

  DestroyVoxelDrawer(&g_context->vox_drawer, &g_context->vox_allocator);

  DestroyFontAtlas(&g_context->font_atlas);
  DestroyBitmapRenderer(&g_context->quad_renderer);

  {
    FOREACH_COMPONENT(Pipeline_Program) {
      vkDestroyPipeline(g_device->logical_device, components[i].pipeline, NULL);
    }
  }

#define X(a) UNREGISTER_COMPONENT(&g_context->ecs, a)
  X_ALL_COMPONENTS();
#undef X

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

  ProfilerSaveJSON("trace.json");

  PlatformFreeMemory(g_persistent_memory.ptr);
}

void
EngineUpdateAndRender()
{
  PROFILE_FUNCTION();
  // calculate time difference
  g_context->prev_time = g_context->curr_time;
  g_context->curr_time = PlatformGetTicks();
  const float dt = (g_context->curr_time - g_context->prev_time) / 1000.0f;

  switch (g_window->frame_counter & 31)
    {
    case 31:
      // get file notifications every 32 frame
      UpdateAssets(&g_context->asset_manager);
      g_profiler.enabled = *GetVar_Int(g_config, "Misc.profiling");
      g_context->camera.fovy = RADIANS(*GetVar_Float(g_config, "Camera.fovy"));
      g_context->camera.rotation_speed = *GetVar_Float(g_config, "Camera.rotation_speed");
      g_context->camera.movement_speed = *GetVar_Float(g_config, "Camera.movement_speed");
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
  Mat4_Mul(&camera->projection_matrix, &camera->view_matrix, &camera->projview_matrix);
  g_context->main_cull.camera_pos = g_context->camera.position;
  g_context->main_cull.camera_dir = g_context->camera.front;

  Scene_Data_Struct* sc_data = g_context->forward_pass.uniform_buffer_mapped;
  memcpy(&sc_data->camera_projection, &camera->projection_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_view, &camera->view_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_projview, &camera->projview_matrix, sizeof(Mat4));
  // sc_data->sun_dir = VEC3_CREATE(0.03f, 0.9f, 0.09f);
  sc_data->sun_dir = VEC3_CREATE(0.03f, 1.9f, 0.09f);
  sc_data->sun_ambient = 0.1f;
  Vec3_Normalize(&sc_data->sun_dir, &sc_data->sun_dir);

  Mat4 light_proj, light_view;
  {
    float extent = *GetVar_Float(g_config, "Render.shadow_extent");
    float near = *GetVar_Float(g_config, "Render.shadow_near");
    float far = *GetVar_Float(g_config, "Render.shadow_far");
    OrthographicMatrix(-extent, extent, -extent, extent, near, far, &light_proj);
    // Vec3 light_pos = { 0.0f, 10.0f, 0.0f };
    // Vec3 light_target = { 0.05f, 11.0f, 0.1f };
    Vec3 light_pos = VEC3_MUL(sc_data->sun_dir, 10.0f);
    Vec3 light_target = VEC3_ADD(light_pos, sc_data->sun_dir);
    Vec3 up = { 1.0f, 0.0f, 0.0f };
    LookAtMatrix(&light_pos, &light_target, &up, &light_view);
    g_context->shadow_cull.camera_pos = light_pos;
    Vec3_Normalize(&VEC3_MUL(light_target, -1.0f), &g_context->shadow_cull.camera_dir);
  }
  Mat4_Mul(&light_proj, &light_view, &sc_data->light_space);

  // run scripts
  {
    FOREACH_COMPONENT(Script) {
      if (g_window->frame_counter % components[i].frequency != 0)
        continue;
      components[i].func(&components[i], entities[i], dt);
    }
  }

  if (g_context->vox_drawer.vertex_offset >= g_context->vox_drawer.max_vertices * 9 / 10) {
    // reset cache when 90% of it is filled.
    // This is kind of dumb but it works.
    ClearVoxelDrawerCache(&g_context->vox_drawer);
  }
  NewVoxelDrawerFrame(&g_context->vox_drawer);
  NewDebugDrawerFrame(&g_context->debug_drawer);

  FOREACH_COMPONENT(Voxel_Grid) {
    Transform* transform = GetComponent(Transform, entities[i]);
    OBB* obb = GetComponent(OBB, entities[i]);
    // update OBB
    CalculateVoxelGridOBB(&components[i], transform, obb);
    // frustum culling
    // TODO(render): set cached's cull_mask to 0
    int cull_mask = 0;
    cull_mask |= TestFrustumOBB(&camera->projview_matrix, obb) * g_context->main_cull.cull_mask;
    cull_mask |= TestFrustumOBB(&sc_data->light_space, obb) * g_context->shadow_cull.cull_mask;
    if (cull_mask == 0)
      continue;
    // draw
    PushMeshToVoxelDrawer(&g_context->vox_drawer, &g_context->ecs, entities[i]);
    Voxel_Cached* cached = GetComponent(Voxel_Cached, entities[i]);
    cached->cull_mask = cull_mask;
    // draw wireframe
    int* opt = GetVar_Int(g_config, "Render.debug_voxel_obb");
    if (opt && *opt) {
      DebugDrawOBB(&g_context->debug_drawer, obb);
    }
  }

  enum {
    TIMESTAMP_SHADOW_PASS_BEGIN = 0,
    TIMESTAMP_FORWARD_PASS_BEGIN,
    TIMESTAMP_MAIN_PASS_BEGIN,
    TIMESTAMP_FRAME_END,
    TIMESTAMP_COUNT
  };

  VkCommandBuffer cmd = BeginCommands();
  uint64_t timestamps[TIMESTAMP_COUNT];
  VkQueryPool query_pool = GetTimestampsGPU(TIMESTAMP_COUNT, timestamps);

  if (g_window->frame_counter == 0) {
    Font* font = AddComponent(&g_context->ecs, Font, g_context->arial_font);
    LoadToFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, cmd, font, "Consolas.ttf", 32);
    font = AddComponent(&g_context->ecs, Font, g_context->pixel_font);
    LoadToFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, cmd, font, "pixel1.ttf", 16);
    FontAtlasEndLoading(&g_context->font_atlas, cmd);
  } else {
    NewBitmapFrame(&g_context->quad_renderer);
    DrawQuad(&g_context->quad_renderer, &VEC2_CREATE(0.04f, 0.36f), &VEC2_CREATE(0.3f, 0.05f), PACK_COLOR(23, 67, 240, 109), 1);
    Vec2 pos = { 0.04f, 0.4f };
    Vec2 text_size = { 0.05f, 0.05f };
    uint32_t color = PACK_COLOR(220, 119, 0, 205);
    Font* font = GetComponent(Font, g_context->arial_font);
    DrawText(&g_context->quad_renderer, font, "Banana", &text_size, color, &pos);
    pos = (Vec2) { 0.04f, 0.7f };
    text_size = (Vec2) { 0.05f, 0.05f };
    color = PACK_COLOR(5, 9, 0, 205);
    DrawText(&g_context->quad_renderer, font,
             GetVar_String(g_config, "Misc.some_string"), &text_size, color, &pos);

    // camera front
    // {
    //   pos = VEC2_CREATE(0.005f, 0.9f);
    //   text_size = (Vec2) { 0.025f, 0.025f };
    //   color = PACK_COLOR(255, 255, 255, 160);
    //   char buff[64];
    //   stbsp_sprintf(buff, "front=[%.3f, %.3f, %.3f]",
    //                 g_context->camera.front.x, g_context->camera.front.y, g_context->camera.front.z);
    //   DrawText(&g_context->quad_renderer, font, buff, &text_size, color, &pos);
    //   pos = VEC2_CREATE(0.005f, 0.95f);
    //   stbsp_sprintf(buff, "pos=[%.3f, %.3f, %.3f]",
    //                 g_context->camera.position.x, g_context->camera.position.y, g_context->camera.position.z);
    //   DrawText(&g_context->quad_renderer, font, buff, &text_size, color, &pos);
    // }

    // draw call info
    {
      pos = VEC2_CREATE(0.005f, 0.9f);
      text_size = (Vec2) { 0.025f, 0.025f };
      color = PACK_COLOR(255, 255, 255, 160);
      char buff[64];
      stbsp_sprintf(buff, "draw calls: %u", g_context->voxel_draw_calls);
      DrawText(&g_context->quad_renderer, font, buff, &text_size, color, &pos);
    }

    // GPU timestamps
    if (g_window->frame_counter > 2) {
      char buffer[256];
      char* buff = buffer;
      float period = g_device->properties.limits.timestampPeriod;
      const char* passes[] = {
        "shadow",
        "forward",
        "main"
      };
      for (uint32_t i = 0; i < ARR_SIZE(passes); i++) {
        // (queryResults[i+1] - queryResults[i]) * period / 1_000_000.0f;
        float time = (timestamps[i+1] - timestamps[i]) * period / 1000000.0f;
        buff += stbsp_sprintf(buff, "%s=%.3fms ", passes[i], time);
      }
      pos = VEC2_CREATE(0.005f, 0.95f);
      text_size = (Vec2) { 0.025f, 0.025f };
      color = PACK_COLOR(170, 255, 210, 160);
      DrawText(&g_context->quad_renderer, font, buffer, &text_size, color, &pos);
    }

    pos = (Vec2) { 0.7f, 0.02f };
    text_size = (Vec2) { 0.05f, 0.1f };
    color = PACK_COLOR(4, 59, 200, 254);
    DrawQuad(&g_context->quad_renderer, &pos, &text_size, color, 1);
    pos = (Vec2) { 0.7f, 0.14f };
    color = PACK_COLOR(4, 200, 59, 130);
    DrawQuad(&g_context->quad_renderer, &pos, &text_size, color, 1);
    UpdateAndDrawConsole(&g_context->quad_renderer, dt);
  }

  VkDescriptorSet ds_set;
  g_context->voxel_draw_calls = 0;

#if VX_USE_INDIRECT
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_context->compute_pipeline);
  MeshPass(cmd, &g_context->vox_drawer, &g_context->shadow_cull, g_context->compute_pipeline_layout);
  MeshPass(cmd, &g_context->vox_drawer, &g_context->main_cull, g_context->compute_pipeline_layout);
  // insert execution barrier
  vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                       0,
                       0, NULL,
                       0, NULL,
                       0, NULL);
#endif

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_SHADOW_PASS_BEGIN);
  // render to shadow map
  BeginShadowPass(&g_context->shadow_pass, cmd);
  {
    float depth_bias_constant = *GetVar_Float(g_config, "Render.depth_bias_constant");
    float depth_bias_slope = *GetVar_Float(g_config, "Render.depth_bias_slope");
    vkCmdSetDepthBias(cmd, depth_bias_constant, 0.0f, depth_bias_slope);
    Pipeline_Program* prog = GetComponent(Pipeline_Program, g_context->shadow_pipeline);
    ds_set = g_context->shadow_pass.scene_data_set;
    cmdBindProgram(cmd, prog, 1, &ds_set);
#if VX_USE_INDIRECT
    g_context->voxel_draw_calls += DrawVoxels(&g_context->vox_drawer, cmd, &g_context->shadow_cull);
#else
    g_context->voxel_draw_calls += DrawVoxels(&g_context->vox_drawer, cmd, &g_context->shadow_cull);
#endif
  }
  vkCmdEndRenderPass(cmd);

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_FORWARD_PASS_BEGIN);
  // render to offscreen buffer
  float clear_color[4] = { 0.08f, 0.2f, 0.25f, 1.0f };
  BeginForwardPass(&g_context->forward_pass, cmd, clear_color);
  {
    // draw triangles
    Pipeline_Program* prog = GetComponent(Pipeline_Program, g_context->triangle_pipeline);
    ds_set = g_context->forward_pass.scene_data_set;
    cmdBindProgram(cmd, prog, 1, &ds_set);
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
    prog = GetComponent(Pipeline_Program, g_context->voxel_pipeline);
    VkDescriptorSet ds_sets[] = { ds_set, g_context->shadow_pass.shadow_set };
    cmdBindProgram(cmd, prog, ARR_SIZE(ds_sets), ds_sets);
#if VX_USE_INDIRECT
    int i = 3;
    vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &i);
    g_context->voxel_draw_calls += DrawVoxels(&g_context->vox_drawer, cmd, &g_context->main_cull);
#else
    for (uint32_t i = 0; i < 6; i++) {
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &i);
      g_context->voxel_draw_calls += DrawVoxelsWithNormals(&g_context->vox_drawer, cmd, i,
                                                           &g_context->main_cull);
    }
#endif
    // draw debug lines
    prog = GetComponent(Pipeline_Program, g_context->debug_pipeline);
    // NOTE: forward_pass's descriptor set also has VK_SHADER_STAGE_FRAGMENT_BIT, it doesn't fit us
    ds_set = g_context->shadow_pass.scene_data_set;
    cmdBindProgram(cmd, prog, 1, &ds_set);
    RenderDebugLines(&g_context->debug_drawer, cmd);
  }
  vkCmdEndRenderPass(cmd);

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_MAIN_PASS_BEGIN);
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
    Pipeline_Program* prog = GetComponent(Pipeline_Program, g_context->rect_pipeline);
    cmdBindProgram(cmd, prog, 1, &ds_set);
    vkCmdDraw(cmd, 4, 1, 0, 0);

    // draw UI stuff
    RenderQuads(&g_context->quad_renderer, cmd);
  }
  vkCmdEndRenderPass(cmd);

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_FRAME_END);

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
  float xn = (float)xrel / g_window->swapchain_extent.width;
  float yn = (float)yrel / g_window->swapchain_extent.height;
  MouseMotion(x, y, xn, yn);
}

void
EngineTextInput(const char* text)
{
  TextInput(text);
}


/// keymaps

int
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
      // '0' or '`' popups up console
    case PlatformKey_0:
    case PlatformKey_BACKQUOTE:
      if (modkey_shift) {
        ShowConsoleBig();
      } else {
        ShowConsole();
      }
      break;
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
  return 0;
}

int
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
  return 0;
}

int
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
  return 0;
}

int
CameraKeymap_Mouse(int x, int y, float xrel, float yrel, void* udata)
{
  Camera* camera = udata;
  (void)x;
  (void)y;
  CameraRotate(camera, yrel, xrel, 0.0f);
  return 0;
}


/// commands

void
CMD_clear_scene(uint32_t num, const char** args)
{
  (void)args;
  if (num != 0) {
    LOG_WARN("command 'clear_scene' accepts no arguments; see 'info clear_scene'");
    return;
  }
  FOREACH_COMPONENT(Voxel_Grid) {
    FreeVoxelGrid(&g_context->vox_allocator, &components[i]);
    if (GetComponent(Script, entities[i])) {
      RemoveComponent(&g_context->ecs, Script, entities[i]);
    }
  }
  UNREGISTER_COMPONENT(&g_context->ecs, Voxel_Grid);
  UNREGISTER_COMPONENT(&g_context->ecs, Transform);
  UNREGISTER_COMPONENT(&g_context->ecs, OBB);
  UNREGISTER_COMPONENT(&g_context->ecs, Voxel_Cached);
  DestroyEmptyEntities(&g_context->ecs);
  ClearVoxelDrawerCache(&g_context->vox_drawer);
}

void
CMD_add_voxel(uint32_t num, const char** args)
{
  if (num != 5 && num != 4) {
    LOG_WARN("command 'add_voxel' accepts 4 arguments; see 'info add_voxel'");
    return;
  }
  EID entity = CreateEntity(&g_context->ecs);
  if (AddVoxelGridComponent(&g_context->ecs, &g_context->asset_manager, &g_context->vox_allocator,
                            entity, args[0]) == NULL) {
    return;
  }
  Transform* transform = AddComponent(&g_context->ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  // TODO(convenience): check for parse errors
  transform->position.x = strtof(args[1], NULL);
  transform->position.y = strtof(args[2], NULL);
  transform->position.z = strtof(args[3], NULL);
  if (num == 5) {
    transform->scale = strtof(args[4], NULL);
  } else {
    transform->scale = 1.0f;
  }
  AddComponent(&g_context->ecs, OBB, entity);
}

void
CMD_save_scene(uint32_t num, const char** args)
{
  if (num != 1) {
    LOG_WARN("command 'save_scene' accepts 1 argument; see 'info save_scene'");
    return;
  }
  SaveScene(&g_context->camera, args[0]);
}

void
CMD_load_scene(uint32_t num, const char** args)
{
  if (num != 1) {
    LOG_WARN("command 'load_scene' accepts 1 argument; see 'info load_scene'");
    return;
  }
  LoadScene(&g_context->ecs, &g_context->vox_allocator, &g_context->camera, &g_context->script_manager, args[0]);
}

void
CMD_make_voxel_rotate(uint32_t num, const char** args)
{
  if (num != 4) {
    LOG_WARN("command 'make_voxel_rotate' accepts 4 arguments; see 'info make_voxel_rotate'");
    return;
  }
  EID entity = atoi(args[0]);
  Script* script = AddComponent(&g_context->ecs, Script, entity);
  if (script == NULL) {
    script = GetComponent(Script, entity);
    LOG_WARN("entity %u already has script component '%s'", entity, script->name);
    return;
  }
  script->name = "rotate_voxel";
  script->func = GetScript(&g_context->script_manager, "rotate_voxel");
  script->arg0.float_32 = strtof(args[1], NULL);
  script->arg1.float_32 = strtof(args[2], NULL);
  script->arg2.float_32 = strtof(args[3], NULL);
  script->frequency = 1;
}

void
CMD_list_entities(uint32_t num, const char** args)
{
  (void)args;
  if (num > 0) {
    LOG_WARN("command 'list_entities' accepts no arguments; see 'info list_entities'");
    return;
  }
  uint32_t* entities = g_context->ecs.entities->ptr;
  for (EID eid = 0; eid < g_context->ecs.max_entities; eid++) {
    if (entities[eid] & ENTITY_ALIVE_MASK) {
      LOG_INFO("entity %u has %u components", eid, entities[eid]);
    }
  }
}

void
CMD_make_voxel_change(uint32_t num, const char** args)
{
  if (num > 2) {
    LOG_WARN("command 'make_voxel_rotate' accepts 1 argument; see 'info make_voxel_change'");
    return;
  }
  EID entity = atoi(args[0]);
  Script* script = AddComponent(&g_context->ecs, Script, entity);
  if (script == NULL) {
    script = GetComponent(Script, entity);
    LOG_WARN("entity %u already has script component '%s'", entity, script->name);
    return;
  }
  script->name = "change_voxel";
  script->func = GetScript(&g_context->script_manager, "change_voxel");
  if (num == 2) {
    script->frequency = atoi(args[1]);
  } else {
    script->frequency = 100;
  }
}

void
CMD_spawn_sphere(uint32_t num, const char** args)
{
  if (num != 1 && num != 4 && num != 7 && num != 8) {
    LOG_WARN("command 'spawn_sphere' accepts 1, 4, 7 or 8  arguments; see 'info spawn_sphere'");
    return;
  }
  EID entity = CreateEntity(&g_context->ecs);
  Voxel_Grid* grid = AddComponent(&g_context->ecs, Voxel_Grid, entity);
  int radius = atoi(args[0]);
  AllocateVoxelGrid(&g_context->vox_allocator, grid, radius*2+1, radius*2+1, radius*2+1);
  if (num == 1) {
    grid->palette[1] = PACK_COLOR(240, 240, 240, 255);
  } else {
    int r = atoi(args[1]);
    int g = atoi(args[2]);
    int b = atoi(args[3]);
    grid->palette[1] = PACK_COLOR(r, g, b, 255);
  }
  for (int z = 0; z < radius*2+1; z++)
    for (int y = 0; y < radius*2+1; y++)
      for (int x = 0; x < radius*2+1; x++) {
        int xr = abs(x-radius);
        int yr = abs(y-radius);
        int zr = abs(z-radius);
        if (xr*xr + yr*yr + zr*zr <= radius*radius+1) {
          GetInVoxelGrid(grid, x, y, z) = 1;
        }
      }
  // don't forget to update hash
  grid->hash = HashMemory64(grid->data->ptr, grid->width*grid->height*grid->depth);
  Transform* transform = AddComponent(&g_context->ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  if (num < 4) {
    transform->position.x = 0.0f;
    transform->position.y = 0.0f;
    transform->position.z = 0.0f;
    transform->scale = 1.0f;
  } else {
    transform->position.x = strtof(args[4], NULL);
    transform->position.y = strtof(args[5], NULL);
    transform->position.z = strtof(args[6], NULL);
    if (num == 8) {
      transform->scale = strtof(args[7], NULL);
    } else {
      transform->scale = 1.0f;
    }
  }
  AddComponent(&g_context->ecs, OBB, entity);
}

void
CMD_spawn_cube(uint32_t num, const char** args)
{
  if (num != 3 && num != 6 && num != 9 && num != 10) {
    LOG_WARN("command 'spawn_cube' accepts 3, 6, 9 or 10  arguments; see 'info spawn_cube'");
    return;
  }
  EID entity = CreateEntity(&g_context->ecs);
  uint32_t width = atoi(args[0]);
  uint32_t height = atoi(args[1]);
  uint32_t depth = atoi(args[2]);
  Voxel_Grid* grid = AddComponent(&g_context->ecs, Voxel_Grid, entity);
  AllocateVoxelGrid(&g_context->vox_allocator, grid, width, height, depth);
  if (num == 3) {
    grid->palette[1] = PACK_COLOR(240, 240, 240, 255);
  } else {
    int r = atoi(args[3]);
    int g = atoi(args[4]);
    int b = atoi(args[5]);
    grid->palette[1] = PACK_COLOR(r, g, b, 255);
  }
  memset(grid->data->ptr, 1, width*height*depth);
  // don't forget to update hash
  grid->hash = HashMemory64(grid->data->ptr, grid->width*grid->height*grid->depth);
  Transform* transform = AddComponent(&g_context->ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  if (num < 6) {
    transform->position.x = 0.0f;
    transform->position.y = 0.0f;
    transform->position.z = 0.0f;
    transform->scale = 1.0f;
  } else {
    transform->position.x = strtof(args[6], NULL);
    transform->position.y = strtof(args[7], NULL);
    transform->position.z = strtof(args[8], NULL);
    if (num == 8) {
      transform->scale = strtof(args[9], NULL);
    } else {
      transform->scale = 1.0f;
    }
  }
  AddComponent(&g_context->ecs, OBB, entity);
}

void
CMD_remove_script(uint32_t num, const char** args)
{
  if (num != 1) {
    LOG_WARN("command 'remove_script' accepts 1 argument; see 'info remove_script'");
    return;
  }
  EID entity = atoi(args[0]);
  RemoveComponent(&g_context->ecs, Script, entity);
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
    .cullMode = VK_CULL_MODE_NONE,
    // .cullMode = VK_CULL_MODE_FRONT_BIT,
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
  // NOTE: use depth bias < 0 because our depth is inverted
  static VkDynamicState dynamic_state = VK_DYNAMIC_STATE_DEPTH_BIAS;
  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_FRONT_BIT,
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

void CreateDebugDrawPipeline(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .line_width = 1.0f,
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
    .marker = "debug-draw/pipeline"
  };
  PipelineDebugDrawVertices(&description->vertex_attributes, &description->vertex_attribute_count,
                            &description->vertex_bindings, &description->vertex_binding_count);
}
