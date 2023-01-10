#include "render.h"
#include "base.h"
#include "device.h"
#include "memory.h"

typedef struct {
  lida_Mat4 camera_projview;
  lida_Mat4 camera_projection;
  lida_Mat4 camera_view;
  lida_Mat4 camera_invproj;
} SceneInfo;

typedef struct {

  lida_VideoMemory gpu_memory;
  lida_VideoMemory cpu_memory;
  VkImage color_image;
  VkImage depth_image;
  VkImageView color_image_view;
  VkImageView depth_image_view;
  VkFramebuffer framebuffer;
  VkRenderPass render_pass;
  VkDescriptorSet scene_data_set;
  VkDescriptorSet resulting_image_set;
  VkFormat color_format;
  VkFormat depth_format;
  VkExtent2D render_extent;

} lida_ForwardPass;

lida_ForwardPass* g_fwd_pass;

static void FWD_ChooseFromats();
static VkResult FWD_CreateRenderPass();
static VkResult FWD_CreateResources(uint32_t width, uint32_t height);

#define ATTACHMENT_DESCRIPTION(format_, load_op, store_op, initial_layout, final_layout) (VkAttachmentDescription) { \
    .format = format_,                                                  \
      .samples = VK_SAMPLE_COUNT_1_BIT,                                 \
      .loadOp = load_op,                                                \
      .storeOp = store_op,                                              \
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,                 \
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,               \
      .initialLayout = initial_layout,                                  \
      .finalLayout = final_layout                                       \
      }



VkResult
lida_ForwardPassCreate(uint32_t width, uint32_t height)
{
  g_fwd_pass = lida_TempAllocate(sizeof(lida_ForwardPass));
  FWD_ChooseFromats();
  VkResult err = FWD_CreateRenderPass();
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create render pass with error %s", lida_VkResultToString(err));
    goto err;
  }
  err = FWD_CreateResources(width, height);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create attachments");
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
  vkDestroyFramebuffer(dev, g_fwd_pass->framebuffer, NULL);
  vkDestroyImageView(dev, g_fwd_pass->depth_image_view, NULL);
  vkDestroyImageView(dev, g_fwd_pass->color_image_view, NULL);
  vkDestroyImage(dev, g_fwd_pass->depth_image, NULL);
  vkDestroyImage(dev, g_fwd_pass->color_image, NULL);
  lida_VideoMemoryFree(&g_fwd_pass->gpu_memory);
  vkDestroyRenderPass(dev, g_fwd_pass->render_pass, NULL);
  
  lida_TempFree(g_fwd_pass);
}



void FWD_ChooseFromats()
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
}

VkResult
FWD_CreateRenderPass()
{
  VkAttachmentDescription attachments[2];
  attachments[0] = ATTACHMENT_DESCRIPTION(g_fwd_pass->color_format,
                                          VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          VK_ATTACHMENT_STORE_OP_STORE,
                                          VK_IMAGE_LAYOUT_UNDEFINED,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  attachments[1] = ATTACHMENT_DESCRIPTION(g_fwd_pass->depth_format,
                                          VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          VK_IMAGE_LAYOUT_UNDEFINED,
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  VkAttachmentReference references[2];
  references[0] = (VkAttachmentReference) { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  references[1] = (VkAttachmentReference) { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = references,
    .pDepthStencilAttachment = references + 1,
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
    .attachmentCount = 2,
    .pAttachments = attachments,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 2,
    .pDependencies = dependencies,
  };
  return vkCreateRenderPass(lida_GetLogicalDevice(), &render_pass_info, NULL, &g_fwd_pass->render_pass);
}

VkResult
FWD_CreateResources(uint32_t width, uint32_t height)
{
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkResult err;
  // create color image
  image_info.format = g_fwd_pass->color_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  err = vkCreateImage(lida_GetLogicalDevice(), &image_info, NULL, &g_fwd_pass->color_image);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create color image with error %s", lida_VkResultToString(err));
    return err;
  }
  // create depth image
  image_info.format = g_fwd_pass->depth_format;
  image_info.extent = (VkExtent3D) { width, height, 1 };
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  err = vkCreateImage(lida_GetLogicalDevice(), &image_info, NULL, &g_fwd_pass->depth_image);
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create depth image with error %s", lida_VkResultToString(err));
    return err;
  }
  // allocate memory
  VkMemoryRequirements requirements[3];
  vkGetImageMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->color_image, &requirements[0]);
  vkGetImageMemoryRequirements(lida_GetLogicalDevice(), g_fwd_pass->color_image, &requirements[1]);
  lida_MergeMemoryRequirements(requirements, 2, &requirements[2]);
  if (requirements[2].size > g_fwd_pass->gpu_memory.size) {
    err = lida_VideoMemoryAllocate(&g_fwd_pass->gpu_memory, requirements[2].size,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements[2].memoryTypeBits);
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate GPU memory for attachments with error %s",
                     lida_VkResultToString(err));
      return err;
    }
  } else {
    lida_VideoMemoryReset(&g_fwd_pass->gpu_memory);
  }
  // bind images to memory
  lida_ImageBindToMemory(&g_fwd_pass->gpu_memory, g_fwd_pass->color_image, &requirements[0]);
  lida_ImageBindToMemory(&g_fwd_pass->gpu_memory, g_fwd_pass->depth_image, &requirements[1]);
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
  // create framebuffer
  VkImageView attachments[2] = { g_fwd_pass->color_image_view, g_fwd_pass->depth_image_view };
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = g_fwd_pass->render_pass,
    .attachmentCount = 2,
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
  LIDA_LOG_TRACE("allocated %u bytes for attachments", (uint32_t)requirements[2].size);
  return err;
}
