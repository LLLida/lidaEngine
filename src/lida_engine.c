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

#define LIDA_ENGINE_VERSION 202304
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
#include "lida_gen.c"
#include "lida_console.c"

typedef struct {

  Allocator     entity_allocator;
  Quad_Renderer quad_renderer;
  Font_Atlas    font_atlas;
  Debug_Drawer  debug_drawer;
  Keymap        root_keymap;
  Keymap        camera_keymap;

  EID shadow_camera;
  EID main_camera;

  // pipelines
  EID rect_pipeline;
  EID triangle_pipeline;
  EID debug_pipeline;
  EID depth_reduce_pipeline;

  // fonts
  EID arial_font;
  EID pixel_font;

  uint32_t prev_time;
  uint32_t curr_time;
  int      render_mode;
  uint32_t voxel_draw_calls;
  uint32_t debug_depth_pyramid;

} Engine_Context;

enum {
  RENDER_MODE_DEFAULT,
  RENDER_MODE_SHADOW_MAP,
  RENDER_MODE_DEPTH_PYRAMID,
  RENDER_MODE_COUNT,
};

GLOBAL Engine_Context* g_context;

// simple X macro to do operation on all components
#define X_ALL_COMPONENTS()                      \
  X(Voxel_Grid);                                \
  X(Transform);                                 \
  X(Voxel_View);                                \
  X(OBB);                                       \
  X(Camera);                                    \
  X(Graphics_Pipeline);                         \
  X(Compute_Pipeline);                          \
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
INTERNAL void CreateDebugDrawPipeline(Pipeline_Desc* description);

void
EngineInit(const Engine_Startup_Info* info)
{
  const size_t total_memory = 128 * 1024 * 1024;
  InitMemoryChunk(&g_persistent_memory, PlatformAllocateMemory(total_memory), total_memory);

  ProfilerStart();
  PROFILE_FUNCTION();

  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME };
  CreateDevice(info->enable_debug_layers,
               info->gpu_id,
               info->app_name, info->app_version,
               device_extensions, ARR_SIZE(device_extensions));
  CreateWindow(info->window_vsync);

  g_context = PersistentAllocate(sizeof(Engine_Context));
#define INIT_ALLOCATOR(alloc, mb) InitAllocator(alloc, MemoryAllocateRight(&g_persistent_memory, mb * 1024 * 1024), 1024*1024*mb)
  INIT_ALLOCATOR(&g_context->entity_allocator, 20);

  // 96 Mb for voxels
  g_vox_allocator = PersistentAllocate(sizeof(Allocator));
  INIT_ALLOCATOR(g_vox_allocator, 96);

  g_ecs = PersistentAllocate(sizeof(ECS));
  CreateECS(&g_context->entity_allocator, g_ecs, 8);

  // register all components
#define X(a) REGISTER_COMPONENT(a)
  X_ALL_COMPONENTS();
#undef X

  g_asset_manager = PersistentAllocate(sizeof(Asset_Manager));
  InitAssetManager(g_asset_manager);

  g_config = CreateConfig(g_ecs, g_asset_manager,
                          CreateEntity(g_ecs), "variables.ini");
  g_profiler.enabled = *GetVar_Int(g_config, "Misc.profiling");

  g_forward_pass = PersistentAllocate(sizeof(Forward_Pass));
  CreateForwardPass(g_forward_pass,
                    g_window->swapchain_extent.width, g_window->swapchain_extent.height);

  {
    // TODO: check for null
    int dim = *GetVar_Int(g_config, "Render.shadow_map_dim");
    g_shadow_pass = PersistentAllocate(sizeof(Shadow_Pass));
    CreateShadowPass(g_shadow_pass, g_forward_pass,
                     dim, dim);
  }

  CreateBitmapRenderer(&g_context->quad_renderer);
  CreateFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, 512, 128);

  g_context->prev_time = PlatformGetTicks();
  g_context->curr_time = g_context->prev_time;

  g_random = PersistentAllocate(sizeof(Random_State));
  SeedRandom(g_random, 420, 420);

  const uint32_t max_vertices = 16*1024*1024;
  const uint32_t max_draws = 10*1024;
  g_vox_drawer = PersistentAllocate(sizeof(Voxel_Drawer));
  CreateVoxelDrawer(g_vox_drawer, max_vertices, max_draws);

  g_context->rect_pipeline = CreateEntity(g_ecs);
  g_context->triangle_pipeline = CreateEntity(g_ecs);
