#include "render.h"
#include "base.h"
#include "device.h"
#include "memory.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct {

  lida_VideoMemory gpu_memory;
  lida_VideoMemory cpu_memory;
  VkImage color_image;
  VkImage depth_image;
  VkImage resolve_image;
  VkImageView color_image_view;
  VkImageView depth_image_view;
  VkImageView resolve_image_view;
  VkFramebuffer framebuffer;
  VkRenderPass render_pass;
  VkBuffer uniform_buffer;
  uint32_t uniform_buffer_size;
  void* uniform_buffer_mapped;
  VkDescriptorSet scene_data_set;
  VkDescriptorSet resulting_image_set;
  VkFormat color_format;
  VkFormat depth_format;
  VkSampleCountFlagBits msaa_samples;
  VkExtent2D render_extent;
  VkMappedMemoryRange uniform_buffer_range;

} lida_ForwardPass;

lida_ForwardPass* g_fwd_pass;

static void FWD_ChooseFromats(VkSampleCountFlagBits samples);
static VkResult FWD_CreateRenderPass();
static VkResult FWD_CreateAttachments(uint32_t width, uint32_t height);
static VkResult FWD_CreateBuffers();
static VkResult FWD_AllocateDescriptorSets();



VkResult
lida_ForwardPassCreate(uint32_t width, uint32_t height, VkSampleCountFlagBits samples)
{
  g_fwd_pass = lida_TempAllocate(sizeof(lida_ForwardPass));
  g_fwd_pass->render_extent = (VkExtent2D) {width, height};
  FWD_ChooseFromats(samples);
  VkResult err = FWD_CreateRenderPass();
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create render pass with error %s", lida_VkResultToString(err));
    goto err;
  }
  err = FWD_CreateAttachments(width, height);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create attachments");
    goto err;
  }
  g_fwd_pass->uniform_buffer_size = 2048;
  err = FWD_CreateBuffers();
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create buffers");
    goto err;
  }
  err = FWD_AllocateDescriptorSets();
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to allocate descriptor sets");
    goto err;
  }
  return VK_SUCCESS;
 err:
  lida_TempFree(g_fwd_pass);
  return err;
}

void
lida_ForwardPassDestroy()
{
  VkDevice dev = lida_GetLogicalDevice();

  vkDestroyBuffer(dev, g_fwd_pass->uniform_buffer, NULL);

  vkDestroyFramebuffer(dev, g_fwd_pass->framebuffer, NULL);
  vkDestroyImageView(dev, g_fwd_pass->depth_image_view, NULL);
  vkDestroyImageView(dev, g_fwd_pass->color_image_view, NULL);
  if (g_fwd_pass->resolve_image_view)
    vkDestroyImageView(dev, g_fwd_pass->resolve_image_view, NULL);
  vkDestroyImage(dev, g_fwd_pass->depth_image, NULL);
  vkDestroyImage(dev, g_fwd_pass->color_image, NULL);
  if (g_fwd_pass->resolve_image)
    vkDestroyImage(dev, g_fwd_pass->resolve_image, NULL);
  vkDestroyRenderPass(dev, g_fwd_pass->render_pass, NULL);

  lida_VideoMemoryFree(&g_fwd_pass->cpu_memory);
  lida_VideoMemoryFree(&g_fwd_pass->gpu_memory);

  lida_TempFree(g_fwd_pass);
}

lida_SceneDataStruct*
lida_ForwardPassGetSceneData()
{
  return g_fwd_pass->uniform_buffer_mapped;
}

VkDescriptorSet
lida_ForwardPassGetDS0()
{
  return g_fwd_pass->scene_data_set;
}

VkDescriptorSet
lida_ForwardPassGetDS1()
{
  return g_fwd_pass->resulting_image_set;
}

VkRenderPass
lida_ForwardPassGetRenderPass()
{
  return g_fwd_pass->render_pass;
}

VkSampleCountFlagBits
lida_ForwardPassGet_MSAA_Samples()
{
  return g_fwd_pass->msaa_samples;
}

void
lida_ForwardPassSendData()
{
  VkResult err = vkFlushMappedMemoryRanges(lida_GetLogicalDevice(),
                                           1, &g_fwd_pass->uniform_buffer_range);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to flush memory with error %s", lida_VkResultToString(err));
  }
}

