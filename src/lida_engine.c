/* -*- mode: c -*-

  lida engine - portable and small 3D Vulkan engine.

  ===============================
  Author: Adil Mokhammad
  Email: 0adilmohammad0@gmail.com
 */

#include "lib/volk.h"

#include "stdalign.h"
#include "string.h"

#include "lida_platform.h"

#define LIDA_ENGINE_VERSION 202302
#define INTERNAL static
#define GLOBAL static

#include "lida_base.c"
#include "lida_device.c"
#include "lida_window.c"
#include "lida_render.c"

typedef struct {

  Forward_Pass forward_pass;
  VkPipelineLayout rect_pipeline_layout;
  VkPipeline rect_pipeline;

} Engine_Context;

GLOBAL Engine_Context* g_context;


/// Engine general functions

INTERNAL VkPipeline createRectPipeline(VkPipelineLayout* pipeline_layout)
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

  VkPipeline ret;
  CreateGraphicsPipelines(&ret, 1, &pipeline_desc, pipeline_layout);
  return ret;
}

void
EngineInit(const Engine_Startup_Info* info)
{
  g_persistent_memory.size = 16 * 1024 * 1024;
  g_persistent_memory.ptr = PlatformAllocateMemory(g_persistent_memory.size);
  const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  CreateDevice(info->enable_debug_layers,
               info->gpu_id,
               info->app_name, info->app_version,
               device_extensions, ARR_SIZE(device_extensions));
  CreateWindow(info->window_vsync);

  g_context = PersistentAllocate(sizeof(Engine_Context));

  CreateForwardPass(&g_context->forward_pass,
                    g_window->swapchain_extent.width, g_window->swapchain_extent.height,
                    VK_SAMPLE_COUNT_4_BIT);

  const Shader_Reflect* refl;
  VkShaderModule shader = LoadShader("rect.frag.spv", &refl);
  Assert(shader);

  VkPipelineLayout ppl_layout = CreatePipelineLayout(&refl, 1);
  Assert(ppl_layout);

  g_context->rect_pipeline = createRectPipeline(&g_context->rect_pipeline_layout);
  Assert(g_context->rect_pipeline);
}

void
EngineFree()
{
  // wait until commands from previous frames are ended so we can safely destroy GPU resources
  vkDeviceWaitIdle(g_device->logical_device);

  vkDestroyPipeline(g_device->logical_device, g_context->rect_pipeline, NULL);

  DestroyForwardPass(&g_context->forward_pass);

  // PersistentPop(g_context);

  DestroyWindow(0);
  DestroyDevice(0);
  PlatformFreeMemory(g_persistent_memory.ptr);
}

void
EngineUpdateAndRender()
{
  VkCommandBuffer cmd = BeginCommands();

  BeginRenderingToWindow();

  vkCmdEndRenderPass(cmd);

  vkEndCommandBuffer(cmd);

  PresentToScreen();
}

void
EngineKeyPressed(PlatformKeyCode key)
{
  switch (key)
    {

    case PlatformKey_ESCAPE:
      PlatformWantToQuit();
      break;

    case PlatformKey_1:
      LOG_INFO("FPS=%f", g_window->frames_per_second);
      break;

    default:
      break;

    }
}

void
EngineKeyReleased(PlatformKeyCode key)
{
  switch (key)
    {

    default:
      break;

    }
}

void
EngineMouseMotion(int x, int y, int xrel, int yrel)
{
  (void)x;
  (void)y;
  (void)xrel;
  (void)yrel;
}
