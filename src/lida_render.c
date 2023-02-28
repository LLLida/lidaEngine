/*
  lida_render.c
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

  Mat4 camera_projview;
  Mat4 camera_projection;
  Mat4 camera_view;
  Mat4 camera_invproj;
  Mat4 light_space;
  Vec3 sun_dir;
  float sun_ambient;

} Scene_Data_Struct;

typedef struct {

  Video_Memory memory;
  VkImage image;
  VkImageView image_view;
  VkFramebuffer framebuffer;
  VkRenderPass render_pass;
  VkExtent2D extent;
  VkDescriptorSet scene_data_set;
  VkDescriptorSet shadow_set;

} Shadow_Pass;

// this will help us to do hot resource reloading
typedef struct {

  struct { uint64_t handle; VkObjectType type; uint64_t frame; } objs[32];
  uint32_t left;
  uint32_t count;

} Deletion_Queue;

typedef void(*Pipeline_Create_Func)(Pipeline_Desc* description);

typedef struct {

  VkPipeline pipeline;
  VkPipelineLayout layout;
  // this is only accessed when compiling pipeline
  Pipeline_Create_Func create_func;
  const char* vertex_shader;
  const char* fragment_shader;

} Pipeline_Program;
DECLARE_COMPONENT(Pipeline_Program);

// 16 bytes
typedef struct {

  Vec3 position;
  uint32_t color;

} Vertex_X3C;

// this just draws lines
typedef struct {

  Video_Memory cpu_memory;
  VkBuffer vertex_buffer;
  Vertex_X3C* pVertices;
  uint32_t max_vertices;
  uint32_t vertex_offset;

} Debug_Drawer;

// we always pass color as uint32_t and decompress it on GPU
#define PACK_COLOR(r, g, b, a) ((a) << 24) | ((b) << 16) | ((g) << 8) | (r)


/// private functions

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

INTERNAL VkResult
SH_CreateRenderPass(Shadow_Pass* pass, const Forward_Pass* fwd_pass)
{
  VkAttachmentDescription attachment = {
    .format = fwd_pass->depth_format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
  };
  VkAttachmentReference depth_reference = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .pDepthStencilAttachment = &depth_reference,
  };
  VkSubpassDependency dependencies[2];
  dependencies[0] = (VkSubpassDependency) {
    0, VK_SUBPASS_EXTERNAL,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    VK_DEPENDENCY_BY_REGION_BIT
  };
  dependencies[1] = (VkSubpassDependency) {
    0, VK_SUBPASS_EXTERNAL,
    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
    VK_DEPENDENCY_BY_REGION_BIT
  };
  VkRenderPassCreateInfo render_pass_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = ARR_SIZE(dependencies),
    .pDependencies = dependencies,
  };
  return CreateRenderPass(&pass->render_pass, &render_pass_info, "shadow/render-pass");
}

INTERNAL VkResult
SH_CreateAttachments(Shadow_Pass* pass, const Forward_Pass* fwd_pass)
{
  // typical Vulkan boring stuff ... 😴
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = fwd_pass->depth_format,
    .extent = (VkExtent3D) { pass->extent.width, pass->extent.height, 1 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkResult err = CreateImage(&pass->image, &image_info, "shadow/image");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create image for shadow attachment with error %s",
                   ToString_VkResult(err));
    return err;
  }
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(g_device->logical_device, pass->image, &requirements);
  err = AllocateVideoMemory(&pass->memory, requirements.size,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.memoryTypeBits,
                            "shadow/attachment-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory for shadow attachment with error %s",
              ToString_VkResult(err));
    return err;
  }
  // bind image to memory
  ImageBindToMemory(&pass->memory, pass->image, &requirements);
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = pass->image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = fwd_pass->depth_format,
    .subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
  };
  err = CreateImageView(&pass->image_view, &image_view_info, "shadow/image-view");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create image view for shadow attachment with error %s)",
              ToString_VkResult(err));
    return err;
  }
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = pass->render_pass,
    .attachmentCount = 1,
    .pAttachments = &pass->image_view,
    .width = pass->extent.width,
    .height = pass->extent.height,
    .layers = 1,
  };
  err = CreateFramebuffer(&pass->framebuffer, &framebuffer_info, "shadow/framebuffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create framebuffer for shadow pass with error %s",
              ToString_VkResult(err));
    return err;
  }
  LOG_TRACE("allocated %u bytes for shadow map", (uint32_t)requirements.size);
  return err;
}

INTERNAL VkResult
SH_AllocateDescriptorSets(Shadow_Pass* pass, const Forward_Pass* fwd_pass)
{
  // allocate descriptor sets
  VkDescriptorSetLayoutBinding bindings[4];
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };
  VkResult err = AllocateDescriptorSets(bindings, 1, &pass->scene_data_set, 1, 0,
                                        "shadow/scene-data");
  if (err == VK_SUCCESS) {
    bindings[0] = (VkDescriptorSetLayoutBinding) {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    err = AllocateDescriptorSets(bindings, 1, &pass->shadow_set, 1, 0,
                                 "shadow-map-set");
  }
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor sets with error %s", ToString_VkResult(err));
    return err;
  }
  // update descriptor sets
  VkWriteDescriptorSet write_sets[2];
  VkDescriptorBufferInfo buffer_info = {
    .buffer = fwd_pass->uniform_buffer,
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
    .imageView = pass->image_view,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
  };
  write_sets[1] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = pass->shadow_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_info
  };
  UpdateDescriptorSets(write_sets, ARR_SIZE(write_sets));
  return err;
}


/// Functions used by other modules

INTERNAL VkResult
CreateForwardPass(Forward_Pass* pass, uint32_t width, uint32_t height, VkSampleCountFlagBits samples)
{
  PROFILE_FUNCTION();
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

INTERNAL void
ResizeForwardPass(Forward_Pass* pass, uint32_t width, uint32_t height)
{
  PROFILE_FUNCTION();
  // destroy attachments
  vkDestroyFramebuffer(g_device->logical_device, pass->framebuffer, NULL);
  vkDestroyImageView(g_device->logical_device, pass->depth_image_view, NULL);
  vkDestroyImageView(g_device->logical_device, pass->color_image_view, NULL);
  if (pass->resolve_image_view)
    vkDestroyImageView(g_device->logical_device, pass->resolve_image_view, NULL);
  vkDestroyImage(g_device->logical_device, pass->depth_image, NULL);
  vkDestroyImage(g_device->logical_device, pass->color_image, NULL);
  if (pass->resolve_image)
    vkDestroyImage(g_device->logical_device, pass->resolve_image, NULL);
    // create attachments
  pass->render_extent = (VkExtent2D) {width, height};
  VkResult err = FWD_CreateAttachments(pass, width, height);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to resize forward pass attachments");
  }
  // allocate a new descriptor set and update it
  VkDescriptorSetLayoutBinding binding = {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
  };
  err = AllocateDescriptorSets(&binding, 1, &pass->resulting_image_set, 1, 1,
                               "forward/resulting_image_set");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor set with error %s", ToString_VkResult(err));
  }
  VkDescriptorImageInfo image_info = {
    .imageView = (pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? pass->color_image_view : pass->resolve_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
  };
  VkWriteDescriptorSet write_set = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = pass->resulting_image_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_info,
  };
  UpdateDescriptorSets(&write_set, 1);
}

INTERNAL void
SendForwardPassData(Forward_Pass* pass)
{
  VkResult err = vkFlushMappedMemoryRanges(g_device->logical_device,
                                           1, &pass->uniform_buffer_range);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to flush memory with error %s", ToString_VkResult(err));
  }
}


INTERNAL void
BeginForwardPass(Forward_Pass* pass, VkCommandBuffer cmd, float clear_color[4])
{
  PROFILE_FUNCTION();
  VkClearValue clearValues[2];
  // color attachment
  memcpy(clearValues[0].color.float32, clear_color, sizeof(float) * 4);
  // depth attachment
  clearValues[1].depthStencil.depth = 0.0f;
  clearValues[1].depthStencil.stencil = 0;
  VkRect2D render_area = { .offset = {0, 0},
                           .extent = pass->render_extent };
  VkRenderPassBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = pass->render_pass,
    .framebuffer = pass->framebuffer,
    .pClearValues = clearValues,
    .clearValueCount = 2,
    .renderArea = render_area
  };
  vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport = {
    .x = 0.0f, .y = 0.0f,
    .width = (float)render_area.extent.width,
    .height = (float)render_area.extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &render_area);
}

INTERNAL VkResult
CreateShadowPass(Shadow_Pass* pass, const Forward_Pass* fwd_pass, uint32_t width, uint32_t height)
{
  PROFILE_FUNCTION();
  pass->extent.width = width;
  pass->extent.height = height;
  VkResult err = SH_CreateRenderPass(pass, fwd_pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create render pass for rendering to shadow map with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = SH_CreateAttachments(pass, fwd_pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create attachments for rendering to shadow map with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = SH_AllocateDescriptorSets(pass, fwd_pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor sets for rendering to shadow map with error %s",
              ToString_VkResult(err));
    return err;
  }
  return err;
}

INTERNAL void
DestroyShadowPass(Shadow_Pass* pass)
{
  vkDestroyFramebuffer(g_device->logical_device, pass->framebuffer, NULL);
  vkDestroyImageView(g_device->logical_device, pass->image_view, NULL);
  vkDestroyImage(g_device->logical_device, pass->image, NULL);
  vkDestroyRenderPass(g_device->logical_device, pass->render_pass, NULL);

  FreeVideoMemory(&pass->memory);
}

INTERNAL void
BeginShadowPass(Shadow_Pass* pass, VkCommandBuffer cmd)
{
  VkClearValue clearValue;
  clearValue.depthStencil.depth = 0.0f;
  clearValue.depthStencil.stencil = 0;
  VkRect2D render_area = { .offset = { 0, 0 },
                           .extent = pass->extent };
  VkRenderPassBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = pass->render_pass,
    .framebuffer = pass->framebuffer,
    .pClearValues = &clearValue,
    .clearValueCount = 1,
    .renderArea = render_area
  };
  vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

INTERNAL void
ShadowPassViewport(Shadow_Pass* pass, VkViewport** p_viewport, VkRect2D** p_scissor)
{
  GLOBAL VkViewport viewport;
  GLOBAL VkRect2D rect;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)pass->extent.width;
  viewport.height = (float)pass->extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  rect.offset.x = 0;
  rect.offset.y = 0;
  rect.extent.width = pass->extent.width;
  rect.extent.height = pass->extent.height;
  *p_viewport = &viewport;
  *p_scissor = &rect;
}

INTERNAL void
cmdBindProgram(VkCommandBuffer cmd, const Pipeline_Program* prog,
               uint32_t descriptor_set_count, VkDescriptorSet* descriptor_sets)
{
  if (descriptor_set_count > 0) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->layout,
                            0, descriptor_set_count, descriptor_sets, 0, NULL);
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
}

INTERNAL int
AddForDeletion(Deletion_Queue* dq, uint64_t handle, VkObjectType type)
{
  const size_t max = ARR_SIZE(dq->objs);
  if (dq->count == max) {
    LOG_WARN("deletion queue is out of space");
    return -1;
  }
  size_t id = (dq->left+dq->count) % max;
  dq->objs[id].handle = handle;
  dq->objs[id].type = type;
  dq->objs[id].frame = g_window->frame_counter;
  dq->count++;
  return 0;
}

INTERNAL void
UpdateDeletionQueue(Deletion_Queue* dq)
{
  const size_t max = ARR_SIZE(dq->objs);
  while (dq->count > 0) {
    size_t id = dq->left % max;
    if (dq->objs[id].frame + 2 > g_window->frame_counter)
      break;
    switch (dq->objs[id].type)
      {
#define CASE(upper, camel) case VK_OBJECT_TYPE_##upper:\
        vkDestroy##camel (g_device->logical_device, (Vk##camel)dq->objs[id].handle, NULL);\
        break

        CASE(PIPELINE, Pipeline);
        CASE(IMAGE, Image);
        CASE(IMAGE_VIEW, ImageView);
        CASE(FRAMEBUFFER, Framebuffer);
        CASE(BUFFER, Buffer);

#undef CASE

      default:
        LOG_WARN("deletion queue: undefined type object %d", dq->objs[id].type);
        break;

      }
    dq->left = (dq->left+1) % max;
    dq->count--;
  }
}

INTERNAL VkResult
CreateDebugDrawer(Debug_Drawer* drawer, uint32_t max_vertices)
{
  drawer->max_vertices = max_vertices;
  VkResult err = CreateBuffer(&drawer->vertex_buffer, max_vertices*sizeof(Vertex_X3C),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "debug-draw-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create debug drawer with error %s", ToString_VkResult(err));
    return err;
  }
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(g_device->logical_device, drawer->vertex_buffer, &requirements);
  err = AllocateVideoMemory(&drawer->cpu_memory, requirements.size,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            requirements.memoryTypeBits,
                            "debug-draw-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate memory for debug draws with error %s", ToString_VkResult(err));
    return err;
  }
  err = BufferBindToMemory(&drawer->cpu_memory, drawer->vertex_buffer,
                           &requirements, (void**)&drawer->pVertices,
                           NULL);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to bind vertex buffer to memory for debug draws with error %s",
              ToString_VkResult(err));
    return err;
  }
  return VK_SUCCESS;
}

INTERNAL void
DestroyDebugDrawer(Debug_Drawer* drawer)
{
  vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);

  FreeVideoMemory(&drawer->cpu_memory);
}

INTERNAL void
NewDebugDrawerFrame(Debug_Drawer* drawer)
{
  drawer->vertex_offset = 0;
}

INTERNAL void
RenderDebugLines(Debug_Drawer* drawer, VkCommandBuffer cmd)
{
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &drawer->vertex_buffer, &offset);
  vkCmdDraw(cmd, drawer->vertex_offset, 1, 0, 0);
}

INTERNAL void
AddDebugLine(Debug_Drawer* drawer, const Vec3* start, const Vec3* end, uint32_t color)
{
  if (drawer->vertex_offset + 2 >= drawer->max_vertices) {
    LOG_WARN("debug drawer is out of space");
    return;
  }
  drawer->pVertices[drawer->vertex_offset++] = (Vertex_X3C) {
    .position = *start,
    .color = color,
  };
  drawer->pVertices[drawer->vertex_offset++] = (Vertex_X3C) {
    .position = *end,
    .color = color,
  };
}

INTERNAL void
PipelineDebugDrawVertices(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                          const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings)
{
  GLOBAL VkVertexInputBindingDescription g_bindings[1] = {
    { 0, sizeof(Vertex_X3C), VK_VERTEX_INPUT_RATE_VERTEX },
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes[2] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 0, VK_FORMAT_R32_UINT, offsetof(Vertex_X3C, color) },
  };
  *bindings = g_bindings;
  *num_bindings = ARR_SIZE(g_bindings);
  *attributes = g_attributes;
  *num_attributes = ARR_SIZE(g_attributes);
}

INTERNAL void
CalculateObjectOBB(const Vec3* half_size, const Transform* transform, Vec3 corners[8])
{
  Vec3 box[3];
  box[0] = VEC3_CREATE(half_size->x, 0.0f, 0.0f);
  box[1] = VEC3_CREATE(0.0f, half_size->y, 0.0f);
  box[2] = VEC3_CREATE(0.0f, 0.0f, half_size->z);
  RotateByQuat(&box[0], &transform->rotation, &box[0]);
  RotateByQuat(&box[1], &transform->rotation, &box[1]);
  RotateByQuat(&box[2], &transform->rotation, &box[2]);
  const Vec3 muls[8] = {
    { -1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f },
    { 1.0f, -1.0f, 1.0f },
    { 1.0f, 1.0f, -1.0f },
    { 1.0f, 1.0f, 1.0f },
  };
  for (size_t i = 0; i < 8; i++) {
    Vec3 basis[3];
    basis[0] = VEC3_MUL(box[0], muls[i].x * (transform->scale + 0.1f));
    basis[1] = VEC3_MUL(box[1], muls[i].y * (transform->scale + 0.1f));
    basis[2] = VEC3_MUL(box[2], muls[i].z * (transform->scale + 0.1f));
    corners[i].x = basis[0].x + basis[1].x + basis[2].x + transform->position.x;
    corners[i].y = basis[0].y + basis[1].y + basis[2].y + transform->position.y;
    corners[i].z = basis[0].z + basis[1].z + basis[2].z + transform->position.z;
  }
}
