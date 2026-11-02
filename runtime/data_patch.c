// KarPur: view and spawn distances of Violet, changed in the data files as the game reads them.
//
// Violet keeps its fade-out distance for models (world/data/gfx/model_dither_fade: ModelDitherFadeDefaultParam,
// FarBeginDistance / FarEndDistance, 75 / 90 in 4.0.0) and the create and destroy distances of field objects and
// Pokémon (world/data/system/object_generation_range: ObjectGenerationRange) in small FlatBuffers files that are stored
// unpacked in arc/data.trpfs (tools/trpfs.py --report, docs/progress.md 2026-11-02). tools/trpfs.py --view writes
// game/data_patch.txt: one line per field, its byte offset in romfs.bin, its type (f32, i32, u32, i16, u16), a group
// and its name. Every RomFS read (romfs.c romfs_read) that covers such a field multiplies it in the guest's buffer:
//
//   TOTK_VIEW_DIST=f   group "fade": far fade distances (objects and trees stay visible f times as far); 1 = off
//   TOTK_SPAWN_DIST=f  group "spawn": create/destroy distances of objects and Pokémon; 1 = off (more CPU load)
//   TOTK_DATA_PATCH_LIST=path  the list (default game/data_patch.txt)
//
// The original values never leave the player's PC; the log names fields and offsets only.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

#define MAX_FIELDS 1024

typedef struct {
    uint64_t at;   // byte offset in romfs.bin
    char type;     // 'f' f32, 'i' i32, 'u' u32, 'h' i16, 'H' u16
    float factor;
    char name[64];
} Field;

static Field fields[MAX_FIELDS];
static int nfields;
static uint64_t lo = UINT64_MAX, hi;
static uint64_t applied;

static float factor_of(const char *key) {
    const char *e = cfg_get(key);
    float f = e && *e ? strtof(e, NULL) : 1.0f;
    return f > 0.05f && f < 100.0f ? f : 1.0f;
}

static int by_offset(const void *a, const void *b) {
    uint64_t x = ((const Field *)a)->at, y = ((const Field *)b)->at;
    return x < y ? -1 : x > y;
}

void data_patch_init(void) {
    float view = factor_of("TOTK_VIEW_DIST"), spawn = factor_of("TOTK_SPAWN_DIST");
    if (view == 1.0f && spawn == 1.0f) return;
    const char *path = cfg_get("TOTK_DATA_PATCH_LIST");
    if (!path || !*path) path = "game/data_patch.txt";
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    if (!text) {
        rt_log("data patch: VIEW_DIST/SPAWN_DIST set, but no list %s (tools/trpfs.py --view writes it)", path);
        return;
    }
    int skipped = 0;
    char *end = text + size, *line = text;
    while (line < end) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        if (!nl) nl = end;
        *nl = 0;
        unsigned long long at;
        char type[8], group[16];
        Field f = {0};
        if (line[0] != '#' && sscanf(line, "%llx %7s %15s %63s", &at, type, group, f.name) == 4) {
            f.at = at;
            f.type = !strcmp(type, "f32") ? 'f' : !strcmp(type, "i32") ? 'i' : !strcmp(type, "u32") ? 'u'
                   : !strcmp(type, "i16") ? 'h' : !strcmp(type, "u16") ? 'H' : 0;
            f.factor = !strcmp(group, "fade") ? view : !strcmp(group, "spawn") ? spawn : 1.0f;
            if (f.type && f.factor != 1.0f && nfields < MAX_FIELDS) fields[nfields++] = f;
            else skipped++;
        }
        line = nl + 1;
    }
    free(text);
    qsort(fields, (size_t)nfields, sizeof fields[0], by_offset);
    if (nfields) lo = fields[0].at, hi = fields[nfields - 1].at + 4;
    rt_log("data patch: %d fields from %s (%d left alone); view distance x%g, spawn distance x%g", nfields, path,
           skipped, view, spawn);
}

static void scale(Field *f, uint8_t *p) {
    switch (f->type) {
    case 'f': { float v; memcpy(&v, p, 4); v *= f->factor; memcpy(p, &v, 4); break; }
    case 'i': { int32_t v; memcpy(&v, p, 4); v = (int32_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 4); break; }
    case 'u': { uint32_t v; memcpy(&v, p, 4); v = (uint32_t)(v * f->factor + 0.5f); memcpy(p, &v, 4); break; }
    case 'h': { int16_t v; memcpy(&v, p, 2); v = (int16_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 2); break; }
    case 'H': { uint16_t v; memcpy(&v, p, 2); v = (uint16_t)(v * f->factor + 0.5f); memcpy(p, &v, 2); break; }
    }
}

// A read of n bytes at byte offset off of romfs.bin landed in buf.
void data_patch_read(uint64_t off, void *buf, uint64_t n) {
    if (!nfields || off >= hi || off + n <= lo) return;
    int i = 0, j = nfields;
    while (i < j) {   // first field at or after off
        int m = (i + j) / 2;
        if (fields[m].at < off) i = m + 1;
        else j = m;
    }
    for (; i < nfields; i++) {
        Field *f = &fields[i];
        uint64_t w = f->type == 'h' || f->type == 'H' ? 2 : 4;
        if (f->at + w > off + n) break;
        scale(f, (uint8_t *)buf + (f->at - off));
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s at romfs %#llx x%g", f->name, (unsigned long long)f->at, f->factor);
    }
}