#define ADD_PIPELINE(pipeline, vertex_sh, fragment_sh, func) AddGraphicsPipelineComponent(g_ecs, g_asset_manager, pipeline, \
                                                                                          vertex_sh, fragment_sh, \
                                                                                          func, g_deletion_queue)

  ADD_PIPELINE(g_context->rect_pipeline, "rect.vert.spv", "rect.frag.spv", CreateRectPipeline);
  ADD_PIPELINE(g_context->triangle_pipeline, "triangle.vert.spv", "triangle.frag.spv", CreateTrianglePipeline);
  ADD_PIPELINE(g_voxel_pipeline_colored, "voxel_new.vert.spv", "voxel.frag.spv", CreateVoxelPipelineIndirect);
  ADD_PIPELINE(g_voxel_pipeline_shadow, "shadow_voxel.vert.spv", NULL, CreateVoxelPipelineShadow);
  ADD_PIPELINE(g_context->debug_pipeline, "debug_draw.vert.spv", "debug_draw.frag.spv", CreateDebugDrawPipeline);
#undef ADD_PIPELINE

#define ADD_PIPELINE(pipeline, shader) AddComputePipelineComponent(g_ecs, g_asset_manager, pipeline, \
                                                                   shader, g_deletion_queue)
  ADD_PIPELINE(g_voxel_pipeline_compute_ortho, "vox_cull_ortho.comp.spv");
  ADD_PIPELINE(g_voxel_pipeline_compute_persp, "vox_cull_persp.comp.spv");
  ADD_PIPELINE(g_voxel_pipeline_compute_ext_ortho, "vox_cull_ext_ortho.comp.spv");
  ADD_PIPELINE(g_voxel_pipeline_compute_ext_persp, "vox_cull_ext_persp.comp.spv");
  g_context->depth_reduce_pipeline = CreateEntity(g_ecs);
  ADD_PIPELINE(g_context->depth_reduce_pipeline, "depth_reduce.comp.spv");

  // CreateDebugDrawer(&g_context->debug_drawer, 1024);
  CreateDebugDrawer(&g_context->debug_drawer, 128*1024);

  g_script_manager = PersistentAllocate(sizeof(Script_Manager));
  InitScripts(g_script_manager);

  g_context->arial_font = CreateEntity(g_ecs);
  g_context->pixel_font = CreateEntity(g_ecs);

  g_deletion_queue = PersistentAllocate(sizeof(Deletion_Queue));
  g_deletion_queue->left = 0;
  g_deletion_queue->count = 0;


  {
    g_context->shadow_camera = CreateEntity(g_ecs);
    Camera* camera = AddComponent(g_ecs, Camera, g_context->shadow_camera);
    camera->cull_mask = 1;
    camera->type = CAMERA_TYPE_ORTHO;
  }
  {
    g_context->main_camera = CreateEntity(g_ecs);
    Camera* camera = AddComponent(g_ecs, Camera, g_context->main_camera);
    camera->cull_mask = 2;
    camera->type = CAMERA_TYPE_PERSP;

    camera->z_near = 1.0f;
    camera->position = VEC3_CREATE(0.0f, 0.0f, -2.0f);
    camera->rotation = VEC3_CREATE(0.0f, 3.141f, 0.0f);
    camera->up = VEC3_CREATE(0.0f, 1.0f, 0.0f);
    camera->fovy = RADIANS(*GetVar_Float(g_config, "Camera.fovy"));
    camera->rotation_speed = *GetVar_Float(g_config, "Camera.rotation_speed");
    camera->movement_speed = *GetVar_Float(g_config, "Camera.movement_speed");

    g_context->camera_keymap = (Keymap) { &CameraKeymap_Pressed, &CameraKeymap_Released,
                                          &CameraKeymap_Mouse, NULL, camera };
  }

  // keybindings
  g_context->root_keymap = (Keymap) { &RootKeymap_Pressed, NULL, NULL, NULL, NULL };
  BindKeymap(&g_context->root_keymap);

  InitConsole();
  g_console->font = g_context->pixel_font;

  if (1) {
    // run CMD by hand. I know this looks ugly but it gets job done.
    const char* args[] = { GetVar_String(g_config, "Misc.initial_scene") };
    if (args[0])
      CMD_load_scene(1, args);
  }

  // TODO: for some reason this line of code causes our app to crash
  // FixFragmentation(&g_context->entity_allocator);

  // create pipelines
  BatchCreateGraphicsPipelines();
  BatchCreateComputePipelines();
}

