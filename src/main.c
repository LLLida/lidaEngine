#include <SDL.h>

#include "device.h"
#include "memory.h"
#include "window.h"
#include "base.h"
#include "linalg.h"

VkPipeline createTrianglePipeline();

VkPipelineLayout pipeline_layout;
lida_Camera camera;

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO);
  lida_TempAllocatorCreate(32 * 1024);
  lida_InitPlatformSpecificLoggers();

  // lida_Mat4 m1 = { 1, 0, 2, 3, 4, 1, 2, 0, 3, 3, 4, 1 };
  // lida_Mat4 m2 = { 0, 3, 4, 5, 1, 4, 5, 7, 4, 3, 2, 1 };
  // lida_Mat4Mul(&m1, &m2, &m1);
  // for (uint32_t i = 0; i < 16; i++)
  //   if (i % 4 == 3)
  //     printf("%f\n", ((float*)&m1)[i]);
  //   else
  //     printf("%f ", ((float*)&m1)[i]);

  LIDA_DEVICE_CREATE(.enable_debug_layers = (argc == 1),
                     .gpu_id = 0,
                     .app_name = "tst",
                     .app_version = VK_MAKE_VERSION(0, 0, 0),
                     .device_extensions = (const char*[]){ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME },
                     .num_device_extensions = 2);

  LIDA_WINDOW_CREATE(.name = "hello world",
                     .x = SDL_WINDOWPOS_CENTERED,
                     .y = SDL_WINDOWPOS_CENTERED,
                     .w = 1080,
                     .h = 720,
                     .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);
  // printf("num images in swapchain: %u\n", lida_WindowGetNumImages());
  LIDA_LOG_DEBUG("num images in swapchain: %u\n", lida_WindowGetNumImages());

  // for (uint32_t i = 0; i < lida_GetNumAvailableDeviceExtensions(); i++) {
  //   printf("%s\n", lida_GetAvailableDeviceExtensions()[i].extensionName);
  // }
  // for (uint32_t i = 0; i < lida_GetNumAvailableInstanceExtensions(); i++) {
  //   printf("%s\n", lida_GetAvailableInstanceExtensions()[i].extensionName);
  // }
  // printf("--------------------\n");
  // for (uint32_t i = 0; i < lida_GetNumEnabledInstanceExtensions(); i++) {
  //   printf("%s\n", lida_GetEnabledInstanceExtensions()[i]);
  // }

  VkPipeline pipeline = createTrianglePipeline();

  lida_VideoMemory memory;
  lida_VideoMemoryAllocate(&memory, 128*1024*1024, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, UINT32_MAX);

  VkDescriptorSetLayoutBinding binding = {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };
  VkDescriptorSet ds_set;
  VkResult err = lida_AllocateDescriptorSets(&binding, 1, &ds_set, 1, 0);
  VkBuffer buffer;
  lida_BufferCreate(&buffer, 1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  void* mapped;
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(), buffer, &requirements);
  lida_BufferBindToMemory(&memory, buffer, &requirements, &mapped, NULL);

  VkDescriptorBufferInfo buffer_info = {
    .buffer = buffer,
    .offset = 0,
    .range = 128,
  };

  VkWriteDescriptorSet write_set = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = ds_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .pBufferInfo = &buffer_info,
  };
  lida_UpdateDescriptorSets(&write_set, 1);

  float* numbers = mapped;

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
  SDL_SetRelativeMouseMode(1);

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
        lida_CameraRotate(&camera, event.motion.yrel, event.motion.xrel, 0.0f);
        break;
      }
    }

    VkExtent2D window_extent = lida_WindowGetExtent();
    lida_CameraUpdate(&camera, dt, window_extent.width, window_extent.height);
    lida_CameraUpdateProjection(&camera);
    lida_CameraUpdateView(&camera);

    memcpy(numbers, &camera.projection_matrix, sizeof(lida_Mat4));
    memcpy(numbers + 16, &camera.view_matrix, sizeof(lida_Mat4));

    VkCommandBuffer cmd = lida_WindowBeginCommands();

    lida_Vec4 colors[3] = {
      LIDA_VEC4_CREATE(1.0f, 0.2f, 0.2f, 1.0f),
      LIDA_VEC4_CREATE(0.0f, 0.9f, 0.4f, 1.0f),
      LIDA_VEC4_CREATE(0.2f, 0.35f, 0.76f, 1.0f)
    };

    float clear_color[4] = {0.7f, 0.1f, 0.7f, 1.0f};
    lida_WindowBeginRendering(clear_color);
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(lida_Vec4)*3 + sizeof(lida_Vec3), &colors);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &ds_set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
    lida_WindowPresent();
  }

  printf("Exited successfully\n");

  vkDeviceWaitIdle(lida_GetLogicalDevice());

  vkDestroyBuffer(lida_GetLogicalDevice(), buffer, NULL);

  lida_VideoMemoryFree(&memory);

  vkDestroyPipeline(lida_GetLogicalDevice(), pipeline, NULL);
  vkDestroyPipelineLayout(lida_GetLogicalDevice(), pipeline_layout, NULL);

  lida_WindowDestroy();
  lida_DeviceDestroy(0);
  lida_TempAllocatorDestroy();

  return 0;
}

