#include <SDL.h>

#include "init.h"
#include "device.h"
#include "memory.h"
#include "window.h"
#include "base.h"
#include "linalg.h"
#include "render.h"
#include "voxel.h"
#include "ecs.h"

static VkPipeline createVoxelPipeline();
static VkPipeline createTrianglePipeline();
static VkPipeline createRectPipeline();
static VkPipeline createSHVoxPipeline();

// TODO: do smth with these
VkPipelineLayout pipeline_layout;
VkPipelineLayout pipeline_layout2;
VkPipelineLayout pipeline_layout3;
VkPipelineLayout pipeline_layout4;

lida_ECS* ecs;
lida_TypeInfo vox_grid_type_info;
lida_TypeInfo transform_type_info;

lida_Camera camera;
lida_VoxelDrawer vox_drawer;

int main(int argc, char** argv) {
  lida_EngineInit(argc, argv);
  LIDA_LOG_DEBUG("num images in swapchain: %u\n", lida_WindowGetNumImages());

  ecs = lida_ECS_Create(8, 32);
  vox_grid_type_info = LIDA_TYPE_INFO(lida_VoxelGrid, lida_MallocAllocator(), 0);
  transform_type_info = LIDA_TYPE_INFO(lida_Transform, lida_MallocAllocator(), 0);

  lida_VoxelDrawerCreate(&vox_drawer, 1024 * 1024, 1024);

  VkPipeline pipeline = createTrianglePipeline();
  VkPipeline rect_pipeline = createRectPipeline();
  VkPipeline vox_pipeline = createVoxelPipeline();
  VkPipeline shadow_pipeline = createSHVoxPipeline();

  camera.z_near = 0.01f;
  camera.position = LIDA_VEC3_CREATE(0.0f, 0.0f, -2.0f);
  camera.rotation = LIDA_VEC3_CREATE(0.0f, M_PI, 0.0f);
  camera.up = LIDA_VEC3_CREATE(0.0f, 1.0f, 0.0f);
  camera.fovy = LIDA_RADIANS(45.0f);
  camera.rotation_speed = 0.005f;
  camera.movement_speed = 1.0f;

  uint32_t prev_time = SDL_GetTicks();
  uint32_t curr_time = prev_time;

  // hide the cursor
  SDL_bool mouse_mode = SDL_TRUE;
  SDL_SetRelativeMouseMode(mouse_mode);

  lida_ComponentView* vox_grids = lida_ECS_Components(ecs, &vox_grid_type_info);
  lida_ComponentView* transforms = lida_ECS_Components(ecs, &transform_type_info);
  lida_ComponentSetDestructor(vox_grids, &lida_VoxelGridFreeWrapper);

  // 3x3x3 box
  lida_ID entity1 = lida_CreateEntity(ecs);
  {
    auto grid = (lida_VoxelGrid*)lida_ComponentAdd(ecs, vox_grids, entity1);
    lida_VoxelGridLoadFromFile(grid, "../assets/3x3x3.vox");
    auto transform = (lida_Transform*)lida_ComponentAdd(ecs, transforms, entity1);
    transform->rotation = LIDA_QUAT_IDENTITY();
    transform->position = LIDA_VEC3_CREATE(3.0f, 2.0f, 0.0f);
    transform->scale = 0.85f;
  }

  // another 3x3x3 box
  lida_ID entity2 = lida_CreateEntity(ecs);
  {
    auto grid = (lida_VoxelGrid*)lida_ComponentAdd(ecs, vox_grids, entity2);
    lida_VoxelGridLoadFromFile(grid, "../assets/3x3x3.vox");
    auto transform = (lida_Transform*)lida_ComponentAdd(ecs, transforms, entity2);
    transform->rotation = LIDA_QUAT_IDENTITY();
    transform->position = LIDA_VEC3_CREATE(-3.0f, 2.0f, 0.1f);
    transform->scale = 0.64f;
  }

  // some model
  {
    lida_ID entity = lida_CreateEntity(ecs);
    auto grid = (lida_VoxelGrid*)lida_ComponentAdd(ecs, vox_grids, entity);
    lida_VoxelGridLoadFromFile(grid, "../assets/chr_naked1.vox");
    auto transform = (lida_Transform*)lida_ComponentAdd(ecs, transforms, entity);
    transform->rotation = LIDA_QUAT_IDENTITY();
    transform->position = LIDA_VEC3_CREATE(-1.0f, -1.0f, 3.0f);
    transform->scale = 0.1f;
  }
  // other model
  {
    lida_ID entity = lida_CreateEntity(ecs);
    auto grid = (lida_VoxelGrid*)lida_ComponentAdd(ecs, vox_grids, entity);
    lida_VoxelGridLoadFromFile(grid, "../assets/chr_naked4.vox");
    auto transform = (lida_Transform*)lida_ComponentAdd(ecs, transforms, entity);
    transform->rotation = LIDA_QUAT_IDENTITY();
    transform->position = LIDA_VEC3_CREATE(-1.1f, -1.6f, 7.0f);
    transform->scale = 0.098f;
  }

  SDL_Event event;
  int running = 1;
  while (running) {
    prev_time = curr_time;
    curr_time = SDL_GetTicks();
    const float dt = (curr_time - prev_time) / 1000.0f;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        running = 0;
        break;
      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
          running = 0;
          break;
        case SDLK_1:
          LIDA_LOG_INFO("FPS=%f", lida_WindowGetFPS());
          break;
        case SDLK_2:
          lida_VoxelGridSet((lida_VoxelGrid*)lida_ComponentGet(vox_grids, entity1), 0, 0, 0, 17);
          break;
        case SDLK_3:
          if (mouse_mode) mouse_mode = SDL_FALSE;
          else mouse_mode = SDL_TRUE;
          SDL_SetRelativeMouseMode(mouse_mode);
          break;

          // camera movement
        case SDLK_w:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_FORWARD);
          break;
        case SDLK_s:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_BACK);
          break;
        case SDLK_a:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_LEFT);
          break;
        case SDLK_d:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_RIGHT);
          break;
        case SDLK_LSHIFT:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_DOWN);
          break;
        case SDLK_SPACE:
          lida_CameraPressed(&camera, LIDA_CAMERA_PRESSED_UP);
          break;
        }
        break;

      case SDL_KEYUP:
        switch (event.key.keysym.sym) {
        case SDLK_w:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_FORWARD);
          break;
        case SDLK_s:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_BACK);
          break;
        case SDLK_a:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_LEFT);
          break;
        case SDLK_d:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_RIGHT);
          break;
        case SDLK_LSHIFT:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_DOWN);
          break;
        case SDLK_SPACE:
          lida_CameraUnpressed(&camera, LIDA_CAMERA_PRESSED_UP);
          break;
        }
        break;

      case SDL_MOUSEMOTION:
        if (mouse_mode) {
          lida_CameraRotate(&camera, event.motion.yrel, event.motion.xrel, 0.0f);
        }
        break;

      case SDL_WINDOWEVENT:
        switch (event.window.event) {
        case SDL_WINDOWEVENT_RESIZED:
          vkDeviceWaitIdle(lida_GetLogicalDevice());
          lida_WindowResize();
          lida_ResetDynamicSets();
          lida_ForwardPassResize(lida_WindowGetExtent().width, lida_WindowGetExtent().height);
          break;
        }
        break;
      }
    }

    VkExtent2D window_extent = lida_WindowGetExtent();
    lida_CameraUpdate(&camera, dt, window_extent.width, window_extent.height);
    lida_CameraUpdateProjection(&camera);
    lida_CameraUpdateView(&camera);

    lida_SceneDataStruct* sc_data = lida_ForwardPassGetSceneData();
    memcpy(&sc_data->camera_projection, &camera.projection_matrix, sizeof(lida_Mat4));
    memcpy(&sc_data->camera_view, &camera.view_matrix, sizeof(lida_Mat4));
    lida_Mat4Mul(&sc_data->camera_projection, &sc_data->camera_view, &sc_data->camera_projview);
    sc_data->sun_dir = LIDA_VEC3_CREATE(0.03f, 0.9f, -0.1f);
    sc_data->sun_ambient = 0.1f;
    lida_Vec3Normalize(&sc_data->sun_dir, &sc_data->sun_dir);

    lida_Mat4 light_proj, light_view;
    const float b = 40.0f;
    lida_OrthographicMatrix(-b, b, -b, b, 1.0f, 40.0f, &light_proj);
    // lida_LookAtMatrix(& LIDA_VEC3_CREATE(1.0f, 10.0f, 0.0f), & LIDA_VEC3_SUB(LIDA_VEC3_CREATE(0.0f, 0.0f, 0.0f), sc_data->sun_dir), &camera.up,
    //                   &light_view);
    lida_Vec3 camera_target = camera.position + camera.front;
    lida_LookAtMatrix(& camera.position, &camera_target, &camera.up,
                      &light_view);
    lida_Mat4Mul(&light_proj, &light_view, &sc_data->light_space);

    lida_VoxelDrawerNewFrame(&vox_drawer);

    // send voxel grids to draw
    lida_VoxelGrid* grid = (lida_VoxelGrid*)lida_ComponentData(vox_grids);
    lida_ID* entity = lida_ComponentIDs(vox_grids);
    uint32_t count = 0;
    // LIDA_COMPONENT_FOREACH(vox_grids, grid, entity)
    uint32_t count_ = lida_ComponentCount(vox_grids);
    for (; count_--; grid++, entity++)
    {
      lida_Transform* transform = (lida_Transform*)lida_ComponentGet(transforms, *entity);
      lida_VoxelDrawerPushMesh(&vox_drawer, grid, transform);
    }

    VkCommandBuffer cmd = lida_WindowBeginCommands();

    lida_ShadowPassBegin(cmd);
    VkDescriptorSet ds_set = lida_ShadowPassGetDS0();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout4, 0, 1, &ds_set, 0, NULL);
    // TODO: don't hardcode depth bias values
    vkCmdSetDepthBias(cmd, 1.0f, 0.0f, 1.75f);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);
    lida_VoxelDrawerDraw(&vox_drawer, cmd);
    vkCmdEndRenderPass(cmd);

    lida_Vec4 colors[] = {
      LIDA_VEC4_CREATE(1.0f, 0.2f, 0.2f, 1.0f),
      LIDA_VEC4_CREATE(0.0f, 0.9f, 0.4f, 1.0f),
      LIDA_VEC4_CREATE(0.2f, 0.35f, 0.76f, 1.0f),
      LIDA_VEC4_CREATE(0.0f, 0.0f, 0.0f, 0.0f)
    };
    float clear_color[4] = { 0.08f, 0.2f, 0.25f, 1.0f };

    lida_ForwardPassBegin(cmd, clear_color);
    ds_set = lida_ForwardPassGetDS0();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // 1st draw
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(lida_Vec4)*3 + sizeof(lida_Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // 2nd draw
    colors[3] = LIDA_VEC4_CREATE(0.1f, 0.0f, 1.0f, 0.0f);
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(lida_Vec4)*3 + sizeof(lida_Vec3), &colors);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // draw voxels
    VkDescriptorSet ds_sets[1] = { lida_ForwardPassGetDS0() };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout3, 0, LIDA_ARR_SIZE(ds_sets), ds_sets, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vox_pipeline);
    for (uint32_t i = 0; i < 6; i++) {
      vkCmdPushConstants(cmd, pipeline_layout3, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &i);
      lida_VoxelDrawerDrawWithNormals(&vox_drawer, cmd, i);
    }

    vkCmdEndRenderPass(cmd);

    lida_WindowBeginRendering();
    ds_set = lida_ForwardPassGetDS1();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout2, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rect_pipeline);
    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);

    lida_WindowPresent();
  }

  LIDA_LOG_TRACE("Exited successfully");

  vkDeviceWaitIdle(lida_GetLogicalDevice());

  lida_VoxelDrawerDestroy(&vox_drawer);

  lida_ECS_Destroy(ecs);

  vkDestroyPipeline(lida_GetLogicalDevice(), shadow_pipeline, NULL);
  vkDestroyPipeline(lida_GetLogicalDevice(), rect_pipeline, NULL);
  vkDestroyPipeline(lida_GetLogicalDevice(), pipeline, NULL);
  vkDestroyPipeline(lida_GetLogicalDevice(), vox_pipeline, NULL);

  lida_EngineFree();

  return 0;
}

