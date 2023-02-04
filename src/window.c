#include "window.h"
#include "base.h"
#include "device.h"
#include "memory.h"

#include <SDL_timer.h>
#include <SDL_vulkan.h>

typedef struct {
  VkCommandBuffer cmd;
  VkSemaphore image_available;
  uint64_t submit_time;
} Frame;

#define FRAMES_IN_FLIGHT 2

typedef struct {
  SDL_Window* window;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  VkRenderPass render_pass;
  uint32_t num_images;
  lida_WindowImage* images;
  Frame frames[FRAMES_IN_FLIGHT];
  VkSemaphore render_finished_semaphore;
  VkFence resources_available_fence;
  uint64_t frame_counter;
  uint64_t last_submit;
  float frames_per_second;
  uint32_t current_image;
  VkExtent2D swapchain_extent;
  VkSurfaceFormatKHR format;
  VkPresentModeKHR present_mode;
  VkCompositeAlphaFlagBitsKHR composite_alpha;
} lida_Window;

lida_Window* g_window;

static VkResult CreateSwapchain(VkPresentModeKHR present_mode);
static VkResult CreateRenderPass();
static VkResult CreateFrames();



int
lida_WindowCreate(const lida_WindowDesc* desc)
{
  LIDA_PROFILE_FUNCTION();
  g_window = lida_TempAllocate(sizeof(lida_Window));
  memset(g_window, 0, sizeof(lida_Window));
  Uint32 win_flags = SDL_WINDOW_VULKAN;
  if (desc->resizable) win_flags |= SDL_WINDOW_RESIZABLE;
  g_window->window = SDL_CreateWindow(desc->name, desc->x, desc->y, desc->w, desc->h,
                                      win_flags);
  if (!SDL_Vulkan_CreateSurface(g_window->window, lida_GetVulkanInstance(), &g_window->surface)) {
    LIDA_LOG_FATAL("failed to create vulkan surface with error %s", SDL_GetError());
    goto error;
  }

  VkResult err = CreateSwapchain(desc->preferred_present_mode);
  if (err != VK_SUCCESS)
    goto error;

  err = CreateFrames(g_window);
  if (err != VK_SUCCESS)
    goto error;

  return 0;
 error:
  if (g_window->images)
    lida_TempFree(g_window->images);
  lida_TempFree(g_window);
  return -1;
}

void
lida_WindowDestroy()
{
  LIDA_PROFILE_FUNCTION();
  VkDevice dev = lida_GetLogicalDevice();
  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(dev, g_window->frames[i].image_available, NULL);
  }
  vkDestroyFence(dev, g_window->resources_available_fence, NULL);
  vkDestroySemaphore(dev, g_window->render_finished_semaphore, NULL);
  for (uint32_t i = 0; i < g_window->num_images; i++) {
    vkDestroyFramebuffer(dev, g_window->images[i].framebuffer, NULL);
    vkDestroyImageView(dev, g_window->images[i].image_view, NULL);
  }
  vkDestroyRenderPass(dev, g_window->render_pass, NULL);
  vkDestroySwapchainKHR(dev, g_window->swapchain, NULL);
  vkDestroySurfaceKHR(lida_GetVulkanInstance(), g_window->surface, NULL);

  lida_MallocFree(g_window->images);
  lida_TempFree(g_window);
  g_window = NULL;
}

void
lida_WindowResize()
{
  VkDevice dev = lida_GetLogicalDevice();
  for (uint32_t i = 0; i < g_window->num_images; i++) {
    vkDestroyFramebuffer(dev, g_window->images[i].framebuffer, NULL);
    vkDestroyImageView(dev, g_window->images[i].image_view, NULL);
  }
  lida_MallocFree(g_window->images);
  VkResult err = CreateSwapchain(g_window->present_mode);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to recreate swapchain with error %s", lida_VkResultToString(err));
    return;
  }
  LIDA_LOG_TRACE("successfully resized window");
}

SDL_Window*
lida_WindowGet_SDL_Handle()
{
  return g_window->window;
}

VkSurfaceKHR
lida_WindowGetSurface()
{
  return g_window->surface;
}

VkSwapchainKHR
lida_WindowGetSwapchain()
{
  return g_window->swapchain;
}

