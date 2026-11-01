// Transform feedback layout of an NVN vertex shader, from its 0x880-byte control section. Written for KarPur (MIT).
//
// Found by comparing the control sections of Violet's transform feedback vertex shaders with all others (vk_xfb.c
// history, docs/progress.md 2026-11-01). Per buffer b (0..3) and output j of the buffer:
//   +0x0dc + 0x80 b + j   attribute of the output as 0x20 + generic attribute index (0x20 = generic 0)
//   +0x2dc + 0x80 b + j   first component (assumed; zero in every shader seen)
//   +0x4dc + 0x80 b + j   number of components (0 ends the list)
//   +0x6dc + 2 b          stride of the buffer in bytes (u16; 0: the buffer is not written)
// Observed: one output per buffer, 0x20/0x21/0x22 with 3/3/3 components and strides 12/12/12, or 3/1/1 and 12/4/4,
// matching the buffer sizes the game binds exactly. Only generic attributes were seen; others are left out.
//
// nvn_xfb_layout turns it into what the Ryujinx translator asks for (IGpuAccessor.QueryTransformFeedbackVaryingLocations):
// per buffer, one byte per 32-bit component written, in buffer order, as a Maxwell attribute word offset
// (generic attribute k, component c = 0x20 + 4 k + c).
#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t stride[4];
    uint8_t n[4];          // components written per vertex
    uint8_t loc[4][32];    // Maxwell attribute word offset of each component
} NvnXfb;

// 1 when the shader writes transform feedback (some buffer has a stride), 0 otherwise.
static inline int nvn_xfb_layout(const uint8_t *ctl, NvnXfb *x) {
    int any = 0;
    memset(x, 0, sizeof *x);
    for (int b = 0; b < 4; b++) {
        memcpy(&x->stride[b], ctl + 0x6dc + 2 * b, 2);
        if (!x->stride[b]) continue;
        any = 1;
        for (int j = 0; j < 0x80; j++) {
            unsigned attr = ctl[0x0dc + 0x80 * b + j], first = ctl[0x2dc + 0x80 * b + j],
                     count = ctl[0x4dc + 0x80 * b + j];
            if (!count) break;
            for (unsigned c = 0; c < count && x->n[b] < 32; c++)
                x->loc[b][x->n[b]++] =
                    attr >= 0x20 && attr < 0x40 && first + c < 4 ? (uint8_t)(0x20 + 4 * (attr - 0x20) + first + c) : 0xff;
        }
    }
    return any;
}
