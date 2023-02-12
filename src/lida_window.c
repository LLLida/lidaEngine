/*

  Vulkan swapchain creation and management.

  NOTE: in this file you will often see number 2. That's because we
  have double buffering in this engine. We record commands to command
  while other frame is rendering. Then we wait that frame to be
  finished and swap buffers.
  I think doing '#define FRAMES 2' is redundant.

 */

typedef struct {
  VkImage image;
  VkImageView image_view;
  VkFramebuffer framebuffer;
} Window_Image;

typedef struct {
  VkCommandBuffer cmd;
  VkSemaphore image_available;
  uint64_t submit_time;
} Window_Frame;

typedef struct {

  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  VkRenderPass render_pass;
  uint32_t num_images;
  Window_Image* images;
  Window_Frame frames[2];
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

} Vulkan_Window;

GLOBAL Vulkan_Window* g_window;


/// Functions used primarily by this module

INTERNAL VkResult
CreateMainPass()
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
  return RenderPassCreate(&g_window->render_pass, &render_pass_info, "main-render-pass");
}

INTERNAL VkResult
CreateSwapchain(VkPresentModeKHR present_mode)
{
  VkResult err;
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_device->physical_device,
                                            g_window->surface,
                                            &capabilities);

  // choose surface format
  VkSurfaceFormatKHR old_format = g_window->format;
  uint32_t count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_device->physical_device,
                                       g_window->surface,
                                       &count,
                                       NULL);
  VkSurfaceFormatKHR* formats = PersistentAllocate(sizeof(VkSurfaceFormatKHR) * count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_device->physical_device,
                                       g_window->surface,
                                       &count,
                                       formats);
  g_window->format = formats[0];
  for (uint32_t i = 1; i < count; i++) {
    // try to pick R8G8B8A8_SRGB with nonlinear color space because it looks good
    // TODO: choose in more smart way
    if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
        formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      g_window->format = formats[i];
      break;
    }
  }
  PersistentPop(formats);

  // choose present mode
  vkGetPhysicalDeviceSurfacePresentModesKHR(g_device->physical_device, g_window->surface, &count, NULL);
  VkPresentModeKHR* present_modes = PersistentAllocate(sizeof(VkPresentModeKHR) * count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(g_device->physical_device, g_window->surface, &count, present_modes);
  g_window->present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (uint32_t i = 0; i < count; i++) {
    if (present_modes[i] == present_mode) {
      g_window->present_mode = present_modes[i];
      break;
    }
  }
  PersistentPop(present_modes);

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

  uint32_t queue_family_indices[] = { g_device->graphics_queue_family };
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
  err = vkCreateSwapchainKHR(g_device->logical_device, &swapchain_info, NULL, &g_window->swapchain);
  if (err != VK_SUCCESS) {
    LOG_FATAL("failed to create swapchain with error %s", ToString_VkResult(err));
    return err;
  }
  DebugMarkObject(VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT, (uint64_t)g_window->swapchain,
                  "main-swapchain");

  // recreate render pass if needed
  if (old_format.format != g_window->format.format) {
    if (g_window->render_pass) vkDestroyRenderPass(g_device->logical_device, g_window->render_pass, NULL);
    err = CreateMainPass(g_window);
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to create render pass with errror %s", ToString_VkResult(err));
      return err;
    }
  }

  // get images(VkSwapchainKHR kindly manages their lifetime for us)
  vkGetSwapchainImagesKHR(g_device->logical_device, g_window->swapchain, &g_window->num_images, NULL);
  VkImage* swapchain_images = PersistentAllocate(sizeof(VkImage) * g_window->num_images);
  vkGetSwapchainImagesKHR(g_device->logical_device, g_window->swapchain, &g_window->num_images, swapchain_images);
  // create attachments
  // NOTE: we hope that no device creates more than 8 swapchain images
  g_window->images = PersistentAllocate(sizeof(Window_Image) * 8);
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
    stbsp_sprintf(buff, "swapchain-image-view[%u]", i);
    err = ImageViewCreate(&g_window->images[i].image_view, &image_view_info, buff);
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to create image view no. %u with error %s", i, ToString_VkResult(err));
    }
    framebuffer_info.pAttachments = &g_window->images[i].image_view;
    stbsp_sprintf(buff, "swapchain-framebuffer[%u]", i);
    err = FramebufferCreate(&g_window->images[i].framebuffer, &framebuffer_info, buff);
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to create framebuffer no. %u with error %s", i, ToString_VkResult(err));
    }
  }
  PersistentPop(swapchain_images);

  // TODO: manage preTransform
  // This is crucial for devices you can flip

  return err;
}