uint32_t
lida_WindowGetNumImages()
{
  return g_window->num_images;
}

const lida_WindowImage*
lida_WindowGetImages()
{
  return g_window->images;
}

VkExtent2D
lida_WindowGetExtent()
{
  return g_window->swapchain_extent;
}

VkRenderPass
lida_WindowGetRenderPass()
{
  return g_window->render_pass;
}

VkSurfaceFormatKHR
lida_WindowGetFormat()
{
  return g_window->format;
}

VkPresentModeKHR
lida_WindowGetPresentMode()
{
  return g_window->present_mode;
}

float
lida_WindowGetFPS()
{
  return g_window->frames_per_second;
}

uint64_t
lida_WindowGetFrameNo()
{
  return g_window->frame_counter;
}

VkCommandBuffer
lida_WindowBeginCommands()
{
  LIDA_PROFILE_FUNCTION();
  Frame* frame = &g_window->frames[g_window->frame_counter % FRAMES_IN_FLIGHT];
  VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  vkBeginCommandBuffer(frame->cmd, &begin_info);
  return frame->cmd;
}

VkResult
lida_WindowBeginRendering()
{
  LIDA_PROFILE_FUNCTION();
  Frame* frame = &g_window->frames[g_window->frame_counter % FRAMES_IN_FLIGHT];
  VkResult err = vkAcquireNextImageKHR(lida_GetLogicalDevice(),
                                       g_window->swapchain,
                                       UINT64_MAX,
                                       frame->image_available,
                                       VK_NULL_HANDLE,
                                       &g_window->current_image);
  switch (err) {
  case VK_SUCCESS:
    break;
  case VK_SUBOPTIMAL_KHR:
    LIDA_LOG_WARN("warning: got VK_SUBOPTIMAL_KHR when acquiring next swapchain image");
    break;
  default:
    LIDA_LOG_ERROR("failed to acquire next swapchain image with error %s", lida_VkResultToString(err));
    return err;
  }
  // start render pass
  VkRect2D render_area = { .offset = {0, 0},
                           .extent = g_window->swapchain_extent };
  VkRenderPassBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = g_window->render_pass,
    .framebuffer = g_window->images[g_window->current_image].framebuffer,
    .renderArea = render_area,
    .clearValueCount = 0,
  };
  vkCmdBeginRenderPass(frame->cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport = {
    .x = 0.0f,
    .y = 0.0f,
    .width = (float)render_area.extent.width,
    .height =  (float)render_area.extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(frame->cmd, 0, 1, &viewport);
  vkCmdSetScissor(frame->cmd, 0, 1, &render_area);
  return VK_SUCCESS;
}

VkResult
lida_WindowPresent()
{
  LIDA_PROFILE_FUNCTION();
  VkDevice dev = lida_GetLogicalDevice();
  Frame* frame = &g_window->frames[g_window->frame_counter % FRAMES_IN_FLIGHT];
  VkResult err;
  err = vkWaitForFences(dev, 1, &g_window->resources_available_fence, VK_TRUE, UINT64_MAX);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to wait for fence before submitting commands with error %s", lida_VkResultToString(err));
    return err;
  }
  err = vkResetFences(dev, 1, &g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to reset fences before presenting image with error %s", lida_VkResultToString(err));
    return err;
  }
  // submit commands to render current image
  VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &frame->image_available,
    .pWaitDstStageMask = wait_stages,
    .commandBufferCount = 1,
    .pCommandBuffers = &frame->cmd,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &g_window->render_finished_semaphore,
  };
  err = lida_QueueSubmit(&submit_info, 1, g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to submit commands to graphics queue with error %s", lida_VkResultToString(err));
    return err;
  }
  frame->submit_time = SDL_GetPerformanceCounter();
  g_window->frames_per_second = (float)SDL_GetPerformanceFrequency() / (float)(frame->submit_time - g_window->last_submit);
  g_window->last_submit = frame->submit_time;
  // present image to screen
  VkResult present_results[1];
  VkPresentInfoKHR present_info = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &g_window->render_finished_semaphore,
    .swapchainCount = 1,
    .pSwapchains = &g_window->swapchain,
    .pImageIndices = &g_window->current_image,
    .pResults = present_results,
  };
  err = lida_QueuePresent(&present_info);
  if (err != VK_SUCCESS && err != VK_SUBOPTIMAL_KHR) {
    LIDA_LOG_ERROR("queue failed to present with error %s", lida_VkResultToString(err));
  }
  g_window->frame_counter++;
  g_window->current_image = UINT32_MAX;
  return err;
}


