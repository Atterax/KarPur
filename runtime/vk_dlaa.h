// NVIDIA DLAA (DLSS at the scene's own size) for the Vulkan back end (vk_dlaa.c). Windows with an NVIDIA RTX GPU and
// nvngx_dlss.dll next to karpur.exe; anywhere else it is simply unavailable. TOTK_DLAA=1 turns it on (default off).
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

// One frame of a temporal anti-aliasing: the jittered picture, its depth, the motion vectors (RG16F, pixels, current to
// previous) at w x h, and the output at ow x oh (DLAA: the same size). Images in VK_IMAGE_LAYOUT_GENERAL. jx, jy: the
// jitter the frame was drawn with, in pixels.
typedef struct {
    VkImage color, depth, mv, out;
    VkImageView color_view, depth_view, mv_view, out_view;
    VkFormat color_format, depth_format, out_format;
    uint32_t w, h, ow, oh;
    float jx, jy, dt_ms;
    int reset;                           // the history does not apply (a cut, a new size)
} VkTaaFrame;

int vk_dlaa_wanted(void);                // TOTK_DLAA, read once (NGX needs its extensions at instance and device creation)
// The instance / device extensions NGX asks for, appended to ext (n of them so far, room for max)
void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max);
void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max);
// After the device is made: NGX brought up on it (logged)
void vk_dlaa_init(VkInstance inst, VkPhysicalDevice phys, VkDevice dev);
int vk_dlaa_available(void);             // NGX is up and DLSS can run on this GPU
// Records DLAA into cmd: f->color resolved with its history into f->out. 0: not done (logged)
int vk_dlaa_record(VkDevice dev, VkCommandBuffer cmd, const VkTaaFrame *f);
// Releases the feature and shuts NGX down on dev (before vkDestroyDevice; the driver faulted at exit without it)
void vk_dlaa_shutdown(VkDevice dev);
