#include "render.h"
#include "base.h"
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
static VkResult FWD_CreateResources(uint32_t width, uint32_t height);



VkResult
lida_ForwardPassCreate(uint32_t width, uint32_t height)
{
  g_fwd_pass = lida_TempAllocate(sizeof(lida_ForwardPass));
  FWD_ChooseFromats();
  // just testing sampler cache
  VkSampler nearest_sampler = lida_GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  VkSampler linear_sampler = lida_GetSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
  return VK_SUCCESS;
}

void
lida_ForwardPassDestroy()
{
  VkSampler nearest_sampler = lida_GetSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);  
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
  LIDA_LOG_DEBUG("Renderer formats: color=%s, depth=%s",
                 lida_VkFormatToString(g_fwd_pass->color_format),
                 lida_VkFormatToString(g_fwd_pass->depth_format));
}

VkResult
FWD_CreateResources(uint32_t width, uint32_t height)
{

}
