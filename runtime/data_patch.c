// KarPur: view and spawn distances of Violet, changed in the data files as the game reads them.
//
// Violet keeps its fade-out distance for models (world/data/gfx/model_dither_fade: ModelDitherFadeDefaultParam,
// FarBeginDistance / FarEndDistance, 75 / 90 in 4.0.0) and the create and destroy distances of field objects and
// Pokémon (world/data/system/object_generation_range: ObjectGenerationRange) in small FlatBuffers files that are stored
// in arc/data.trpfs, some stored, some Oodle-packed (tools/trpfs.py --report, docs/progress.md 2026-11-02).
// tools/trpfs.py --view writes game/data_patch.txt, one line per field:
//   romfs <offset in romfs.bin> <type> <group> <name>            a field of a stored file: changed in every RomFS read
//                                                                that covers it (romfs.c romfs_read)
//   oodle <packed> <unpacked> <fnv1a64 of the first 4 KiB packed> <offset in the file> <type> <group> <name>
//                                                                a field of a packed file: changed right after the
//                                                                game's Oodle decode (data_watch.c, which turns the
//                                                                patch set data_watch on for it)
// type is f32, i32, u32, i16 or u16; the field is multiplied by its group's factor:
//
//   TOTK_VIEW_DIST=f   group "fade": far fade distances (objects and trees stay visible f times as far); 1 = off
//   TOTK_SPAWN_DIST=f  group "spawn": create/destroy distances of objects and Pokémon; 1 = off (more CPU load)
//   TOTK_SPAWN_COUNT=f group "count": maxGenerationNum, how many of a kind exist at once (15 for wild Pokémon in
//                      4.0.0; a known spawn mod raises it to 20..48 together with the distances); 1 = off
//   TOTK_DATA_PATCH_LIST=path  the list (default game/data_patch.txt)
//   TOTK_MOD_VALUES=1  set the values a community mod has in these fields (game/mod_patch.txt, written by
//                      tools/mod_report.py --apply: group "mod=<value>"), as if the mod were installed
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
    uint64_t at;   // byte offset in romfs.bin, or in the unpacked file (decoded)
    uint64_t packed, unpacked, print;   // decoded: which file
    char type;     // 'f' f32, 'i' i32, 'u' u32, 'h' i16, 'H' u16
    float factor;
    int done;          // decoded: set at least once
    int absolute;      // set value instead of multiplying (a mod's value, group "mod=<value>")
    double value;
    char name[64];
} Field;

static Field fields[MAX_FIELDS];   // romfs fields, sorted by offset
static int nfields;
static Field decoded[MAX_FIELDS];  // fields of Oodle-packed files
static int ndecoded;
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

int data_patch_wants_decodes(void) { return ndecoded > 0; }

// Fields of packed files not set yet (data_watch.c turns its decoder redirect off at 0).
int data_patch_open(void) {
    int n = 0;
    for (int i = 0; i < ndecoded; i++) n += !__atomic_load_n(&decoded[i].done, __ATOMIC_RELAXED);
    return n;
}

