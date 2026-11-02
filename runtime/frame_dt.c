// KarPur: Violet's frame time for the patch set fps_dt (tools/fps_patches.py).
//
// The game passes its frame time in nanoseconds (33333334 at 30 fps) as x2 to the thunk main+0x92dd20, which tail
// calls a virtual function with it. With fps_dt on, the patched thunk loads x2 from the patch page +64 instead, and
// this file keeps that word at the measured time between presents: the game then steps by the time that really
// passed, whatever the frame rate (the approach of a third-party frame-rate mod for Violet, read for behaviour only;
// docs/progress.md 2026-11-02).
//
// TOTK_FPS_DT_NS=n fixes the word at n nanoseconds instead (0, the default: measured). The measured time is the mean
// of the last 8 present intervals, each clamped to 4..100 ms so a load stall does not throw the game forward.
//
// TOTK_FPS_DT_SNAP=1 (test, default off): the word never drops below one frame at the frame limit
// (frame_step_cap_ns). At 60 fps the measured mean jitters around 16.67 ms and is often a little shorter; Violet's
// particle effects then stepped irregularly (choppy at 60 fps, smooth at 45 fps and with a fixed 16666667 ns,
// docs/progress.md 2026-11-02), as if they counted whole 60 Hz frames in the frame time. The pacer holds frames to the
// limit, so a mean below it is jitter only.
#include <stdint.h>
#include <stdlib.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

#define PAGE_FRAME_NS 64      // byte offset on the patch page (recomp/patches.py layout)
#define START_NS 33333334ull  // the game's own value at 30 fps, until presents give a measurement

static int set = -2;
static uint64_t *word;   // written with __atomic_store_n: the game reads it on other threads

static int ready(void) {
    if (set == -2) {
        set = rt_patch_set("fps_dt");
        if (set >= 0 && rt_patch_page()) {
            word = (uint64_t *)((uint8_t *)rt_patch_page() + PAGE_FRAME_NS);
            const char *e = cfg_get("TOTK_FPS_DT_NS");
            uint64_t fixed = e && *e ? strtoull(e, NULL, 10) : 0;
            __atomic_store_n(word, fixed ? fixed : START_NS, __ATOMIC_RELAXED);
            rt_log("frame dt: patch set fps_dt %s; frame time for the game %s", rt_patch_enabled(set) ? "on" : "off",
                   fixed ? "fixed (TOTK_FPS_DT_NS)" : "measured between presents");
            if (fixed) set = -1;   // nothing to update
        }
    }
    return set >= 0 && word;
}

void frame_dt_init(void) { ready(); }

void frame_dt_present(void) {
    static uint64_t last, hist[8];
    static unsigned n;
    if (!ready()) return;
    uint64_t now = plat_now_ns();
    if (last) {
        uint64_t d = now - last;
        d = d < 4000000u ? 4000000u : d > 100000000u ? 100000000u : d;
        hist[n++ % 8] = d;
        unsigned k = n < 8 ? n : 8;
        uint64_t sum = 0;
        for (unsigned i = 0; i < k; i++) sum += hist[i];
        uint64_t dt = sum / k;
        static int snap = -1;
        if (snap < 0) {
            snap = rt_knob("FPS_DT_SNAP", "0");
            if (snap) rt_log("frame dt: never shorter than one frame at the frame limit (TOTK_FPS_DT_SNAP)");
        }
        if (snap) {
            uint64_t cap = frame_step_cap_ns();
            if (dt < cap) dt = cap;
        }
        __atomic_store_n(word, dt, __ATOMIC_RELAXED);
    }
    last = now;
}