void
EngineFree()
{
  PROFILE_FUNCTION();
  {
    FOREACH_COMPONENT(Voxel_Grid) {
      FreeVoxelGrid(g_vox_allocator, &components[i]);
    }
  }

  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  DestroyDebugDrawer(&g_context->debug_drawer);

  DestroyVoxelDrawer(g_vox_drawer);

  DestroyFontAtlas(&g_context->font_atlas);
  DestroyBitmapRenderer(&g_context->quad_renderer);

  {
    FOREACH_COMPONENT(Graphics_Pipeline) {
      vkDestroyPipeline(g_device->logical_device, components[i].pipeline, NULL);
    }
  }

  {
    FOREACH_COMPONENT(Compute_Pipeline) {
      vkDestroyPipeline(g_device->logical_device, components[i].pipeline, NULL);
    }
  }

#define X(a) UNREGISTER_COMPONENT(g_ecs, a)
  X_ALL_COMPONENTS();
#undef X

  DestroyECS(g_ecs);

  if (ReleaseAllocator(g_vox_allocator)) {
    LOG_WARN("vox: memory leak detected");
  }

  if (ReleaseAllocator(&g_context->entity_allocator)) {
    LOG_WARN("entity: memory leak detected");
    DebugListAllocations(&g_context->entity_allocator);
  }

  // FreeAssetManager(g_asset_manager);

  DestroyShadowPass(g_shadow_pass);

  DestroyForwardPass(g_forward_pass);

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
      UpdateAssets(g_asset_manager);
      g_profiler.enabled               = *GetVar_Int(g_config, "Misc.profiling");
      {
        Camera* camera         = GetComponent(Camera, g_context->main_camera);
        camera->fovy           = RADIANS(*GetVar_Float(g_config, "Camera.fovy"));
        camera->rotation_speed = *GetVar_Float(g_config, "Camera.rotation_speed");
        camera->movement_speed = *GetVar_Float(g_config, "Camera.movement_speed");
      }

      uint32_t new_shadow_map_dim = *GetVar_Int(g_config, "Render.shadow_map_dim");
      if (new_shadow_map_dim != g_shadow_pass->extent.width) {
        // recreate shadow map if extent is changed
        RecreateShadowPass(g_shadow_pass, g_deletion_queue, new_shadow_map_dim);
        // need to recreate shadow pipeline
        Graphics_Pipeline* pipeline = GetComponent(Graphics_Pipeline, g_voxel_pipeline_shadow);
        AddForDeletion(g_deletion_queue, (uint64_t)pipeline->pipeline, VK_OBJECT_TYPE_PIPELINE);
        Pipeline_Desc desc;
        pipeline->create_func(&desc);
        desc.vertex_shader = pipeline->vertex_shader;
        CreateGraphicsPipelines(&pipeline->pipeline, 1, &desc, &pipeline->layout);
      }

      break;

    case 30:
      // destroy objects needed to be destroyed
      UpdateDeletionQueue(g_deletion_queue);
      break;
    }

  // update camera position, rotation etc.
  Camera* camera         = GetComponent(Camera, g_context->main_camera);
  CameraUpdate(camera, dt, g_window->swapchain_extent.width, g_window->swapchain_extent.height);
  CameraUpdateProjection(camera);
  CameraUpdateView(camera);
  Mat4_Mul(&camera->projection_matrix, &camera->view_matrix, &camera->projview_matrix);

  Scene_Data_Struct* sc_data = g_forward_pass->uniform_buffer_mapped;
  memcpy(&sc_data->camera_projection, &camera->projection_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_view, &camera->view_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_projview, &camera->projview_matrix, sizeof(Mat4));
  memcpy(&sc_data->camera_pos, &camera->position, sizeof(Vec3));
  sc_data->sun_dir = VEC3_CREATE(0.03f, 1.9f, 0.09f);
  sc_data->sun_ambient = 0.1f;
  Vec3_Normalize(&sc_data->sun_dir, &sc_data->sun_dir);

  {
    Mat4 light_proj, light_view;
    float extent = *GetVar_Float(g_config, "Render.shadow_extent");
    float near   = *GetVar_Float(g_config, "Render.shadow_near");
    float far    = *GetVar_Float(g_config, "Render.shadow_far");
    OrthographicMatrix(-extent, extent, -extent, extent, near, far, &light_proj);
    // Vec3 light_pos = { 0.0f, 10.0f, 0.0f };
    // Vec3 light_target = { 0.05f, 11.0f, 0.1f };
    Vec3 light_pos    = VEC3_MUL(sc_data->sun_dir, 20.0f);
    Vec3 light_target = VEC3_ADD(light_pos, sc_data->sun_dir);
    Vec3 up           = { 1.0f, 0.0f, 0.0f };
    LookAtMatrix(&light_pos, &light_target, &up, &light_view);
    Mat4_Mul(&light_proj, &light_view, &sc_data->light_space);
    {
      Camera* shadow_camera = GetComponent(Camera, g_context->shadow_camera);
      memcpy(&shadow_camera->projview_matrix, &sc_data->light_space, sizeof(Mat4));
      shadow_camera->position = light_pos;
      Vec3_Normalize(&VEC3_MUL(light_target, -1.0f), &shadow_camera->front);
    }
  }

  // run scripts
  {
    FOREACH_COMPONENT(Script) {
      if (g_window->frame_counter % components[i].frequency != 0)
        continue;
      components[i].func(&components[i], entities[i], dt);
    }
  }

  // TODO: make this work
  // if (g_vox_drawer->vertex_offset >= g_vox_drawer->max_vertices * 9 / 10) {
  //   // reset cache when 90% of it is filled.
  //   // This is kind of dumb but it works.
  //   ClearVoxelDrawerCache(g_vox_drawer);
  // }
  NewVoxelDrawerFrame(g_vox_drawer);
  NewDebugDrawerFrame(&g_context->debug_drawer);

  FOREACH_COMPONENT(Voxel_View) {
    Transform* transform = GetComponent(Transform, entities[i]);
    OBB* obb = GetComponent(OBB, entities[i]);
    Voxel_Grid* grid = GetComponent(Voxel_Grid, components[i].grid);
    // update OBB
    CalculateVoxelGridOBB(grid, transform, obb);
    // frustum culling
    int cull_mask = 0;
    {
      FOREACH_COMPONENT(Camera) {
        cull_mask |= TestFrustumOBB(&components[i].projview_matrix, obb) * components[i].cull_mask;
      }
    }
    if (cull_mask == 0)
      continue;
    // draw
    PushMeshToVoxelDrawer(g_vox_drawer, entities[i]);
    Voxel_View* cached = GetComponent(Voxel_View, entities[i]);
    cached->cull_mask = cull_mask;
    // draw wireframe
    int* opt = GetVar_Int(g_config, "Render.debug_voxel_obb");
    if (opt && *opt) {
      DebugDrawOBB(&g_context->debug_drawer, obb);
      /* DebugDrawVoxelBlocks(&g_context->debug_drawer, grid, obb); */
    }
  }

  // draw axes
  AddDebugLine(&g_context->debug_drawer, &VEC3_CREATE(0.0, 0.0, 0.0), &VEC3_CREATE(3.0, 0.0, 0.0), PACK_COLOR(255, 0, 0, 255));
  AddDebugLine(&g_context->debug_drawer, &VEC3_CREATE(0.0, 0.0, 0.0), &VEC3_CREATE(0.0, 3.0, 0.0), PACK_COLOR(0, 255, 0, 255));
  AddDebugLine(&g_context->debug_drawer, &VEC3_CREATE(0.0, 0.0, 0.0), &VEC3_CREATE(0.0, 0.0, 3.0), PACK_COLOR(0, 0, 255, 255));

  enum {
    TIMESTAMP_SHADOW_PASS_BEGIN = 0,
    // TIMESTAMP_CULL_PASS_BEGIN, // TODO: measure culling time
    TIMESTAMP_FORWARD_PASS_BEGIN,
    TIMESTAMP_MAIN_PASS_BEGIN,
    TIMESTAMP_FRAME_END,
    TIMESTAMP_COUNT
  };

  VkCommandBuffer cmd = BeginCommands();
  uint64_t timestamps[TIMESTAMP_COUNT];
  VkQueryPool query_pool = GetTimestampsGPU(TIMESTAMP_COUNT, timestamps);

  cmdResetPipelineStats(&g_vox_drawer->pipeline_stats_fragment, cmd);
  cmdResetPipelineStats(&g_vox_drawer->pipeline_stats_shadow,   cmd);

  if (g_window->frame_counter == 0) {
    Font* font = AddComponent(g_ecs, Font, g_context->arial_font);
    LoadToFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, cmd, font, "Consolas.ttf", 32);
    font = AddComponent(g_ecs, Font, g_context->pixel_font);
    LoadToFontAtlas(&g_context->quad_renderer, &g_context->font_atlas, cmd, font, "pixel1.ttf", 16);
    FontAtlasEndLoading(&g_context->font_atlas, cmd);
  } else {
    NewBitmapFrame(&g_context->quad_renderer);
    Vec2 pos = { 0.04f, 0.4f };
    Vec2 text_size = { 0.05f, 0.05f };
    uint32_t color = PACK_COLOR(220, 119, 0, 205);
    Font* font = GetComponent(Font, g_context->arial_font);

    // draw call info
    {
      pos = VEC2_CREATE(0.005f, 0.9f);
      text_size = (Vec2) { 0.025f, 0.025f };
      color = PACK_COLOR(255, 255, 255, 160);
      char buff[64];
      stbsp_sprintf(buff, "draw calls: %u", g_context->voxel_draw_calls);
      DrawText(&g_context->quad_renderer, font, buff, &text_size, color, &pos);
    }
    // draw rects for debugging occlusion culling
    if (*GetVar_Int(g_config, "Render.debug_ss_aabb") == 1) {
      FOREACH_COMPONENT(Voxel_View) {
        if ((components[i].cull_mask & 2) == 0)
          continue;
        Transform* transform = GetComponent(Transform, entities[i]);
        OBB* obb = GetComponent(OBB, entities[i]);
        float radius2 = 0.0f;
        for (int i = 0; i < 8; i++) {
          Vec3 diff = VEC3_SUB(obb->corners[i], transform->position);
          float d = VEC3_DOT(diff, diff);
          if (d > radius2) {
            radius2 = d;
          }
        }
        Vec3 dist = VEC3_SUB(transform->position, camera->position);
        float tsquared = VEC3_DOT(dist, dist);
        if (tsquared <= radius2)
          continue;
        Vec4 rect = { 1.0f, 1.0f, -1.0f, -1.0f };
        for (int i = 0; i < 8; i++) {
          Vec4 pos = { obb->corners[i].x, obb->corners[i].y, obb->corners[i].z, 1.0f };
          Mat4_Mul_Vec4(&camera->projview_matrix, &pos, &pos);
          pos.x /= pos.w;
          pos.y /= pos.w;
          if (pos.x < rect.x) rect.x = pos.x;
          if (pos.y < rect.y) rect.y = pos.y;
          if (pos.x > rect.z) rect.z = pos.x;
          if (pos.y > rect.w) rect.w = pos.y;
        }
        DrawQuad(&g_context->quad_renderer,
                 &VEC2_CREATE(rect.x*0.5+0.5, rect.y*0.5+0.5),
                 &VEC2_CREATE(0.5*(rect.z-rect.x), 0.5*(rect.w-rect.y)),
                 PACK_COLOR(120, 180, 120, 50), 1);
        if (i == 200)
          break;
      }
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
        float time = (timestamps[i+1] - timestamps[i]) * period / 1000000.0f;
        buff += stbsp_sprintf(buff, "%s=%.3fms ", passes[i], time);
      }
      pos = VEC2_CREATE(0.005f, 0.95f);
      text_size = (Vec2) { 0.025f, 0.025f };
      color = PACK_COLOR(170, 255, 210, 160);
      DrawText(&g_context->quad_renderer, font, buffer, &text_size, color, &pos);
    }

    UpdateAndDrawConsole(&g_context->quad_renderer, dt);
  }

  VkDescriptorSet ds_set;
  g_context->voxel_draw_calls = 0;

  {
    Compute_Pipeline* pip = GetComponent(Compute_Pipeline, g_context->depth_reduce_pipeline);
    DepthReductionPass(&g_forward_pass->depth_pyramid, cmd, pip,
                       g_forward_pass->render_extent.width, g_forward_pass->render_extent.height);
  }

  CullPass(cmd, g_vox_drawer, ComponentData(Camera), ComponentCount(Camera));

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_SHADOW_PASS_BEGIN);
  // render to shadow map
  BeginShadowPass(g_shadow_pass, cmd);
  {
    cmdBeginPipelineStats(&g_vox_drawer->pipeline_stats_shadow, cmd);
    float depth_bias_constant = *GetVar_Float(g_config, "Render.depth_bias_constant");
    float depth_bias_slope = *GetVar_Float(g_config, "Render.depth_bias_slope");
    vkCmdSetDepthBias(cmd, depth_bias_constant, 0.0f, depth_bias_slope);
    g_context->voxel_draw_calls += DrawVoxels(g_vox_drawer, cmd, GetComponent(Camera, g_context->shadow_camera),
                                              1, &g_shadow_pass->scene_data_set);
    cmdEndPipelineStats(&g_vox_drawer->pipeline_stats_shadow, cmd);
  }
  vkCmdEndRenderPass(cmd);

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_FORWARD_PASS_BEGIN);
  // render scene to offscreen framebuffer
  float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  clear_color[0] = *GetVar_Float(g_config, "Render.bg_fill_color_r");
  clear_color[1] = *GetVar_Float(g_config, "Render.bg_fill_color_g");
  clear_color[2] = *GetVar_Float(g_config, "Render.bg_fill_color_b");
  BeginForwardPass(g_forward_pass, cmd, clear_color);
  {
    // draw triangles
    Graphics_Pipeline* prog = GetComponent(Graphics_Pipeline, g_context->triangle_pipeline);
    ds_set = g_forward_pass->scene_data_set;
    cmdBindGraphics(cmd, prog, 1, &ds_set);
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
    cmdBeginPipelineStats(&g_vox_drawer->pipeline_stats_fragment, cmd);
    VkDescriptorSet ds_sets[] = { ds_set, g_shadow_pass->shadow_set };
    g_context->voxel_draw_calls += DrawVoxels(g_vox_drawer, cmd, GetComponent(Camera, g_context->main_camera),
                                              ARR_SIZE(ds_sets), ds_sets);
    cmdEndPipelineStats(&g_vox_drawer->pipeline_stats_fragment, cmd);
    // draw debug lines
    prog = GetComponent(Graphics_Pipeline, g_context->debug_pipeline);
    // NOTE: forward_pass's descriptor set has VK_SHADER_STAGE_FRAGMENT_BIT, it doesn't fit us
    ds_set = g_shadow_pass->scene_data_set;
    cmdBindGraphics(cmd, prog, 1, &ds_set);
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
      ds_set = g_forward_pass->resulting_image_set;
      break;
    case RENDER_MODE_SHADOW_MAP:
      ds_set = g_shadow_pass->shadow_set;
      break;
    case RENDER_MODE_DEPTH_PYRAMID:
      ds_set = g_forward_pass->depth_pyramid.debug_sets[g_context->debug_depth_pyramid];
      break;
    default:
      Assert(0);
    }
    Graphics_Pipeline* prog = GetComponent(Graphics_Pipeline, g_context->rect_pipeline);
    cmdBindGraphics(cmd, prog, 1, &ds_set);
    vkCmdDraw(cmd, 4, 1, 0, 0);

    // draw UI stuff
    RenderQuads(&g_context->quad_renderer, cmd);
  }
  vkCmdEndRenderPass(cmd);

  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                      TIMESTAMP_FRAME_END);

  // end of frame
  vkEndCommandBuffer(cmd);

  SendForwardPassData(g_forward_pass);

  VkResult err = PresentToScreen();
  if (err == VK_SUBOPTIMAL_KHR) {
    // resize render attachments
    vkDeviceWaitIdle(g_device->logical_device);
    ResizeWindow();
    ResetDynamicDescriptorSets();
    ResizeForwardPass(g_forward_pass,
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
      // '5' prints camera stats
    case PlatformKey_5:
      {
        Camera* camera = GetComponent(Camera, g_context->main_camera);
        LOG_INFO("Camera.position=[%.3f %.3f %.3f]", camera->position.x, camera->position.y, camera->position.z);
        LOG_INFO("Camera.front=[%.3f %.3f %.3f]", camera->front.x, camera->front.y, camera->front.z);
        // TODO: print other fields such as fovy, etc.
      }break;
      // '7' shrinks memory
    case PlatformKey_7:
      {
        uint32_t s = FixFragmentation(g_vox_allocator);
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
    case PlatformKey_O:
      // SHIFT-O cicles through depth pyramid
      if (modkey_shift) {
        g_context->debug_depth_pyramid = (g_context->debug_depth_pyramid + 1) % g_forward_pass->depth_pyramid.num_mips;
      }
      break;
      // ALT-C goes to camera mode
    case PlatformKey_C:
      if (modkey_alt) {
        BindKeymap(&g_context->camera_keymap);
        PlatformHideCursor();
      }
      break;
    case PlatformKey_S:
      // ALT-S spawns 10 random voxel models. This is to test how good
      // is our mesh generation.
      if (modkey_alt) {
        const char* args = "10";
        CMD_spawn_random_vox_models(1, &args);
      }

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
CMD_save_scene(uint32_t num, const char** args)
{
  if (num != 1) {
    LOG_WARN("command 'save_scene' accepts 1 argument; see 'info save_scene'");
    return;
  }
  Camera* camera = GetComponent(Camera, g_context->main_camera);
  SaveScene(camera, args[0]);
}

void
CMD_load_scene(uint32_t num, const char** args)
{
  if (num != 1) {
    LOG_WARN("command 'load_scene' accepts 1 argument; see 'info load_scene'");
    return;
  }
  Camera* camera = GetComponent(Camera, g_context->main_camera);
  LoadScene(g_ecs, g_vox_allocator, camera, g_script_manager, args[0]);
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
    .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    .polygonMode         = VK_POLYGON_MODE_FILL,
    .cullMode            = VK_CULL_MODE_NONE,
    .depth_bias_enable   = VK_FALSE,
    .blend_logic_enable  = VK_FALSE,
    .attachment_count    = 1,
    .attachments         = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states      = dynamic_states,
    .render_pass         = g_window->render_pass,
    .subpass             = 0,
    .marker              = "blit-3D-scene-fullscreen"
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
    .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode         = VK_POLYGON_MODE_FILL,
    .cullMode            = VK_CULL_MODE_NONE,
    .depth_bias_enable   = VK_FALSE,
    .depth_test          = VK_TRUE,
    .depth_write         = VK_TRUE,
    .depth_compare_op    = VK_COMPARE_OP_GREATER,
    .blend_logic_enable  = VK_FALSE,
    .attachment_count    = 1,
    .attachments         = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states      = dynamic_states,
    .render_pass         = g_forward_pass->render_pass,
    .subpass             = 0,
    .marker              = "draw-triangle-pipeline"
  };
}

void CreateDebugDrawPipeline(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  *description = (Pipeline_Desc) {
    .topology            = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    .polygonMode         = VK_POLYGON_MODE_FILL,
    .cullMode            = VK_CULL_MODE_NONE,
    .line_width          = 1.0f,
    .depth_bias_enable   = VK_FALSE,
    .depth_test          = VK_TRUE,
    .depth_write         = VK_TRUE,
    .depth_compare_op    = VK_COMPARE_OP_GREATER,
    .blend_logic_enable  = VK_FALSE,
    .attachment_count    = 1,
    .attachments         = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states      = dynamic_states,
    .render_pass         = g_forward_pass->render_pass,
    .subpass             = 0,
    .marker              = "debug-draw/pipeline"
  };
  PipelineDebugDrawVertices(&description->vertex_attributes, &description->vertex_attribute_count,
                            &description->vertex_bindings, &description->vertex_binding_count);
}
