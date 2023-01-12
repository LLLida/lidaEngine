#include "SDL_video.h"
#include "stdint.h"
#include "volk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* name;
  uint32_t x, y;
  uint32_t w, h;
  VkPresentModeKHR preferred_present_mode;
} lida_WindowDesc;

typedef struct {
  VkImage image;
  VkImageView image_view;
  VkFramebuffer framebuffer;
} lida_WindowImage;

int lida_WindowCreate(const lida_WindowDesc* description);
// usage:
/*
LIDA_WINDOW_CREATE( .name = "Hello world",
                    .x = 0,
                    .y = 0,
                    .w = 640,
                    .h = 480 );
 */
#define LIDA_WINDOW_CREATE(...) lida_WindowCreate(&(lida_WindowDesc) { __VA_ARGS__ })

void lida_WindowDestroy();

VkSurfaceKHR lida_WindowGetSurface();
VkSwapchainKHR lida_WindowGetSwapchain();
uint32_t lida_WindowGetNumImages();
const lida_WindowImage* lida_WindowGetImages();
VkExtent2D lida_WindowGetExtent();
VkRenderPass lida_WindowGetRenderPass();
VkSurfaceFormatKHR lida_WindowGetFormat();
VkPresentModeKHR lida_WindowGetPresentMode();
float lida_WindowGetFPS();

// wait for an available command buffer and begin it
// this function will take the most of the time if not used properly as it waits
// till previous commands end on GPU
VkCommandBuffer lida_WindowBeginCommands();
// Wait for an available swapchain image and start rendering to it(start main render pass)
VkResult lida_WindowBeginRendering();
// submit render commands and present image to screen
// this function doesn't wait for commands to end, it just *sends* them to GPU
VkResult lida_WindowPresent();

#ifdef __cplusplus
}
#endif
