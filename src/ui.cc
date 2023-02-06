#include "ui.h"

#include "device.h"
#include "window.h"

#include "lib/imgui.h"
#include "lib/imgui_impl_sdl.h"
#include "lib/imgui_impl_vulkan.h"

#include <ft2build.h>
#include FT_FREETYPE_H

static ImGuiContext* im_context;
static FT_Library freetype;

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
lida_UI_Prepare(VkCommandBuffer cmd)
{
  if (lida_WindowGetFrameNo() == 0) {
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
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