VkPipeline
createVoxelPipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  VkPipeline ret;
  lida_PipelineDesc pipeline_desc = {
    .vertex_shader = "shaders/voxel.vert.spv",
    .fragment_shader = "shaders/voxel.frag.spv",
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depthBiasEnable = VK_FALSE,
    .msaa_samples = lida_ForwardPassGet_MSAA_Samples(),
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = LIDA_ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = lida_ForwardPassGetRenderPass(),
    .subpass = 0,
    .marker = "forward/voxel-pipeline"
  };
  lida_PipelineVoxelVertices(&pipeline_desc.vertex_attributes, &pipeline_desc.vertex_attribute_count,
                             &pipeline_desc.vertex_bindings, &pipeline_desc.vertex_binding_count,
                             1);
  lida_CreateGraphicsPipelines(&ret, 1, &pipeline_desc, &pipeline_layout3);
  return ret;
}

VkPipeline
createTrianglePipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  VkPipeline ret;
  lida_PipelineDesc pipeline_desc = {
    .vertex_shader = "shaders/triangle.vert.spv",
    .fragment_shader = "shaders/triangle.frag.spv",
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .depthBiasEnable = VK_FALSE,
    .msaa_samples = lida_ForwardPassGet_MSAA_Samples(),
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = LIDA_ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = lida_ForwardPassGetRenderPass(),
    .subpass = 0,
    .marker = "draw-triangle-pipeline"
  };
  lida_CreateGraphicsPipelines(&ret, 1, &pipeline_desc, &pipeline_layout);

  return ret;
}

