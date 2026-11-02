// KarPur: the LOD bias of Violet's patch set lod_bias (tools/fps_patches.py).
//
// Two sites in main load a float from an object right before a virtual call that takes it as its argument; a
// third-party frame-rate and camera mod for Violet replaces both with -15.0 and calls it the LOD bias (read for
// behaviour only; docs/progress.md 2026-11-02). With the set on (TOTK_LOD=1) both load the float at patch page +72,
// which this file sets from TOTK_LOD_BIAS (default -15.0; a lower value is meant to keep detailed models and far
// objects longer).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"

#define PAGE_LOD_BIAS 72   // byte offset on the patch page (recomp/patches.py layout)

void lod_init(void) {
    int set = rt_patch_set("lod_bias");
    if (set < 0 || !rt_patch_page()) return;
    const char *e = cfg_get("TOTK_LOD_BIAS");
    float v = e && *e ? strtof(e, NULL) : -15.0f;
    memcpy((uint8_t *)rt_patch_page() + PAGE_LOD_BIAS, &v, sizeof v);
    rt_log("lod: patch set lod_bias %s; bias %g (TOTK_LOD=1 turns it on, TOTK_LOD_BIAS sets the value)",
           rt_patch_enabled(set) ? "on" : "off", (double)v);
}
