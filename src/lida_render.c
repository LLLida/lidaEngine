/*
  A classic forward renderer. I might switch to gbuffer in future.
 */

typedef struct {

  Video_Memory gpu_memory;
  Video_Memory cpu_memory;
  VkImage color_image;
  VkImage depth_image;
  VkImage resolve_image;
  VkImageView color_image_view;
  VkImageView depth_image_view;
  VkImageView resolve_image_view;
  VkFramebuffer framebuffer;
  VkRenderPass render_pass;
  VkBuffer uniform_buffer;
  size_t uniform_buffer_size;
  void* uniform_buffer_mapped;
  VkDescriptorSet scene_data_set;
  VkDescriptorSet resulting_image_set;
  VkFormat color_format;
  VkFormat depth_format;
  VkSampleCountFlagBits msaa_samples;
  VkExtent2D render_extent;
  VkMappedMemoryRange uniform_buffer_range;

} Forward_Pass;

typedef struct {

  /*lida_Mat4 camera_projview;
  lida_Mat4 camera_projection;
  lida_Mat4 camera_view;
  lida_Mat4 camera_invproj;
  lida_Mat4 light_space;
  lida_Vec3 sun_dir;*/
  float sun_ambient;

} Scene_Data_Struct;


/// Functions primarily by this module