void
lida_ForwardPassBegin(VkCommandBuffer cmd, float clear_color[4])
{
  VkClearValue clearValues[2];
  // color attachment
  memcpy(clearValues[0].color.float32, clear_color, sizeof(float) * 4);
  // depth attachment
  clearValues[1].depthStencil.depth = 0.0f;
  clearValues[1].depthStencil.stencil = 0;
  VkRect2D render_area = { .offset = {0, 0},
                           .extent = g_fwd_pass->render_extent };
  VkRenderPassBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = g_fwd_pass->render_pass,
    .framebuffer = g_fwd_pass->framebuffer,
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



void FWD_ChooseFromats(VkSampleCountFlagBits samples)
{
  VkFormat hdr_formats[] = {
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,
  };
  g_fwd_pass->color_format = LIDA_FIND_SUPPORTED_FORMAT(hdr_formats, VK_IMAGE_TILING_OPTIMAL,
                                                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
                                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
                                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  VkFormat depth_formats[] = {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
  };
  g_fwd_pass->depth_format = LIDA_FIND_SUPPORTED_FORMAT(depth_formats, VK_IMAGE_TILING_OPTIMAL,
                                                        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|
                                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  LIDA_LOG_TRACE("Renderer formats: color=%s, depth=%s",
                 lida_VkFormatToString(g_fwd_pass->color_format),
                lida_VkFormatToString(g_fwd_pass->depth_format));
  g_fwd_pass->msaa_samples = lida_MaxSampleCount(samples);
}

VkResult
FWD_CreateRenderPass()
{
  VkAttachmentDescription attachments[3];
  attachments[0] = (VkAttachmentDescription) {
    .format = g_fwd_pass->color_format,
    .samples = g_fwd_pass->msaa_samples,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  };
  attachments[1] = (VkAttachmentDescription) {
    .format = g_fwd_pass->depth_format,
    .samples = g_fwd_pass->msaa_samples,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };
  if (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[2] = (VkAttachmentDescription) {
      .format = g_fwd_pass->color_format,
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
    .colorAttachmentCount = LIDA_ARR_SIZE(color_references),
    .pColorAttachments = color_references,
    .pDepthStencilAttachment = &depth_reference,
    .pResolveAttachments = (g_fwd_pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? NULL : resolve_references,
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
    .attachmentCount = 2 + (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT),
    .pAttachments = attachments,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = LIDA_ARR_SIZE(dependencies),
    .pDependencies = dependencies,
  };
  return vkCreateRenderPass(lida_GetLogicalDevice(), &render_pass_info, NULL, &g_fwd_pass->render_pass);
}

VkResult
FWD_CreateAttachments(uint32_t width, uint32_t height)
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
  image_info.format = g_fwd_pass->color_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (g_fwd_pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) {
    image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  } else {
    // TODO: try to use memory with LAZILY_ALLOCATED property
    image_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  }
  image_info.samples = g_fwd_pass->msaa_samples;
  err = vkCreateImage(lida_GetLogicalDevice(), &image_info, NULL, &g_fwd_pass->color_image);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create color image with error %s", lida_VkResultToString(err));
    return err;
  }
  // create depth image
  image_info.format = g_fwd_pass->depth_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.samples = g_fwd_pass->msaa_samples;
  err = vkCreateImage(lida_GetLogicalDevice(), &image_info, NULL, &g_fwd_pass->depth_image);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create depth image with error %s", lida_VkResultToString(err));
    return err;
  }
  // create resolve image if msaa_samples > 1
  if (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    // FIXME: should we use another format for resolve image?
    image_info.format = g_fwd_pass->color_format;
    image_info.extent = (VkExtent3D) { width, height, 1 };
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    err = vkCreateImage(lida_GetLogicalDevice(), &image_info, NULL, &g_fwd_pass->resolve_image);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create resolve image with error %s", lida_VkResultToString(err));
      return err;
    }
  } else {
    g_fwd_pass->resolve_image = VK_NULL_HANDLE;
  }
  // allocate memory
  VkMemoryRequirements image_requirements[3];
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->color_image, &image_requirements[0]);
  vkGetImageMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->color_image, &image_requirements[1]);
  if (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    vkGetImageMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->resolve_image, &image_requirements[2]);
  }
  lida_MergeMemoryRequirements(image_requirements, 2 + (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT), &requirements);
  if (requirements.size > g_fwd_pass->gpu_memory.size) {
    err = lida_VideoMemoryAllocate(&g_fwd_pass->gpu_memory, requirements.size,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.memoryTypeBits);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate GPU memory for attachments with error %s",
                     lida_VkResultToString(err));
      return err;
    }
  } else {
    lida_VideoMemoryReset(&g_fwd_pass->gpu_memory);
  }
  // bind images to memory
  lida_ImageBindToMemory(&g_fwd_pass->gpu_memory, g_fwd_pass->color_image, &image_requirements[0]);
  lida_ImageBindToMemory(&g_fwd_pass->gpu_memory, g_fwd_pass->depth_image, &image_requirements[1]);
  if (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    lida_ImageBindToMemory(&g_fwd_pass->gpu_memory, g_fwd_pass->resolve_image, &image_requirements[2]);
  }
  // create image views
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
  };
  image_view_info.image = g_fwd_pass->color_image;
  image_view_info.format = g_fwd_pass->color_format;
  image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  err = vkCreateImageView(lida_GetLogicalDevice(), &image_view_info, NULL, &g_fwd_pass->color_image_view);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create color image view with error %s", lida_VkResultToString(err));
    return err;
  }
  image_view_info.image = g_fwd_pass->depth_image;
  image_view_info.format = g_fwd_pass->depth_format;
  image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
  err = vkCreateImageView(lida_GetLogicalDevice(), &image_view_info, NULL, &g_fwd_pass->depth_image_view);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create depth image view with error %s", lida_VkResultToString(err));
    return err;
  }
  if (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    image_view_info.image = g_fwd_pass->resolve_image;
    image_view_info.format = g_fwd_pass->color_format;
    image_view_info.subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    err = vkCreateImageView(lida_GetLogicalDevice(), &image_view_info, NULL, &g_fwd_pass->resolve_image_view);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to create resolve image with error %s", lida_VkResultToString(err));
      return err;
    }
  } else {
    g_fwd_pass->resolve_image_view = VK_NULL_HANDLE;
  }
  // create framebuffer
  VkImageView attachments[3] = { g_fwd_pass->color_image_view, g_fwd_pass->depth_image_view, g_fwd_pass->resolve_image_view };
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = g_fwd_pass->render_pass,
    .attachmentCount = 2 + (g_fwd_pass->msaa_samples != VK_SAMPLE_COUNT_1_BIT),
    .pAttachments = attachments,
    .width = width,
    .height = height,
    .layers = 1,
  };
  err = vkCreateFramebuffer(lida_GetLogicalDevice(), &framebuffer_info, NULL, &g_fwd_pass->framebuffer);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create framebuffer with error %s", lida_VkResultToString(err));
    return err;
  }
  LIDA_LOG_TRACE("allocated %u bytes for attachments", (uint32_t)requirements.size);
  return err;
}