/// Static functions

VkResult
CreateSwapchain(VkPresentModeKHR present_mode)
{
  VkResult err;
  VkPhysicalDevice phys_dev = lida_GetPhysicalDevice();
  VkDevice log_dev = lida_GetLogicalDevice();
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev,
                                            g_window->surface,
                                            &capabilities);

  // choose surface format
  VkSurfaceFormatKHR old_format = g_window->format;
  uint32_t count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_dev,
                                       g_window->surface,
                                       &count,
                                       NULL);
  VkSurfaceFormatKHR* formats = lida_TempAllocate(sizeof(VkSurfaceFormatKHR) * count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_dev,
                                       g_window->surface,
                                       &count,
                                       formats);
  g_window->format = formats[0];
  for (uint32_t i = 1; i < count; i++) {
    // try to pick R8G8B8A8_SRGB with nonlinear color space because it looks good
    if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
        formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      g_window->format = formats[i];
      break;
    }
  }
  lida_TempFree(formats);

  // choose present mode
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_dev, g_window->surface, &count, NULL);
  VkPresentModeKHR* present_modes = lida_TempAllocate(sizeof(VkPresentModeKHR) * count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_dev, g_window->surface, &count, present_modes);
  g_window->present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (uint32_t i = 0; i < count; i++) {
    if (present_modes[i] == present_mode) {
      g_window->present_mode = present_modes[i];
      break;
    }
  }
  lida_TempFree(present_modes);

  // choose extent
  if (capabilities.currentExtent.width == UINT32_MAX) {
#define CLAMP(value, low, high) (((value)<(low))?(low):(((value)>(high))?(high):(value)))
    g_window->swapchain_extent.width = CLAMP(g_window->swapchain_extent.width,
                                         capabilities.minImageExtent.width,
                                         capabilities.maxImageExtent.height);
    g_window->swapchain_extent.height = CLAMP(g_window->swapchain_extent.height,
                                          capabilities.minImageExtent.height,
                                          capabilities.maxImageExtent.height);
#undef CLAMP
  } else {
    g_window->swapchain_extent = capabilities.currentExtent;
  }

  // choose image count
  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
    image_count = capabilities.maxImageCount;

  // choose composite alpha flag
  if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    g_window->composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else {
    uint32_t i = 0;
    VkCompositeAlphaFlagsKHR flags = capabilities.supportedCompositeAlpha;
    // composite_alpha always will be initialized as spec says:
    // supportedCompositeAlpha is a bitmask of VkCompositeAlphaFlagBitsKHR, representing the alpha compositing modes supported by the presentation engine for the surface on the specified device, and at least one bit will be set
    while (flags > 0) {
      if (flags & 1) {
        g_window->composite_alpha = 1 << i;
        break;
      }
      flags >>= 1;
      i++;
    }
  }

  uint32_t queue_family_indices[] = { lida_GetGraphicsQueueFamily() };
  // create swapchain
  VkSwapchainCreateInfoKHR swapchain_info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = g_window->surface,
    .minImageCount = image_count,
    .imageFormat = g_window->format.format,
    .imageColorSpace = g_window->format.colorSpace,
    .imageExtent = g_window->swapchain_extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 1,
    .pQueueFamilyIndices = queue_family_indices,
    .preTransform = capabilities.currentTransform,
    .compositeAlpha = g_window->composite_alpha,
    .presentMode = g_window->present_mode,
    .clipped = 1,
    .oldSwapchain = g_window->swapchain,
  };
  // note: we specified swapchainInfo.oldSwapchain so
  // we don't need to destroy oldSwapchain when resizing
  // From vulkan spec:
  // Upon calling vkCreateSwapchainKHR with an oldSwapchain that is not
  // VK_NULL_HANDLE, oldSwapchain is retired — even if creation of the new
  // swapchain fails. The new swapchain is created in the non-retired state
  // whether or not oldSwapchain is VK_NULL_HANDLE.
  err = vkCreateSwapchainKHR(log_dev, &swapchain_info, NULL, &g_window->swapchain);
  if (err != VK_SUCCESS) {
    LIDA_LOG_FATAL("failed to create swapchain with error %s", lida_VkResultToString(err));
    return err;
  }

  // recreate render pass if needed
  if (old_format.format != g_window->format.format) {
    if (g_window->render_pass) vkDestroyRenderPass(log_dev, g_window->render_pass, NULL);
    err = CreateRenderPass(g_window);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create render pass with errror %s", lida_VkResultToString(err));
      return err;
    }
  }

  // get images(VkSwapchainKHR kindly manages their lifetime for us)
  vkGetSwapchainImagesKHR(log_dev, g_window->swapchain, &g_window->num_images, NULL);
  VkImage* swapchain_images = lida_TempAllocate(sizeof(VkImage) * g_window->num_images);
  vkGetSwapchainImagesKHR(log_dev, g_window->swapchain, &g_window->num_images, swapchain_images);
  // create attachments
  g_window->images = lida_Malloc(sizeof(lida_WindowImage) * g_window->num_images);
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = g_window->format.format,
    .components = { .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A,},
    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                          .baseMipLevel = 0,
                          .levelCount = 1,
                          .baseArrayLayer = 0,
                          .layerCount = 1 },
  };
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = g_window->render_pass,
    .attachmentCount = 1,
    .width = g_window->swapchain_extent.width,
    .height = g_window->swapchain_extent.height,
    .layers = 1,
  };
  for (uint32_t i = 0; i < g_window->num_images; i++) {
    char buff[64];
    g_window->images[i].image = swapchain_images[i];
    image_view_info.image = swapchain_images[i];
    sprintf(buff, "swapchain-image-view[%u]", i);
    err = lida_ImageViewCreate(&g_window->images[i].image_view, &image_view_info, "buff");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create image view no. %u with error %s", i, lida_VkResultToString(err));
    }
    framebuffer_info.pAttachments = &g_window->images[i].image_view;
    sprintf(buff, "swapchain-framebuffer[%u]", i);
    err = lida_FramebufferCreate(&g_window->images[i].framebuffer, &framebuffer_info, buff);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create framebuffer no. %u with error %s", i, lida_VkResultToString(err));
    }
  }
  lida_TempFree(swapchain_images);
  // TODO: manage preTransform
  return err;
}

