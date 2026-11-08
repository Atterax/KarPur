// KarPur DLSS Frame Generation through NVIDIA Streamline (vk_slfg.cpp). TOTK_FRAMEGEN=1 (default 0) loads Streamline
// from the folder of karpur.exe (sl.interposer.dll, sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and
// nvngx_dlssg.dll, the player's own copies); without them everything runs as before.
//
// Stage 1 (2026-11-08): KarPur creates the Vulkan instance and device with the extensions and extra queues the loaded
// Streamline features ask for (slGetFeatureRequirements; every feature the GPU has is enabled anyway) and hands them over
// with slSetVulkanInfo; Streamline owns the swapchain, its images, acquire and present, the surface and vkDeviceWaitIdle,
// which KarPur calls through the wrappers below. (Streamline's own vkCreateInstance / vkCreateDevice proxies crashed in
// sl.common.dll on a null call while it initialised, with no feature loaded.) Frame generation is not switched on yet:
// the log says whether DLSS-G is supported on this GPU.
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

int slfg_active(void);   // Streamline was loaded and initialised (before the instance is made)
int slfg_manual(void);
int slfg_generating(void);   // ... and frame generation is to run (TOTK_FRAMEGEN 2..4): DLSS's output goes to the window's size   // ... in its manual hooking mode (TOTK_FRAMEGEN_MODE=manual)
// Before vkCreateInstance: loads and initialises Streamline when TOTK_FRAMEGEN asks for it (logged)
void slfg_start(void);
// What the loaded features need, added to KarPur's lists: instance extensions; device extensions (those the device has);
// the extra graphics and compute queues
void slfg_instance_exts(const char **ext, uint32_t *n, uint32_t max);
void slfg_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max);
void slfg_extra_queues(uint32_t *graphics, uint32_t *compute);
// After the device is made: slSetVulkanInfo (Streamline's queues start at these family / index pairs), the swapchain
// proxies, and whether DLSS-G can run here (logged)
void slfg_device_ready(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t gfx_family, uint32_t gfx_index,
                       uint32_t compute_family, uint32_t compute_index, uint32_t optical_flow_family);   // UINT32_MAX: none
void slfg_shutdown(void);   // before the device and instance go

// Stage 2: one frame's inputs for DLSS Frame Generation (vk_taa.c fills it at the resolve). Matrices as Violet keeps
// them (columns; clip = matrix x position), without jitter; depth and motion vectors at the render size w x h, the
// motion vectors in pixels from this frame to the last; the HUD-less picture at the output size ow x oh (the size of
// the picture the present shows), in the presented image's encoding. Images in VK_IMAGE_LAYOUT_GENERAL.
typedef struct {
    float cur[16], prev[16], proj[16];
    float jx, jy;                        // the jitter the frame was drawn with, in pixels
    int reset, depth_inv;
    VkImage depth, mv, hudless;
    VkImageView depth_view, mv_view, hudless_view;
    VkFormat depth_format;
    uint32_t depth_usage, w, h, ow, oh;
} SlfgFrame;
// The present thread, in the present's command buffer before it is submitted: a frame token, Reflex's markers up to the
// render submission, the tags (copied now) and the camera constants; DLSS-G switched on at the first frame
// (TOTK_FRAMEGEN=2..4: the presented frames per rendered one)
void slfg_frame(VkCommandBuffer cb, const SlfgFrame *f);
void slfg_present_mark(int end);
// The frames per second DLSS-G puts on screen (generated ones included), over the last second; 0 without frame generation
float slfg_shown_fps(void);   // around vkQueuePresentKHR: Reflex's present markers of that frame (0 start, 1 end)

// The functions Streamline intercepts: its proxies while active, Vulkan's own otherwise
VkResult slfg_vkCreateWin32SurfaceKHR(VkInstance inst, const void *ci, const VkAllocationCallbacks *a, VkSurfaceKHR *s);
void slfg_vkDestroySurfaceKHR(VkInstance inst, VkSurfaceKHR s, const VkAllocationCallbacks *a);
VkResult slfg_vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *a, VkSwapchainKHR *sc);
void slfg_vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks *a);
VkResult slfg_vkGetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR sc, uint32_t *n, VkImage *img);
VkResult slfg_vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore s, VkFence f, uint32_t *idx);
VkResult slfg_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi);
VkResult slfg_vkDeviceWaitIdle(VkDevice d);

#ifdef __cplusplus
}
#endif
