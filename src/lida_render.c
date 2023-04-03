/*
  lida_render.c
  A classic forward renderer. I might switch to gbuffer in future.
 */

typedef struct {

  VkImage image;
  VkImageView mips[15];
  uint32_t num_mips;
  VkDescriptorSet reduce_sets[15];
  VkDescriptorSet debug_sets[15];
  VkDescriptorSet read_set;

} Depth_Pyramid;

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
  Depth_Pyramid depth_pyramid;
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

Forward_Pass* g_forward_pass;

typedef struct {

  Mat4 camera_projview;
  Mat4 camera_projection;
  Mat4 camera_view;
  Mat4 camera_invproj;
  Mat4 light_space;
  Vec3 sun_dir;
  float sun_ambient;
  Vec3 camera_pos;

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

Shadow_Pass* g_shadow_pass;

// this will help us to do hot resource reloading
typedef struct {

  struct { uint64_t handle; VkObjectType type; uint64_t frame; } objs[32];
  uint32_t left;
  uint32_t count;

} Deletion_Queue;

Deletion_Queue* g_deletion_queue;

typedef void(*Pipeline_Create_Func)(Pipeline_Desc* description);

typedef struct {

  VkPipeline pipeline;
  VkPipelineLayout layout;
  // this is only accessed when compiling pipeline
  Pipeline_Create_Func create_func;
  const char* vertex_shader;
  const char* fragment_shader;

} Graphics_Pipeline;
DECLARE_COMPONENT(Graphics_Pipeline);

typedef struct {

  VkPipeline pipeline;
  VkPipelineLayout layout;
  const char* shader;

} Compute_Pipeline;
DECLARE_COMPONENT(Compute_Pipeline);

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

// HACK: for some reason vulkan doesn't define VK_OBJECT_TYPE_MEMORY so we define it ourselves
#define VK_OBJECT_TYPE_MEMORY 50

INTERNAL VkResult
ReallocateMemoryIfNeeded(Video_Memory* memory, Deletion_Queue* dq, const VkMemoryRequirements* requirements,
                         VkMemoryPropertyFlags flags, const char* marker)
{
  if (memory->handle &&
      ALIGN_TO(memory->offset, requirements->alignment) + requirements->size <= memory->size) {
    return VK_SUCCESS;
  }
  if (memory->handle != VK_NULL_HANDLE) {
    if (memory->mapped) {
      vkUnmapMemory(g_device->logical_device, memory->handle);
    }
    AddForDeletion(dq, (uint64_t)memory->handle, VK_OBJECT_TYPE_MEMORY);
  }
  return AllocateVideoMemory(memory, requirements->size, flags, requirements->memoryTypeBits, marker);
}

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
    .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
CreateDepthPyramidImage(Depth_Pyramid* pyramid, uint32_t width, uint32_t height)
{
  pyramid->num_mips = Log2_u32((width > height) ? width : height)+1;
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_R32_SFLOAT,
    .extent.width = NearestPow2(width/2),
    .extent.height = NearestPow2(height/2),
    .extent.depth = 1,
    .mipLevels = pyramid->num_mips,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .usage = VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  return CreateImage(&pyramid->image, &image_info, "depth-pyramid");
}

INTERNAL VkResult
CreateDepthPyramidMips(Depth_Pyramid* pyramid)
{
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .image = pyramid->image,
    .format = VK_FORMAT_R32_SFLOAT,
    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
 };
  for (uint32_t i = 0; i < pyramid->num_mips; i++) {
    image_view_info.subresourceRange.baseMipLevel = i;
    char buff[32];
    stbsp_sprintf(buff, "depth-mip[%u]", i);
    VkResult err = CreateImageView(&pyramid->mips[i], &image_view_info, buff);
    if (err != VK_SUCCESS) {
      return err;
    }
  }
  return VK_SUCCESS;
}