VkResult FWD_CreateBuffers()
{
  VkResult err = lida_BufferCreate(&g_fwd_pass->uniform_buffer, g_fwd_pass->uniform_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create uniform buffer with error %s", lida_VkResultToString(err));
    return err;
  }
  VkMemoryRequirements buffer_requirements[1];
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->uniform_buffer, &buffer_requirements[0]);
  VkMemoryRequirements requirements;
  lida_MergeMemoryRequirements(buffer_requirements, 1, &requirements);
  err = lida_VideoMemoryAllocate(&g_fwd_pass->cpu_memory, requirements.size,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT, requirements.memoryTypeBits);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to allocate memory for buffers with error %s", lida_VkResultToString(err));
    return err;
  }
  err = lida_BufferBindToMemory(&g_fwd_pass->cpu_memory, g_fwd_pass->uniform_buffer, &buffer_requirements[0],
                                &g_fwd_pass->uniform_buffer_mapped, &g_fwd_pass->uniform_buffer_range);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to bind uniform buffer to memory with error %s", lida_VkResultToString(err));
    return err;
  }
  LIDA_LOG_TRACE("allocated %u bytes for attachments", (uint32_t)requirements.size);
  return err;
}

VkResult FWD_AllocateDescriptorSets()
{
  // allocate descriptor sets
  VkDescriptorSetLayoutBinding bindings[4];
  bindings[0] = (VkDescriptorSetLayoutBinding) {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };
  VkResult err = lida_AllocateDescriptorSets(bindings, 1, &g_fwd_pass->scene_data_set, 1, 0);
  if (err == VK_SUCCESS) {
    bindings[0] = (VkDescriptorSetLayoutBinding) {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    err = lida_AllocateDescriptorSets(bindings, 1, &g_fwd_pass->resulting_image_set, 1, 1);
  }
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to allocate descriptor sets with error %s", lida_VkResultToString(err));
    return err;
  }
  // update descriptor sets
  VkWriteDescriptorSet write_sets[2];
  VkDescriptorBufferInfo buffer_info = {
    .buffer = g_fwd_pass->uniform_buffer,
    .offset = 0,
    .range = sizeof(lida_SceneDataStruct)
  };
  write_sets[0] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = g_fwd_pass->scene_data_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .pBufferInfo = &buffer_info,
  };
  VkDescriptorImageInfo image_info = {
    .imageView = (g_fwd_pass->msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? g_fwd_pass->color_image_view : g_fwd_pass->resolve_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .sampler = lida_GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
  };
  write_sets[1] = (VkWriteDescriptorSet) {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = g_fwd_pass->resulting_image_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_info
  };
  lida_UpdateDescriptorSets(write_sets, 2);
  return err;
}