static uint64_t fnv1a64(const uint8_t *p, uint64_t n) {
    uint64_t h = 0xCBF29CE484222645ull;
    for (uint64_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// One list file; returns the fields taken, adds the left-alone ones to *skipped; -1 when the file is missing.
static int load_list(const char *path, float view, float spawn, float count, int mods, int *skipped) {
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    if (!text) return -1;
    int taken = 0;
    char *end = text + size, *line = text;
    while (line < end) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        if (!nl) nl = end;
        *nl = 0;
        unsigned long long at, packed, unpacked, print;
        char type[8], group[40];
        Field f = {0};
        int romfs = line[0] != '#' && sscanf(line, "romfs %llx %7s %39s %63s", &at, type, group, f.name) == 4;
        int oodle = !romfs && line[0] != '#' && sscanf(line, "oodle %llu %llu %llx %llx %7s %39s %63s", &packed,
                                                       &unpacked, &print, &at, type, group, f.name) == 7;
        if (romfs || oodle) {
            f.at = at;
            if (oodle) f.packed = packed, f.unpacked = unpacked, f.print = print;
            f.type = !strcmp(type, "f32") ? 'f' : !strcmp(type, "i32") ? 'i' : !strcmp(type, "u32") ? 'u'
                   : !strcmp(type, "i16") ? 'h' : !strcmp(type, "u16") ? 'H' : 0;
            if (!strncmp(group, "mod=", 4)) {
                f.absolute = mods, f.value = strtod(group + 4, NULL), f.factor = mods ? 0.0f : 1.0f;
            } else {
                f.factor = !strcmp(group, "fade") ? view : !strcmp(group, "spawn") ? spawn
                         : !strcmp(group, "count") ? count : 1.0f;
            }
            if (!f.type || (!f.absolute && f.factor == 1.0f)) (*skipped)++;
            else if (romfs && nfields < MAX_FIELDS) fields[nfields++] = f, taken++;
            else if (oodle && ndecoded < MAX_FIELDS) decoded[ndecoded++] = f, taken++;
        }
        line = nl + 1;
    }
    free(text);
    return taken;
}

void data_patch_init(void) {
    static int done;
    if (done) return;
    done = 1;
    float view = factor_of("TOTK_VIEW_DIST"), spawn = factor_of("TOTK_SPAWN_DIST"), count = factor_of("TOTK_SPAWN_COUNT");
    // TOTK_MOD_VALUES=1 (or a list path): the values tools/mod_report.py --apply took from a mod (game/mod_patch.txt)
    const char *mv = cfg_get("TOTK_MOD_VALUES");
    int mods = mv && *mv && strcmp(mv, "0");
    if (view == 1.0f && spawn == 1.0f && count == 1.0f && !mods) return;
    int skipped = 0;
    if (view != 1.0f || spawn != 1.0f || count != 1.0f) {
        const char *path = cfg_get("TOTK_DATA_PATCH_LIST");
        if (!path || !*path) path = "game/data_patch.txt";
        int n = load_list(path, view, spawn, count, 0, &skipped);
        if (n < 0) rt_log("data patch: VIEW_DIST/SPAWN_DIST set, but no list %s (tools/trpfs.py --view writes it)", path);
    }
    if (mods) {
        const char *path = strcmp(mv, "1") ? mv : "game/mod_patch.txt";
        int n = load_list(path, 1, 1, 1, 1, &skipped);
        if (n < 0) rt_log("data patch: MOD_VALUES set, but no list %s (tools/mod_report.py --apply writes it)", path);
        else rt_log("data patch: %d values of a mod from %s", n, path);
    }
    qsort(fields, (size_t)nfields, sizeof fields[0], by_offset);
    if (nfields) lo = fields[0].at, hi = fields[nfields - 1].at + 4;
    rt_log("data patch: %d fields in stored files and %d in packed files (%d left alone); view distance x%g, "
           "spawn distance x%g, spawn count x%g", nfields, ndecoded, skipped, view, spawn, count);
    if (!nfields && !ndecoded) rt_log("data patch: the lists name no field: check tools/trpfs.py --view / mod_report.py");
}

static void scale(Field *f, uint8_t *p) {
    if (f->absolute) {
        switch (f->type) {
        case 'f': { float v = (float)f->value; memcpy(p, &v, 4); break; }
        case 'i': { int32_t v = (int32_t)f->value; memcpy(p, &v, 4); break; }
        case 'u': { uint32_t v = (uint32_t)f->value; memcpy(p, &v, 4); break; }
        case 'h': { int16_t v = (int16_t)f->value; memcpy(p, &v, 2); break; }
        case 'H': { uint16_t v = (uint16_t)f->value; memcpy(p, &v, 2); break; }
        }
        return;
    }
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
        if (k <= 64) rt_log("data patch: %s at romfs %#llx %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
}

// The game's Oodle decoder unpacked n bytes to out from packed_n bytes at packed (data_watch.c host_oodle).
void data_patch_decoded(const uint8_t *packed, uint64_t packed_n, uint8_t *out, uint64_t n) {
    uint64_t print = 0, print_head = 0;
    for (int i = 0; i < ndecoded; i++) {
        Field *f = &decoded[i];
        uint64_t head = f->packed < 4096 ? f->packed : 4096, w = f->type == 'h' || f->type == 'H' ? 2 : 4;
        if (f->unpacked != n || packed_n < head || f->at + w > n) continue;
        if (print_head != head) print = fnv1a64(packed, head), print_head = head;
        if (print != f->print) continue;
        scale(f, out + f->at);
        __atomic_store_n(&f->done, 1, __ATOMIC_RELAXED);
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s in a packed file (+%#llx) %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
}