INTERNAL void
FWD_ChooseFromats(Forward_Pass* pass, VkSampleCountFlagBits samples)
{
  VkFormat hdr_formats[] = {
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,
  };
  pass->color_format = FindSupportedFormat(hdr_formats, ARR_SIZE(hdr_formats),
                                                 VK_IMAGE_TILING_OPTIMAL,
                                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  VkFormat depth_formats[] = {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
  };
  pass->depth_format = FindSupportedFormat(depth_formats, ARR_SIZE(depth_formats),
                                                 VK_IMAGE_TILING_OPTIMAL,
                                                 VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  pass->msaa_samples = MaxSampleCount(samples);
  LOG_TRACE("Renderer formats(samples=%d): color=%s, depth=%s",
            (int)pass->msaa_samples,
            ToString_VkFormat(pass->color_format),
            ToString_VkFormat(pass->depth_format));
}

INTERNAL VkResult
FWD_CreateRenderPass(Forward_Pass* pass)
{
  VkAttachmentDescription attachments[3];
  attachments[0] = (VkAttachmentDescription) {
    .format = pass->color_format,
    .samples = pass->msaa_samples,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  };
  attachments[1] = (VkAttachmentDescription) {
    .format = pass->depth_format,
    .samples = pass->msaa_samples,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    // if we msaa is enabled then also configure resolve attachment
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[2] = (VkAttachmentDescription) {
      .format = pass->color_format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
  }
  VkAttachmentReference color_references[1];
  color_references[0] = (VkAttachmentReference) { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkAttachmentReference depth_reference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
  VkAttachmentReference resolve_references[1];
  resolve_references[0] = (VkAttachmentReference) { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = ARR_SIZE(color_references),
    .pColorAttachments = color_references,
    .pDepthStencilAttachment = &depth_reference,
    .pResolveAttachments = (pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? NULL : resolve_references,
  };
  VkSubpassDependency dependencies[2];
  dependencies[0] = (VkSubpassDependency) { VK_SUBPASS_EXTERNAL, 0,
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_DEPENDENCY_BY_REGION_BIT };
  dependencies[1] = (VkSubpassDependency) { 0, VK_SUBPASS_EXTERNAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                      VK_DEPENDENCY_BY_REGION_BIT };
  VkRenderPassCreateInfo render_pass_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 2 + (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT),
    .pAttachments = attachments,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = ARR_SIZE(dependencies),
    .pDependencies = dependencies,
  };
  return CreateRenderPass(&pass->render_pass, &render_pass_info, "forward/render-pass");
}

INTERNAL VkResult
FWD_CreateAttachments(Forward_Pass* pass, uint32_t width, uint32_t height)
{
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .mipLevels = 1,
    .arrayLayers = 1,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkResult err;
  // create color image
  image_info.format = pass->color_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) {
    image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  } else {
    // TODO: try to use memory with LAZILY_ALLOCATED property
    image_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  }
  image_info.samples = pass->msaa_samples;
  err = CreateImage(&pass->color_image, &image_info, "forward/color-image");
  if (err != VK_SUCCESS) {
    return err;
  }
  // create depth image
  image_info.format = pass->depth_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.samples = pass->msaa_samples;
  err = CreateImage(&pass->depth_image, &image_info, "forward/depth-image");
  if (err != VK_SUCCESS) {
    return err;
  }
  // create resolve image if msaa_samples > 1
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    // FIXME: should we use another format for resolve image?
    image_info.format = pass->color_format;
    image_info.extent = (VkExtent3D) { width, height, 1 };
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    err = CreateImage(&pass->resolve_image, &image_info, "forward/resolve-image");
    if (err != VK_SUCCESS) {
      return err;
    }
  } else {
    pass->resolve_image = VK_NULL_HANDLE;
  }
  // allocate memory
  VkMemoryRequirements image_requirements[3];
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(g_device->logical_device,
                               pass->color_image, &image_requirements[0]);
  vkGetImageMemoryRequirements(g_device->logical_device,
                               pass->color_image, &image_requirements[1]);
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    vkGetImageMemoryRequirements(g_device->logical_device,
                                 pass->resolve_image, &image_requirements[2]);
  }
  MergeMemoryRequirements(image_requirements, 2 + (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT), &requirements);
  if (requirements.size > pass->gpu_memory.size) {
    if (pass->gpu_memory.handle) {
      // free GPU memory
      FreeVideoMemory(&pass->gpu_memory);
    }
    err = AllocateVideoMemory(&pass->gpu_memory, requirements.size,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.memoryTypeBits,
                                   "forward/attachment-memory");
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to allocate GPU memory for attachments with error %s",
                ToString_VkResult(err));
      return err;
    }
  } else {
    ResetVideoMemory(&pass->gpu_memory);
  }
  // bind images to memory
  ImageBindToMemory(&pass->gpu_memory, pass->color_image, &image_requirements[0]);
  ImageBindToMemory(&pass->gpu_memory, pass->depth_image, &image_requirements[1]);
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    ImageBindToMemory(&pass->gpu_memory, pass->resolve_image, &image_requirements[2]);
  }
  // create image views
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
  };
  image_view_info.image = pass->color_image;
  image_view_info.format = pass->color_format;
  image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  err = CreateImageView(&pass->color_image_view, &image_view_info, "forward/color-image-view");
  if (err != VK_SUCCESS) {
    return err;
  }
  image_view_info.image = pass->depth_image;
  image_view_info.format = pass->depth_format;
  image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
  err = CreateImageView(&pass->depth_image_view, &image_view_info, "forward/depth-image-view");
  if (err != VK_SUCCESS) {
    return err;
  }
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    image_view_info.image = pass->resolve_image;
    image_view_info.format = pass->color_format;
    image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    err = CreateImageView(&pass->resolve_image_view, &image_view_info, "forward/resolve-image-view");
    if (err != VK_SUCCESS) {
      return err;
    }
  } else {
    pass->resolve_image_view = VK_NULL_HANDLE;
  }
  // create framebuffer
  VkImageView attachments[3] = { pass->color_image_view, pass->depth_image_view, pass->resolve_image_view };
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = pass->render_pass,
    .attachmentCount = 2 + (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT),
    .pAttachments = attachments,
    .width = width,
    .height = height,
    .layers = 1,
  };
  err = CreateFramebuffer(&pass->framebuffer, &framebuffer_info, "forward/framebuffer");
  if (err != VK_SUCCESS) {
    return err;
  }
  LOG_TRACE("allocated %u bytes for attachments", (uint32_t)requirements.size);
  return err;
}