INTERNAL VkResult
CreateWindowFrames()
{
  VkResult err;
  VkCommandBuffer command_buffers[2];
  err = AllocateCommandBuffers(command_buffers, 2, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                               "main-command-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate command buffers with error %s", ToString_VkResult(err));
    return err;
  }
  VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                   .flags = VK_FENCE_CREATE_SIGNALED_BIT };
  // NOTE: i didn't debug mark code, because I don't think it is very useful
  for (int i = 0; i < 2; i++) {
    g_window->frames[i].cmd = command_buffers[i];
    err = vkCreateSemaphore(g_device->logical_device, &semaphore_info, NULL, &g_window->frames[i].image_available);
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to create semaphore with error %s", ToString_VkResult(err));
      return err;
    }
  }
  err = vkCreateSemaphore(g_device->logical_device, &semaphore_info, NULL, &g_window->render_finished_semaphore);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create semaphore with error %s", ToString_VkResult(err));
    return err;
  }
  err = vkCreateFence(g_device->logical_device, &fence_info, NULL, &g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create fence with error %s", ToString_VkResult(err));
    return err;
  }
  g_window->frame_counter = 0;
  g_window->current_image = UINT32_MAX;
  return err;
}


/// Functions used by other modules

INTERNAL VkResult
CreateWindow(int vsync)
{
  g_window = PersistentAllocate(sizeof(Vulkan_Window));
  memset(g_window, 0, sizeof(Vulkan_Window));
  PlatformCreateWindow();
  VkPresentModeKHR preferred = (vsync == 0) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
  g_window->surface = PlatformCreateVkSurface(g_device->instance);
  VkResult err = CreateSwapchain(preferred);
  if (err != VK_SUCCESS) {
    LOG_FATAL("failed to create vulkan swapchain with error %s", ToString_VkResult(err));
    goto error;
  }
  err = CreateWindowFrames();
  if (err != VK_SUCCESS) {
    goto error;
  }
  return VK_SUCCESS;
 error:
  PersistentPop(g_window);
  return err;
}

INTERNAL void
DestroyWindow(int free_memory)
{
  for (int i = 0; i < 2; i++) {
    vkDestroySemaphore(g_device->logical_device, g_window->frames[i].image_available, NULL);
  }
  vkDestroyFence(g_device->logical_device, g_window->resources_available_fence, NULL);
  vkDestroySemaphore(g_device->logical_device, g_window->render_finished_semaphore, NULL);

  for (uint32_t i = 0; i < g_window->num_images; i++) {
    vkDestroyFramebuffer(g_device->logical_device, g_window->images[i].framebuffer, NULL);
    vkDestroyImageView(g_device->logical_device, g_window->images[i].image_view, NULL);
  }
  vkDestroyRenderPass(g_device->logical_device, g_window->render_pass, NULL);
  vkDestroySwapchainKHR(g_device->logical_device, g_window->swapchain, NULL);
  vkDestroySurfaceKHR(g_device->instance, g_window->surface, NULL);

  if (free_memory)  {
    PersistentPop(g_window->images);
    PersistentPop(g_window);
  }

  g_window = NULL;
}

INTERNAL VkCommandBuffer
BeginCommands()
{
  Window_Frame* frame = &g_window->frames[g_window->frame_counter % 2];
  VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  vkBeginCommandBuffer(frame->cmd, &begin_info);
  return frame->cmd;
}

INTERNAL VkResult
BeginRenderingToWindow()
{
  Window_Frame* frame = &g_window->frames[g_window->frame_counter % 2];
  VkResult err = vkAcquireNextImageKHR(g_device->logical_device,
                                       g_window->swapchain,
                                       UINT64_MAX,
                                       frame->image_available,
                                       VK_NULL_HANDLE,
                                       &g_window->current_image);
  switch (err)
    {
    case VK_SUCCESS:
      break;
    case VK_SUBOPTIMAL_KHR:
      LOG_WARN("acquire next image: got VK_SUBOPTIMAL_KHR");
      break;
    default:
      LOG_ERROR("failed to acquire next swapchain image with error %s", ToString_VkResult(err));
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

INTERNAL VkResult
PresentToScreen()
{
  Window_Frame* frame = &g_window->frames[g_window->frame_counter % 2];
  VkResult err;
  // wait till commands from previous frame are done, so we can safely use GPU resources
  err = vkWaitForFences(g_device->logical_device, 1, &g_window->resources_available_fence,
                        VK_TRUE, UINT64_MAX);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to wait for fence with error %s", ToString_VkResult(err));
    return err;
  }
  err = vkResetFences(g_device->logical_device, 1, &g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to reset fence before presenting image with error %s", ToString_VkResult(err));
    return err;
  }
  // submit commands
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
  err = QueueSubmit(&submit_info, 1, g_window->resources_available_fence);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to submit commands to graphics queue with error %s", ToString_VkResult(err));
    return err;
  }
  // update FPS
  frame->submit_time = PlatformGetPerformanceCounter();
  g_window->frames_per_second = (float)PlatformGetPerformanceFrequency() / (float)(frame->submit_time - g_window->last_submit);
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
  err = QueuePresent(&present_info);
  if (err != VK_SUCCESS && err != VK_SUBOPTIMAL_KHR) {
    LOG_ERROR("queue failed to present with error %s", ToString_VkResult(err));
  }
  g_window->frame_counter++;
  g_window->current_image = UINT32_MAX;
  return err;
}