VkPipeline createRectPipeline()
{
  VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  VkPipelineColorBlendStateCreateInfo colorblend_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &colorblend_attachment,
  };
  VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  lida_PipelineDesc pipeline_desc = {
    .vertex_shader = "shaders/rect.vert.spv",
    .fragment_shader = "shaders/rect.frag.spv",
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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
    .marker = "blit-3D-scene-fullscreen"
  };

  VkPipeline ret;
  lida_CreateGraphicsPipelines(&ret, 1, &pipeline_desc, &pipeline_layout2);
  return ret;
}

VkPipeline createSHVoxPipeline()
{
  VkDynamicState dynamic_state = VK_DYNAMIC_STATE_DEPTH_BIAS;
  lida_PipelineDesc pipeline_desc = {
    .vertex_shader = "shaders/shadow_voxel.vert.spv",
    .fragment_shader = NULL,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .depthBiasEnable = VK_TRUE,
    .msaa_samples = VK_SAMPLE_COUNT_1_BIT,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 0,
    .dynamic_state_count = 1,
    .dynamic_states = &dynamic_state,
    .render_pass = lida_ShadowPassGetRenderPass(),
    .subpass = 0,
    .marker = "voxels-to-shadow-map",
  };
  lida_PipelineVoxelVertices(&pipeline_desc.vertex_attributes, &pipeline_desc.vertex_attribute_count,
                             &pipeline_desc.vertex_bindings, &pipeline_desc.vertex_binding_count,
                             0);
  lida_ShadowPassViewport(&pipeline_desc.viewport, &pipeline_desc.scissor);

  VkPipeline ret;
  lida_CreateGraphicsPipelines(&ret, 1, &pipeline_desc, &pipeline_layout4);
  return ret;
}