VkPipeline createTrianglePipeline() {

  VkShaderModule vertex_shader, fragment_shader;
  const lida_ShaderReflect* reflects[2];
  vertex_shader = lida_LoadShader("shaders/triangle.vert.spv", &reflects[0]);
  fragment_shader = lida_LoadShader("shaders/triangle.frag.spv", &reflects[1]);

  pipeline_layout = lida_CreatePipelineLayout(reflects, 2);

  VkPipelineShaderStageCreateInfo stages[2];
  stages[0] = (VkPipelineShaderStageCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vertex_shader,
    .pName = "main",
  };
  stages[1] = (VkPipelineShaderStageCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = fragment_shader,
    .pName = "main",
  };

  // for (uint32_t i = 0; i < lida_ShaderReflectGetNumSets(reflect_info); i++) {
  //   uint32_t num_bindings = lida_ShaderReflectGetNumBindings(reflect_info, i);
  //   VkDescriptorSetLayoutBinding* bindings = lida_ShaderReflectGetBindings(reflect_info, i);
  //   for (uint32_t j = 0; j < num_bindings; j++) {
  //     LIDA_LOG_DEBUG("Binding %u: binding=%u, type=%d, descriptorCount=%u, stageFlags=%d",
  //                    j, bindings[j].binding, bindings[j].descriptorType,
  //                    bindings[j].descriptorCount, bindings[j].stageFlags);
  //   }
  // }
  // for (uint32_t i = 0; i < lida_ShaderReflectGetNumRanges(reflect_info); i++) {
  //   const VkPushConstantRange* ranges = lida_ShaderReflectGetRanges(reflect_info);
  //   LIDA_LOG_DEBUG("Range %u: stages=%d, offset=%u, size=%u",
  //                  i, ranges[i].stageFlags, ranges[i].offset, ranges[i].size);
  // }

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };
  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };
  VkPipelineViewportStateCreateInfo viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1,
  };
  VkPipelineRasterizationStateCreateInfo rasterization_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
  };
  VkPipelineMultisampleStateCreateInfo multisample_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .alphaToCoverageEnable = VK_FALSE,
  };
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
  VkPipelineDynamicStateCreateInfo dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamic_states,
  };
  VkGraphicsPipelineCreateInfo pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = stages,
    .pVertexInputState = &vertex_input_state,
    .pInputAssemblyState = &input_assembly_state,
    .pViewportState = &viewport_state,
    .pRasterizationState = &rasterization_state,
    .pMultisampleState = &multisample_state,
    .pColorBlendState = &colorblend_state,
    .pDynamicState = &dynamic_state,
    .layout = pipeline_layout,
    .renderPass = lida_WindowGetRenderPass(),
    .subpass = 0,
  };

  VkPipeline ret;
  vkCreateGraphicsPipelines(lida_GetLogicalDevice(), VK_NULL_HANDLE, 1, &pipeline_info, NULL, &ret);
  return ret;
}