INTERNAL VkResult
FWD_CreateBuffers(Forward_Pass* pass)
{
  VkResult err = CreateBuffer(&pass->uniform_buffer, pass->uniform_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              "forward/uniform");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create uniform buffer with error %s", ToString_VkResult(err));
    return err;
  }
  VkMemoryRequirements buffer_requirements[1];
  vkGetBufferMemoryRequirements(g_device->logical_device, pass->uniform_buffer,
                                &buffer_requirements[0]);
  VkMemoryRequirements requirements;
  MergeMemoryRequirements(buffer_requirements, 1, &requirements);
  err = AllocateVideoMemory(&pass->cpu_memory, requirements.size,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT, requirements.memoryTypeBits,
                            "forward/buffer-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory for buffers with error %s", ToString_VkResult(err));
    return err;
  }
  err = BufferBindToMemory(&pass->cpu_memory, pass->uniform_buffer, &buffer_requirements[0],
                                &pass->uniform_buffer_mapped, &pass->uniform_buffer_range);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind uniform buffer to memory with error %s", ToString_VkResult(err));
    return err;
  }
  LOG_TRACE("allocated %u bytes for uniform buffer", (uint32_t)requirements.size);
  return err;
}

INTERNAL VkResult
FWD_AllocateDescriptorSets(Forward_Pass* pass)
{
  // allocate descriptor sets
  VkDescriptorSetLayoutBinding bindings[4];
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  VkResult err = AllocateDescriptorSets(bindings, 1, &pass->scene_data_set, 1, 0, "forward/scene-data");
  if (err == VK_SUCCESS) {
    bindings[0] = (VkDescriptorSetLayoutBinding) {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    err = AllocateDescriptorSets(bindings, 1, &pass->resulting_image_set, 1, 1, "forward/resulting-image");
  }
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor sets with error %s", ToString_VkResult(err));
    return err;
  }
  // update descriptor sets
  VkWriteDescriptorSet write_sets[2];
  VkDescriptorBufferInfo buffer_info = {
    .buffer = pass->uniform_buffer,
    .offset = 0,
    .range = sizeof(Scene_Data_Struct)
  };
  write_sets[0] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = pass->scene_data_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .pBufferInfo = &buffer_info,
  };
  VkDescriptorImageInfo image_info = {
    .imageView = (pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? pass->color_image_view : pass->resolve_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
  };
  write_sets[1] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = pass->resulting_image_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_info
  };
  UpdateDescriptorSets(write_sets, 2);
  return err;
}


/// Functions used by other modules

INTERNAL VkResult
CreateForwardPass(Forward_Pass* pass, uint32_t width, uint32_t height, VkSampleCountFlagBits samples)
{
  memset(pass, 0, sizeof(Forward_Pass));
  pass->render_extent = (VkExtent2D) {width, height};
  FWD_ChooseFromats(pass, samples);
  VkResult err = FWD_CreateRenderPass(pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create render pass with error %s", ToString_VkResult(err));
    return err;
  }
  err = FWD_CreateAttachments(pass, width, height);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create attachments");
    return err;
  }
  pass->uniform_buffer_size = 2048;
  err = FWD_CreateBuffers(pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create buffers");
    return err;
  }
  err = FWD_AllocateDescriptorSets(pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor sets");
  return err;
  }
  return VK_SUCCESS;
}

INTERNAL void
DestroyForwardPass(Forward_Pass* pass)
{
  vkDestroyBuffer(g_device->logical_device, pass->uniform_buffer, NULL);

  vkDestroyFramebuffer(g_device->logical_device, pass->framebuffer, NULL);
  vkDestroyImageView(g_device->logical_device, pass->depth_image_view, NULL);
  vkDestroyImageView(g_device->logical_device, pass->color_image_view, NULL);
  if (pass->resolve_image_view)
    vkDestroyImageView(g_device->logical_device, pass->resolve_image_view, NULL);
  vkDestroyImage(g_device->logical_device, pass->depth_image, NULL);
  vkDestroyImage(g_device->logical_device, pass->color_image, NULL);
  if (pass->resolve_image)
    vkDestroyImage(g_device->logical_device, pass->resolve_image, NULL);
  vkDestroyRenderPass(g_device->logical_device, pass->render_pass, NULL);

  FreeVideoMemory(&pass->cpu_memory);
  FreeVideoMemory(&pass->gpu_memory);
}