VkResult
CreateRenderPass()
{
  VkAttachmentDescription attachment = {
    .format = g_window->format.format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  VkAttachmentReference reference = {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &reference,
  };
  VkSubpassDependency dependencies[2];
  dependencies[0] = (VkSubpassDependency) {
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
  };
  dependencies[1] = (VkSubpassDependency) {
    .srcSubpass = 0,
    .dstSubpass = VK_SUBPASS_EXTERNAL,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
  };
  VkRenderPassCreateInfo render_pass_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 2,
    .pDependencies = dependencies,
  };
  return lida_RenderPassCreate(&g_window->render_pass, &render_pass_info, "main-render-pass");
}

VkResult
CreateFrames()
{
  VkResult err;
  VkCommandBuffer command_buffers[FRAMES_IN_FLIGHT];
  err = lida_AllocateCommandBuffers(command_buffers, FRAMES_IN_FLIGHT,
                                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                    "main-command-buffer");
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to allocate command buffers with error %s", lida_VkResultToString(err));
    return err;
  }
  VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                   .flags = VK_FENCE_CREATE_SIGNALED_BIT };
  VkDevice dev = lida_GetLogicalDevice();
  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
    g_window->frames[i].cmd = command_buffers[i];
    err = vkCreateSemaphore(dev, &semaphore_info, NULL, &g_window->frames[i].image_available);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create semaphore with error %s", lida_VkResultToString(err));
      return err;
    }
  }
  err = vkCreateSemaphore(dev, &semaphore_info, NULL, &g_window->render_finished_semaphore);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create semaphore with error %s", lida_VkResultToString(err));
    return err;
  }
  err = vkCreateFence(dev, &fence_info, NULL, &g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create fence with error %s", lida_VkResultToString(err));
    return err;
  }
  g_window->frame_counter = 0;
  g_window->current_image = UINT32_MAX;
  return err;
}
