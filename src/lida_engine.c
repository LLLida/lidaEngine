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
#include "lida_algebra.c"
#include "lida_render.c"
#include "lida_voxel.c"

typedef struct {

  Allocator allocator;
  Forward_Pass forward_pass;
  Camera camera;
  Voxel_Drawer vox_drawer;
  VkPipelineLayout rect_pipeline_layout;
  VkPipeline rect_pipeline;
  VkPipelineLayout triangle_pipeline_layout;
  VkPipeline triangle_pipeline;
  VkPipelineLayout voxel_pipeline_layout;
  VkPipeline voxel_pipeline;
  uint32_t prev_time;
  uint32_t curr_time;
  int mouse_mode;

} Engine_Context;

GLOBAL Engine_Context* g_context;

GLOBAL Voxel_Grid grid_1;
GLOBAL Voxel_Grid grid_2;


/// Engine general functions

INTERNAL void CreateRectPipeline();
INTERNAL void CreateTrianglePipeline();
INTERNAL void CreateVoxelPipeline();

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
  InitAllocator(&g_context->allocator, MemoryAllocateRight(&g_persistent_memory, 4 * 1024 * 1024), 4*1024*1024);

  CreateForwardPass(&g_context->forward_pass,
                    g_window->swapchain_extent.width, g_window->swapchain_extent.height,
                    VK_SAMPLE_COUNT_4_BIT);

  CreateRectPipeline();
  CreateTrianglePipeline();
  CreateVoxelPipeline();

  g_context->camera.z_near = 0.01f;
  g_context->camera.position = VEC3_CREATE(0.0f, 0.0f, -2.0f);
  g_context->camera.rotation = VEC3_CREATE(0.0f, 3.141f, 0.0f);
  g_context->camera.up = VEC3_CREATE(0.0f, 1.0f, 0.0f);
  g_context->camera.fovy = RADIANS(45.0f);
  g_context->camera.rotation_speed = 0.005f;
  g_context->camera.movement_speed = 1.0f;

  PlatformHideCursor();
  g_context->mouse_mode = 1;

  g_context->prev_time = PlatformGetTicks();
  g_context->curr_time = g_context->prev_time;

  CreateVoxelDrawer(&g_context->vox_drawer, 128*1024, 32);

  LoadVoxelGridFromFile(&g_context->allocator, &grid_1, "../assets/3x3x3.vox");
  Allocation* some_allocation = DoAllocation(&g_context->allocator, 6969);
  LoadVoxelGridFromFile(&g_context->allocator, &grid_2, "../assets/chr_beau.vox");
  Allocation* other_allocation = DoAllocation(&g_context->allocator, 1337);
  FreeAllocation(&g_context->allocator, some_allocation);
  FreeAllocation(&g_context->allocator, other_allocation);
}

