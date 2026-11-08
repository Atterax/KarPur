// KarPur temporal anti-aliasing with NVIDIA DLAA (vk_taa.c). TOTK_DLAA=1 turns it on where DLSS is available.
#pragma once
#include <stdint.h>

#include "vk_exec.h"
#include "vk.h"

int vk_taa_on(void);                    // DLAA wanted and available
// A G-buffer draw (4+ colour targets, depth): the scene camera this frame (c5 +0 and the projection at c5 +0x80, both
// as columns; vk_exec_draw.c reads them) and the scene depth target. The first one of a frame counts.
int vk_taa_need_scene(void);            // 1 while this frame has no camera yet
void vk_taa_note_scene(const VkExec *ex, const float vp[16], const float proj[16]);
// record_fixed: the jitter for a viewport of a scene pass (the scene depth bound), in pixels
void vk_taa_viewport(const VkExec *ex, float *x, float *y);
// After a draw was recorded with fragment shader fs (its first 4 input id bytes): the tone mapping draw resolves
void vk_taa_after_draw(VkExec *ex, uint32_t fs);
int vk_taa_frame(void);                 // at each present (replay order): 1 when this frame's resolve was upscaled
// The present thread: arm the next present's composite (vk_taa_frame's answer for its frame), then, in the present's
// command buffer, the composite of the presented image (w x h) with DLSS's picture: 1 and *out (ow x oh, RGBA8 UNORM,
// GENERAL) to show in its place
void vk_taa_present_arm(int up);
// The overlay's words for the last frame: 0 nothing; 1 DLAA ("NVIDIA DLAA"); 2 DLSS to a larger picture (its sizes)
int vk_taa_describe(char *buf, size_t cap);
int vk_taa_compose(VkCommandBuffer cb, unsigned slot, VkImage shown, VkFormat fmt, int w, int h, VkImage *out, int *ow, int *oh);
