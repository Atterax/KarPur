// KarPur: looks at the data files Violet unpacks, for the patch set data_watch (tools/fps_patches.py).
//
// Violet's data (arc/data.trpfs) is mostly Oodle Kraken data that the game unpacks with OodleLZ_Decompress, linked
// into main (main+0xcc8750 in 4.0.0). With the set on (TOTK_DATA_WATCH=1) every direct call of it comes here (patch
// page +0xfd0): the guest decoder runs as before, then the unpacked bytes are compared with a list of wanted files that
// tools/trpfs.py --watch writes (TOTK_DATA_WATCH_LIST, default game/data_watch.txt; one line per file: packed size,
// unpacked size, FNV-1a 64 of the first 4 KiB of the packed bytes in hex, a name). A file on the list is written once
// to game/dump/<name>, which stays on the player's PC (game/ is ignored by git); tools/trpfs.py --report reads the
// dumps and prints field names and numbers. Purpose: find the data that sets LOD and view distances
// (docs/progress.md 2026-11-02), which the runtime will later change here, after the decode.
//
// Arguments of OodleLZ_Decompress: x0 packed bytes, x1 packed size, x2 output, x3 output size, x4..x7 and six more on
// the guest stack (rt_call leaves sp alone, so they stay where the decoder reads them). Returns the unpacked size, or 0.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "dispatch.h"
#include "host.h"
#include "platform/platform.h"

#define PAGE_HOST 0xfd0            // host function entry on the patch page (recomp/patches.py layout)
#define OODLE_DECOMPRESS 0xcc8750  // main offset of OodleLZ_Decompress (4.0.0)
#define HEAD 4096                  // bytes of the packed data that the fingerprint covers
#define MAX_WANT 4096

typedef struct {
    uint64_t packed, unpacked, print;
    char name[240];
    int done;
} Want;

static Want want[MAX_WANT];
static int nwant;
static uint64_t guest_decoder;
static int watch_set = -1, dumping;   // the patch set; DATA_WATCH asked for dumps (keeps the redirect on)
static struct { uint64_t calls, out, matched, failed; } S;

static uint64_t fnv1a64(const uint8_t *p, uint64_t n) {
    uint64_t h = 0xCBF29CE484222645ull;
    for (uint64_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

static void load_list(void) {
    const char *path = cfg_get("TOTK_DATA_WATCH_LIST");
    if (!path || !*path) path = "game/data_watch.txt";
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    if (!text) {
        rt_log("data watch: no list %s (tools/trpfs.py --watch writes it): only counting decodes", path);
        return;
    }
    char *end = text + size, *line = text;
    while (line < end && nwant < MAX_WANT) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        if (!nl) nl = end;
        *nl = 0;
        Want w = {0};
        if (line[0] != '#' &&
            sscanf(line, "%llu %llu %llx %239s", (unsigned long long *)&w.packed, (unsigned long long *)&w.unpacked,
                   (unsigned long long *)&w.print, w.name) == 4)
            want[nwant++] = w;
        line = nl + 1;
    }
    free(text);
    rt_log("data watch: %d wanted files from %s; dumps go to game/dump", nwant, path);
}

static void dump(Want *w, const uint8_t *data, uint64_t n) {
    char path[300];
    plat_file_mkdir_p("game/dump");
    snprintf(path, sizeof path, "game/dump/%s", w->name);
    int ok = plat_file_write(path, data, (size_t)n);
    rt_log("data watch: %s %s (%llu bytes)", ok ? "dumped" : "could not write", path, (unsigned long long)n);
}

static void host_oodle(Cpu *c) {
    uint64_t a[8];
    memcpy(a, c->x, sizeof a);
    uint64_t r = 0;
    if (a[0] + a[1] <= RT_ADDRESS_LIMIT && a[2] + a[3] <= RT_ADDRESS_LIMIT)
        r = data_patch_replace((const uint8_t *)(uintptr_t)a[0], a[1], (uint8_t *)(uintptr_t)a[2], a[3]);
    if (r) c->x[0] = r;   // what the decoder would return; rt_call leaves its result there
    else r = rt_call(c, guest_decoder, 8, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    uint64_t n = __atomic_add_fetch(&S.calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&S.out, r, __ATOMIC_RELAXED);
    if (!r) __atomic_add_fetch(&S.failed, 1, __ATOMIC_RELAXED);
    if (n <= 3 || !(n & 0xfff))
        rt_log("data watch: %llu decodes, %.1f MiB unpacked, %llu wanted files found, %llu failed (this one: %llu -> "
               "%llu bytes)", (unsigned long long)n, S.out / 1048576.0, (unsigned long long)S.matched,
               (unsigned long long)S.failed, (unsigned long long)a[1], (unsigned long long)r);
    if (!r || r > RT_ADDRESS_LIMIT || a[0] + a[1] > RT_ADDRESS_LIMIT || a[2] + r > RT_ADDRESS_LIMIT) return;
    const uint8_t *src = (const uint8_t *)(uintptr_t)a[0];
    uint64_t print;
    for (int i = 0; i < nwant; i++) {
        Want *w = &want[i];
        // the unpacked size and the start of the packed bytes (the packed size the game passes may include padding)
        uint64_t head = w->packed < HEAD ? w->packed : HEAD;
        if (w->unpacked != r || a[1] < head || __atomic_load_n(&w->done, __ATOMIC_RELAXED)) continue;
        print = fnv1a64(src, head);
        if (print != w->print || __atomic_exchange_n(&w->done, 1, __ATOMIC_RELAXED)) continue;
        __atomic_add_fetch(&S.matched, 1, __ATOMIC_RELAXED);
        dump(w, (const uint8_t *)(uintptr_t)a[2], r);
    }
    // After the dumps, so a dump is the game's own file: tools/pc_menu.py and mod_report.py compare with it.
    data_patch_decoded((const uint8_t *)(uintptr_t)a[0], a[1], (uint8_t *)(uintptr_t)a[2], r);
    // With the redirect on, trees flicker after the world loads until the next loading screen (docs/progress.md
    // 2026-11-02; cause not found). The fields are set once, when their files load at the start, so once all are set
    // the decoder calls go direct again: the generated code reads rt_patch_on at every call.
    if (!dumping && !data_patch_open() && rt_patch_enabled(watch_set)) {
        rt_patch_disable(watch_set);
        rt_log("data watch: every wanted field is set; the Oodle decoder is called directly again");
    }
}

void data_watch_init(void) {
    int set = rt_patch_set("data_watch");
    if (set < 0 || !rt_patch_page()) return;
    // The entry exists whenever the set does: with the set off the sites keep their original call and never come here.
    rt_register_host((uint64_t)(uintptr_t)rt_patch_page() + PAGE_HOST, host_oodle);
    guest_decoder = ldr_module(0)->base + OODLE_DECOMPRESS;
    // Distance fields in Oodle-packed files are changed after the decode, so a list with such fields turns the set on.
    data_patch_init();
    if (!rt_patch_enabled(set) && data_patch_wants_decodes()) {
        rt_patch_enable(set);
        rt_log("data watch: set data_watch on for the distance fields in packed files (VIEW_DIST / SPAWN_DIST)");
    }
    if (!rt_patch_enabled(set)) return;
    rt_log("data watch: every call of the Oodle decoder main+%#x goes through the host", OODLE_DECOMPRESS);
    const char *dw = cfg_get("TOTK_DATA_WATCH");
    watch_set = set;
    if (dw && *dw && strcmp(dw, "0")) dumping = 1, load_list();
}
