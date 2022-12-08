#include <SDL.h>

#include "device.h"
#include "memory.h"
#include "window.h"
#include "base.h"

VkPipeline createTrianglePipeline();

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO);
  lida_TempAllocatorCreate(32 * 1024);
  lida_InitPlatformSpecificLoggers();

  LIDA_DEVICE_CREATE(.enable_debug_layers = 1,
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

  SDL_Event event;
  int running = 1;
  while (running) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        running = 0;
        break;
      case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_ESCAPE)
          running = 0;
        if (event.key.keysym.sym == SDLK_SPACE)
          LIDA_LOG_INFO("FPS=%f", lida_WindowGetFPS());
        break;
      }
    }

    VkCommandBuffer cmd = lida_WindowBeginCommands();

    float clear_color[4] = {0.7f, 0.1f, 0.7f, 1.0f};
    lida_WindowBeginRendering(clear_color);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
    lida_WindowPresent();
  }

  printf("Exited successfully\n");

  vkDeviceWaitIdle(lida_GetLogicalDevice());

  lida_VideoMemoryFree(&memory);

  vkDestroyPipeline(lida_GetLogicalDevice(), pipeline, NULL);

  lida_WindowDestroy();
  lida_DeviceDestroy(0);
  lida_TempAllocatorDestroy();

  return 0;
}

VkPipeline createTrianglePipeline() {

  VkPipelineLayout pipeline_layout;
  VkPipelineLayoutCreateInfo layout_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };
  vkCreatePipelineLayout(lida_GetLogicalDevice(), &layout_info, NULL, &pipeline_layout);

  VkPipelineShaderStageCreateInfo stages[2];
  stages[0] = (VkPipelineShaderStageCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = lida_LoadShader("shaders/triangle.vert.spv"),
    .pName = "main",
  };
  stages[1] = (VkPipelineShaderStageCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = lida_LoadShader("shaders/triangle.frag.spv"),
    .pName = "main",
  };
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
  vkDestroyPipelineLayout(lida_GetLogicalDevice(), pipeline_layout, NULL);
  return ret;
}