void
EngineFree()
{
  FreeVoxelGrid(&g_context->allocator, &grid_1);
  FreeVoxelGrid(&g_context->allocator, &grid_2);

  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  if (ReleaseAllocator(&g_context->allocator)) {
    LOG_WARN("memory leak detected");
  }

  DestroyVoxelDrawer(&g_context->vox_drawer);

  vkDestroyPipeline(g_device->logical_device, g_context->voxel_pipeline, NULL);
  vkDestroyPipeline(g_device->logical_device, g_context->triangle_pipeline, NULL);
  vkDestroyPipeline(g_device->logical_device, g_context->rect_pipeline, NULL);

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

  Transform transform = {
    .rotation = QUAT_IDENTITY(),
    .position = VEC3_CREATE(3.1f, 2.6f, 1.0f),
    .scale = 0.9f,
  };
  PushMeshToVoxelDrawer(&g_context->vox_drawer, &grid_1, &transform);
  transform.position = VEC3_CREATE(-1.1f, -1.6f, 7.0f);
  transform.scale = 0.09f;
  PushMeshToVoxelDrawer(&g_context->vox_drawer, &grid_2, &transform);

  VkCommandBuffer cmd = BeginCommands();
  VkDescriptorSet ds_set;

  Vec4 colors[] = {
    VEC4_CREATE(1.0f, 0.2f, 0.2f, 1.0f),
    VEC4_CREATE(0.0f, 0.9f, 0.4f, 1.0f),
    VEC4_CREATE(0.2f, 0.35f, 0.76f, 1.0f),
    VEC4_CREATE(0.0f, 0.0f, 0.0f, 0.0f)
  };
  // render to offscreen buffer
  float clear_color[4] = { 0.08f, 0.2f, 0.25f, 1.0f };
  BeginForwardPass(&g_context->forward_pass, cmd, clear_color);
  {
    // draw triangles
    ds_set = g_context->forward_pass.scene_data_set;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_context->triangle_pipeline_layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_context->triangle_pipeline);
    // 1st draw
    vkCmdPushConstants(cmd, g_context->triangle_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Vec4)*3 + sizeof(Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // 2nd draw
    colors[2] = VEC4_CREATE(0.1f, 0.3f, 1.0f, 0.0f);
    vkCmdPushConstants(cmd, g_context->triangle_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Vec4)*3 + sizeof(Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // draw voxels
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_context->voxel_pipeline_layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_context->voxel_pipeline);
    for (uint32_t i = 0; i < 6; i++) {
      vkCmdPushConstants(cmd, g_context->voxel_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &i);
      DrawVoxelsWithNormals(&g_context->vox_drawer, cmd, i);
    }
  }
  vkCmdEndRenderPass(cmd);

  // render to screen
  BeginRenderingToWindow();
  {
    ds_set = g_context->forward_pass.resulting_image_set;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_context->rect_pipeline_layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_context->rect_pipeline);
    vkCmdDraw(cmd, 4, 1, 0, 0);
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
  Camera* camera = &g_context->camera;
  switch (key)
    {

    case PlatformKey_ESCAPE:
      PlatformWantToQuit();
      break;

      // '1' prints FPS
    case PlatformKey_1:
      LOG_INFO("FPS=%f", g_window->frames_per_second);
      break;
      // '3' toggles mouse control
    case PlatformKey_3:
      if (g_context->mouse_mode) {
        g_context->mouse_mode = 0;
        PlatformShowCursor();
      } else {
        g_context->mouse_mode = 1;
        PlatformHideCursor();
      }
      break;
      // '7' shrinks memory
    case PlatformKey_7:
      {
        uint32_t s = FixFragmentation(&g_context->allocator);
        LOG_INFO("just saved %u bytes", s);
      } break;

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

    default:
      break;

    }
}

void
EngineKeyReleased(PlatformKeyCode key)
{
  Camera* camera = &g_context->camera;

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
EngineMouseMotion(int x, int y, int xrel, int yrel)
{
  (void)x;
  (void)y;
  if (g_context->mouse_mode) {
    CameraRotate(&g_context->camera, yrel, xrel, 0.0f);
  }
}


/// pipeline creation

void CreateRectPipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  Pipeline_Desc pipeline_desc = {
    .vertex_shader = "rect.vert.spv",
    .fragment_shader = "rect.frag.spv",
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

  CreateGraphicsPipelines(&g_context->rect_pipeline, 1, &pipeline_desc, &g_context->rect_pipeline_layout);
}

void CreateTrianglePipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  Pipeline_Desc pipeline_desc = {
    .vertex_shader = "triangle.vert.spv",
    .fragment_shader = "triangle.frag.spv",
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
  CreateGraphicsPipelines(&g_context->triangle_pipeline, 1, &pipeline_desc, &g_context->triangle_pipeline_layout);
}

void CreateVoxelPipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  Pipeline_Desc pipeline_desc = {
    .vertex_shader = "voxel.vert.spv",
    .fragment_shader = "voxel.frag.spv",
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
  PipelineVoxelVertices(&pipeline_desc.vertex_attributes, &pipeline_desc.vertex_attribute_count,
                        &pipeline_desc.vertex_bindings, &pipeline_desc.vertex_binding_count,
                        1);
  CreateGraphicsPipelines(&g_context->voxel_pipeline, 1, &pipeline_desc, &g_context->voxel_pipeline_layout);
}