INTERNAL VkResult
AllocateDepthPyramidDescriptorSets(Depth_Pyramid* pyramid, VkImageView depth_image_view)
{
  VkResult err;
  VkDescriptorSetLayoutBinding bindings[2];
  // allocate descriptor sets for depth reduction pass
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  bindings[1] = (VkDescriptorSetLayoutBinding) {
    .binding         = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  err = AllocateDescriptorSets(bindings, 2, pyramid->reduce_sets, pyramid->num_mips, 1, "depth-pyramid-set");
  if (err != VK_SUCCESS) return err;
  // allocate descriptor sets for visualizing depth pyramid
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  err = AllocateDescriptorSets(bindings, 1, pyramid->debug_sets, pyramid->num_mips, 1, "depth-pyramid-debug-set");
  if (err != VK_SUCCESS) return err;
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  err = AllocateDescriptorSets(bindings, 1, &pyramid->read_set, 1, 1, "depth-pyramid-read-set");
  VkWriteDescriptorSet write_sets[46];
  VkDescriptorImageInfo image_infos[46];
  for (uint32_t i = 0; i < pyramid->num_mips; i++) {
    // CLEANUP: I should make a macro, too much boilerplate...
    image_infos[3*i] = (VkDescriptorImageInfo) {
      .imageView = (i == 0) ? depth_image_view : pyramid->mips[i-1],
      .imageLayout = (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
      .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
    };
    write_sets[3*i] = (VkWriteDescriptorSet) {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = pyramid->reduce_sets[i],
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image_infos[3*i]
    };
    image_infos[3*i+1] = (VkDescriptorImageInfo) {
      .imageView = pyramid->mips[i],
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .sampler = VK_NULL_HANDLE
    };
    write_sets[3*i+1] = (VkWriteDescriptorSet) {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = pyramid->reduce_sets[i],
      .dstBinding = 1,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &image_infos[3*i+1]
    };
    image_infos[3*i+2] = (VkDescriptorImageInfo) {
      .imageView = pyramid->mips[i],
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
    };
    write_sets[3*i+2] = (VkWriteDescriptorSet) {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = pyramid->debug_sets[i],
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image_infos[3*i+2]
    };
  }
  image_infos[pyramid->num_mips*3] = (VkDescriptorImageInfo) {
    .imageView = pyramid->mips[0],
    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
  };
  write_sets[pyramid->num_mips*3] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = pyramid->read_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_infos[pyramid->num_mips*3],
  };
  UpdateDescriptorSets(write_sets, pyramid->num_mips*3 + 1);
  return err;
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
  // create depth pyramid
  err = CreateDepthPyramidImage(&pass->depth_pyramid, width, height);
  if (err != VK_SUCCESS) {
      return err;
  }
  // allocate memory
  VkMemoryRequirements image_requirements[4];
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(g_device->logical_device,
                               pass->color_image, &image_requirements[0]);
  vkGetImageMemoryRequirements(g_device->logical_device,
                               pass->color_image, &image_requirements[1]);
  vkGetImageMemoryRequirements(g_device->logical_device,
                               pass->depth_pyramid.image, &image_requirements[2]);
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    vkGetImageMemoryRequirements(g_device->logical_device,
                                 pass->resolve_image, &image_requirements[3]);
  }

  MergeMemoryRequirements(image_requirements, 3 + (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT), &requirements);
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
  ImageBindToMemory(&pass->gpu_memory, pass->depth_pyramid.image, &image_requirements[2]);
  if (pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    ImageBindToMemory(&pass->gpu_memory, pass->resolve_image, &image_requirements[3]);
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
  err = CreateDepthPyramidMips(&pass->depth_pyramid);
  if (err != VK_SUCCESS) return err;
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
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE)
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
  err = AllocateDepthPyramidDescriptorSets(&pass->depth_pyramid, pass->depth_image_view);
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
SH_CreateAttachments(Shadow_Pass* pass, const Forward_Pass* fwd_pass, Deletion_Queue* dq)
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
  err = ReallocateMemoryIfNeeded(&pass->memory, dq, &requirements,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "shadow/attachment-memory");
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
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                          VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
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
  for (uint32_t i = 0; i < pass->depth_pyramid.num_mips; i++)
    vkDestroyImageView(g_device->logical_device, pass->depth_pyramid.mips[i], NULL);
  vkDestroyImage(g_device->logical_device, pass->depth_image, NULL);
  vkDestroyImage(g_device->logical_device, pass->color_image, NULL);
  if (pass->resolve_image)
    vkDestroyImage(g_device->logical_device, pass->resolve_image, NULL);
  vkDestroyImage(g_device->logical_device, pass->depth_pyramid.image, NULL);
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
    .sampler = GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE)
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
  pass->memory.handle = VK_NULL_HANDLE;
  err = SH_CreateAttachments(pass, fwd_pass, NULL);
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
cmdBindGraphics(VkCommandBuffer cmd, const Graphics_Pipeline* prog,
                uint32_t descriptor_set_count, VkDescriptorSet* descriptor_sets)
{
  if (descriptor_set_count > 0) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->layout,
                            0, descriptor_set_count, descriptor_sets, 0, NULL);
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->pipeline);
}

INTERNAL void
cmdBindCompute(VkCommandBuffer cmd, const Compute_Pipeline* prog,
               uint32_t descriptor_set_count, VkDescriptorSet* descriptor_sets)
{
  if (descriptor_set_count > 0) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prog->layout,
                            0, descriptor_set_count, descriptor_sets, 0, NULL);
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prog->pipeline);
}

INTERNAL void
DepthReductionPass(Depth_Pyramid* pyramid, VkCommandBuffer cmd, Compute_Pipeline* pipeline,
                   uint32_t width, uint32_t height)
{
  if (g_window->frame_counter == 0) {
    // transition depth pyramid mips to correct layouts
    VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .image = pyramid->image,
      .subresourceRange = (VkImageSubresourceRange) {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = pyramid->num_mips,
        .baseArrayLayer = 0,
        .layerCount = 1,
      }
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);
  } else {
    // read from previous frame's depth
    cmdBindCompute(cmd, pipeline, 0, NULL);
    uint32_t level_width = NearestPow2(width/2);
    uint32_t level_height = NearestPow2(height/2);
    for (uint32_t i = 0; i < pyramid->num_mips; i++) {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout,
                              0, 1, &pyramid->reduce_sets[i], 0, NULL);
      vkCmdDispatch(cmd, level_width / 16, level_height / 16, 1);

      VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = pyramid->image,
        .subresourceRange = (VkImageSubresourceRange) {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = i,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
        }
      };
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           1, &barrier);
      level_width >>= 1;
      level_height >>= 1;
      if (level_width < 16) level_width = 16;
      if (level_height < 16) level_height = 16;
    }
  }
}

INTERNAL void
UpdateDeletionQueue(Deletion_Queue* dq)
{
  const size_t max = ARR_SIZE(dq->objs);
  while (dq->count > 0) {
    size_t id = dq->left % max;
    if (dq->objs[id].frame + 2 > g_window->frame_counter)
      break;
    // HACK: avoid warning "case value ‘50’ not in enumerated type ‘VkObjectType’"
    switch ((uint32_t)dq->objs[id].type)
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
      case VK_OBJECT_TYPE_MEMORY:
        vkFreeMemory(g_device->logical_device, (VkDeviceMemory)dq->objs[id].handle, NULL);
        break;

      default:
        LOG_WARN("deletion queue: undefined type object %d", dq->objs[id].type);
        break;

      }
    dq->left = (dq->left+1) % max;
    dq->count--;
  }
}

INTERNAL VkResult
RecreateShadowPass(Shadow_Pass* pass, Deletion_Queue* dq, uint32_t dim)
{
  AddForDeletion(dq, (uint64_t)pass->framebuffer, VK_OBJECT_TYPE_FRAMEBUFFER);
  AddForDeletion(dq, (uint64_t)pass->image_view, VK_OBJECT_TYPE_IMAGE_VIEW);
  AddForDeletion(dq, (uint64_t)pass->image, VK_OBJECT_TYPE_IMAGE);

  ResetVideoMemory(&pass->memory);
  pass->extent.width = dim;
  pass->extent.height = dim;
  VkResult err = SH_CreateAttachments(pass, g_forward_pass, dq);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to recreate attachments for shadow map with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = SH_AllocateDescriptorSets(pass, g_forward_pass);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to reallocate descriptor sets for rendering to shadow map with error %s",
              ToString_VkResult(err));
    return err;
  }
  return err;
}

INTERNAL void
cmdExecutionBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                       0, NULL,
                       0, NULL,
                       0, NULL);
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
DebugDrawOBB(Debug_Drawer* debug_drawer, const OBB* obb)
{
  const uint32_t indices[24] = {
    0, 1,
    1, 3,
    3, 2,
    2, 0,

    4, 5,
    5, 7,
    7, 6,
    6, 4,

    0, 4,
    1, 5,
    2, 6,
    3, 7
  };
  for (size_t i = 0; i < ARR_SIZE(indices); i += 2) {
    AddDebugLine(debug_drawer,
                 &obb->corners[indices[i]], &obb->corners[indices[i+1]],
                 PACK_COLOR(255, 0, 0, 255));
  }
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
