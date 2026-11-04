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
//   TOTK_DATA_FIND=1   find every double 1/30 (0x3fa1111111111111) in what the game reads from RomFS and unpacks with
//                      Oodle, and append a line for each to game/data_find.txt with the value 1/60 (group
//                      "mod=0.016666666666666666"); TOTK_MOD_VALUES=game/data_find.txt then applies them. Violet's
//                      indoor walking steps by a double 1/30 that two Lua-like interpreter opcodes read (main+0x4135c,
//                      main+0x422e4; docs/progress.md 2026-11-03), so the constant should sit in a script file.
//   TOTK_SCRIPT_STEP=1 (default; 0 turns it off) replaces every double 1/30 in what the game reads and unpacks with one
//                      frame at the frame limit (1/60 at 60 fps, 1/120 at 120) as it loads, so the scripts step their
//                      fixed 1/30 s updates (indoor walking) once per frame: with the 13 found in 4.0.0 set to 1/60
//                      walking indoors ran smooth at 60 fps and nothing else changed (maintainer's test,
//                      docs/progress.md 2026-11-03). Their accumulators take the frame time, so the speed stays right
//                      at any frame rate. The value is taken once, at the first file: scripts keep it in their own
//                      constants, so a new frame limit reaches them only after a restart.
// type f64 is a 64-bit double, u8 a byte.
//   msgtext <packed> <unpacked> <fnv1a64> <line> <tag> <text>
//                      a line of a message file (.dat, tools/msgpatch.py --relocate) gets a new text of any length:
//                      the runtime writes it, encrypted like the game's (UTF-16, each unit XOR a key that starts at
//                      0x7C89 + 0x2983 * line and turns left by 3 bits), to spare memory above the heap
//                      (memory.c mem_spare_near: in the alias region, where Violet keeps its memory) and points the
//                      line's offset and length there; the file's section length grows to cover it, since the game
//                      reads no line beyond it. The game reads the offset as signed, so the text lies less than
//                      2 GiB after the file (maintainer's tests, docs/progress.md 2026-11-04). The text is UTF-8;
//                      "\n" is a line break. "@<line> " before the text puts it in the space of that other (unused)
//                      line of the same file instead, cut to fit. For the PC settings page in the game's options (docs/progress.md
//                      2026-11-04), whose texts the game fetches by label only.
//   TOTK_UI_PATCH=1    set the bytes of the game's UI files that tools/uilayout.py --hide lists (game/ui_patch.txt,
//                      group "mod=<value>"): the pane flags of panes to hide, such as the main menu's
//                      downloadable-content panel N_dlc_00 (docs/progress.md 2026-11-03). A path names another list.
//
// The original values never leave the player's PC; the log names fields and offsets only.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

double gfx_window_refresh_hz(void);   // ui/menu.h, which is for the menu's C++ side too

#define MAX_FIELDS 1024

typedef struct {
    uint64_t at;   // byte offset in romfs.bin, or in the unpacked file (decoded)
    uint64_t packed, unpacked, print;   // decoded: which file
    char type;     // 'f' f32, 'i' i32, 'u' u32, 'h' i16, 'H' u16, 'd' f64, 'b' u8
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

#define MAX_TEXTS 1024
typedef struct {
    uint64_t packed, unpacked, print;   // which file
    uint32_t line;
    int donor;         // >= 0: the text goes in the space of that line of the file (cut to fit), not spare memory
    int done;
    char tag[64];
    uint16_t *units;   // the text in UTF-16 with its terminator, not encrypted
    uint32_t nunits;
} MsgText;
static MsgText texts[MAX_TEXTS];
static int ntexts;
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

static int find_on;      // TOTK_DATA_FIND
static int step_on = -1; // TOTK_SCRIPT_STEP
static volatile int64_t steps_set;

static int step_wanted(void) {
    if (step_on < 0) step_on = rt_knob("SCRIPT_STEP", "1");
    return step_on;
}

int data_patch_wants_decodes(void) { return ndecoded > 0 || ntexts > 0 || find_on || step_wanted(); }

// Fields of packed files not set yet (data_watch.c turns its decoder redirect off at 0).
int data_patch_open(void) {
    if (find_on || step_wanted()) return 1;   // the search and the step replacement want every decode
    int n = 0;
    for (int i = 0; i < ndecoded; i++) n += !__atomic_load_n(&decoded[i].done, __ATOMIC_RELAXED);
    for (int i = 0; i < ntexts; i++) n += !__atomic_load_n(&texts[i].done, __ATOMIC_RELAXED);
    return n;
}

// UTF-8 text (with "\n" for a line break) to UTF-16 units and a terminator; characters beyond the BMP become '?'.
static uint32_t to_utf16(const char *s, uint16_t *out, uint32_t cap) {
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n + 1 < cap) {
        uint32_t c = *p++;
        if (c == '\\' && *p == 'n') { p++; c = '\n'; }
        else if (c >= 0xF0) { c = '?'; while ((*p & 0xC0) == 0x80) p++; }
        else if (c >= 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) {
            c = (c & 0x1F) << 6 | (p[0] & 0x3F);
            p++;
        } else if (c >= 0x80) c = '?';
        out[n++] = (uint16_t)c;
    }
    out[n++] = 0;
    return n;
}

// "msgtext <packed> <unpacked> <fnv1a64> <line> <tag> <text>"; 1 taken, 0 not such a line.
static int load_text(const char *line) {
    unsigned long long packed, unpacked, print;
    unsigned idx;
    char tag[64];
    int at = 0;
    if (sscanf(line, "msgtext %llu %llu %llx %u %63s %n", &packed, &unpacked, &print, &idx, tag, &at) != 5 || !at)
        return 0;
    if (ntexts >= MAX_TEXTS) return 0;
    int donor = -1, skip = 0;
    if (line[at] == '@' && sscanf(line + at, "@%d %n", &donor, &skip) == 1) at += skip;
    uint16_t buf[1024];
    uint32_t n = to_utf16(line + at, buf, 1024);
    MsgText *t = &texts[ntexts];
    t->donor = donor;
    t->units = malloc(n * sizeof buf[0]);
    if (!t->units) return 0;
    memcpy(t->units, buf, n * sizeof buf[0]);
    t->nunits = n, t->packed = packed, t->unpacked = unpacked, t->print = print, t->line = idx;
    snprintf(t->tag, sizeof t->tag, "%s", tag);
    ntexts++;
    return 1;
}

// A message file just unpacked to out (n bytes): header u16 languages (1), u16 lines, u32 section length, u32 key (0),
// u32 section offset; at the section, u32 length, then per line u32 offset (from the section), u16 units, u16 flags.
static void apply_text(MsgText *t, uint8_t *out, uint64_t n) {
    uint16_t langs, lines;
    uint32_t key0, sec;
    if (n < 16) return;
    memcpy(&langs, out, 2), memcpy(&lines, out + 2, 2), memcpy(&key0, out + 8, 4), memcpy(&sec, out + 12, 4);
    if (langs != 1 || key0 || t->line >= lines || (uint64_t)sec + 4 + 8ull * lines > n) {
        rt_log("data patch: %s: not a message file with line %u (%u languages, %u lines)", t->tag, t->line, langs, lines);
        return;
    }
    uint64_t base = (uintptr_t)out + sec;
    uint16_t *dst;
    if (t->donor >= 0) {
        if ((uint32_t)t->donor >= lines) return;
        uint32_t doff;
        uint16_t dlen;
        memcpy(&doff, out + sec + 4 + 8ull * t->donor, 4), memcpy(&dlen, out + sec + 4 + 8ull * t->donor + 4, 2);
        if ((uint64_t)sec + doff + 2ull * dlen > n || !dlen) return;
        if (t->nunits > dlen) t->units[dlen - 1] = 0, t->nunits = dlen;   // cut to the donor's space
        dst = (uint16_t *)(out + sec + doff);
    } else {
        dst = mem_spare_near(base, t->nunits * 2ull);
    }
    uint64_t dist = (uintptr_t)dst - base;
    if (!dst || (uintptr_t)dst < base || dist > 0x7FFFFFFFull || t->nunits > 0xFFFF) {
        rt_log("data patch: %s: no spare memory within 2 GiB after the file at %p", t->tag, (void *)out);
        return;
    }
    uint16_t key = (uint16_t)(0x7C89 + 0x2983 * t->line);
    for (uint32_t i = 0; i < t->nunits; i++) {
        dst[i] = t->units[i] ^ key;
        key = (uint16_t)(key << 3 | key >> 13);
    }
    uint32_t off = (uint32_t)dist;
    uint16_t len = (uint16_t)t->nunits;
    uint8_t *entry = out + sec + 4 + 8ull * t->line;
    memcpy(entry, &off, 4), memcpy(entry + 4, &len, 2);
    if (t->donor < 0) {
        // The game reads a line only when it lies within the section length (in the header at +4 and at the
        // section's start): a text in spare memory read back empty until that length was raised to cover it.
        uint32_t need = off + 2u * len, have;
        memcpy(&have, out + sec, 4);
        if (need > have) memcpy(out + sec, &need, 4), memcpy(out + 4, &need, 4);
    }
    rt_log("data patch: %s: line %u now %u units at %s (+%#x from its section)", t->tag, t->line, t->nunits - 1,
           t->donor >= 0 ? "the space of another line" : "spare memory", off);
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
        if (line[0] == 'm' && load_text(line)) {
            taken++;
            line = nl + 1;
            continue;
        }
        int romfs = line[0] != '#' && sscanf(line, "romfs %llx %7s %39s %63s", &at, type, group, f.name) == 4;
        int oodle = !romfs && line[0] != '#' && sscanf(line, "oodle %llu %llu %llx %llx %7s %39s %63s", &packed,
                                                       &unpacked, &print, &at, type, group, f.name) == 7;
        if (romfs || oodle) {
            f.at = at;
            if (oodle) f.packed = packed, f.unpacked = unpacked, f.print = print;
            f.type = !strcmp(type, "f32") ? 'f' : !strcmp(type, "i32") ? 'i' : !strcmp(type, "u32") ? 'u'
                   : !strcmp(type, "i16") ? 'h' : !strcmp(type, "u16") ? 'H' : !strcmp(type, "f64") ? 'd'
                   : !strcmp(type, "u8") ? 'b' : 0;
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
    find_on = rt_knob("DATA_FIND", "0");
    if (find_on) {
        FILE *fp = fopen("game/data_find.txt", "w");   // a fresh list each run
        if (fp) fputs("# double 1/30 found by TOTK_DATA_FIND (karpur); apply with TOTK_MOD_VALUES=game/data_find.txt\n", fp), fclose(fp);
        rt_log("data find: looking for double 1/30 in RomFS reads and Oodle decodes; lines go to game/data_find.txt");
    }
    const char *ui = cfg_get("TOTK_UI_PATCH");
    int uis = ui && *ui && strcmp(ui, "0");
    if (view == 1.0f && spawn == 1.0f && count == 1.0f && !mods && !uis) return;
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
    if (uis) {
        const char *path = strcmp(ui, "1") ? ui : "game/ui_patch.txt";
        int n = load_list(path, 1, 1, 1, 1, &skipped);
        if (n < 0) rt_log("data patch: UI_PATCH set, but no list %s (tools/uilayout.py --hide writes it)", path);
        else rt_log("data patch: %d UI bytes from %s", n, path);
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
        case 'd': { double v = f->value; memcpy(p, &v, 8); break; }
        case 'b': *p = (uint8_t)f->value; break;
        }
        return;
    }
    switch (f->type) {
    case 'f': { float v; memcpy(&v, p, 4); v *= f->factor; memcpy(p, &v, 4); break; }
    case 'i': { int32_t v; memcpy(&v, p, 4); v = (int32_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 4); break; }
    case 'u': { uint32_t v; memcpy(&v, p, 4); v = (uint32_t)(v * f->factor + 0.5f); memcpy(p, &v, 4); break; }
    case 'h': { int16_t v; memcpy(&v, p, 2); v = (int16_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 2); break; }
    case 'H': { uint16_t v; memcpy(&v, p, 2); v = (uint16_t)(v * f->factor + 0.5f); memcpy(p, &v, 2); break; }
    case 'd': { double v; memcpy(&v, p, 8); v *= f->factor; memcpy(p, &v, 8); break; }
    }
}

static uint64_t width(const Field *f) {
    return f->type == 'b' ? 1 : f->type == 'h' || f->type == 'H' ? 2 : f->type == 'd' ? 8 : 4;
}

// ---- TOTK_DATA_FIND: double 1/30 in the data the game reads
#define FIND_MAX 256
static const uint64_t D130 = 0x3FA1111111111111ull;
static struct { uint64_t a, b, c, d; } found[FIND_MAX];   // romfs: (off, 0, 0, 0); oodle: (unpacked, print, off, packed)
static int nfound;
static volatile int find_lock;

static void find_note(int romfs, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    while (__atomic_exchange_n(&find_lock, 1, __ATOMIC_ACQUIRE)) {}
    int i = 0;
    while (i < nfound && !(found[i].a == a && found[i].b == b && found[i].c == c)) i++;
    int fresh = i == nfound && nfound < FIND_MAX;
    if (fresh) found[nfound++] = (typeof(found[0])){a, b, c, d};
    int k = nfound;
    __atomic_store_n(&find_lock, 0, __ATOMIC_RELEASE);
    if (!fresh) return;
    char line[160];
    if (romfs) snprintf(line, sizeof line, "romfs %llx f64 mod=0.016666666666666666 d130_%d\n", (unsigned long long)a, k);
    else snprintf(line, sizeof line, "oodle %llu %llu %llx %llx f64 mod=0.016666666666666666 d130_%d\n",
                  (unsigned long long)d, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, k);
    FILE *fp = fopen("game/data_find.txt", "a");
    if (fp) fputs(line, fp), fclose(fp);
    if (k <= 64) rt_log("data find: double 1/30 %s (#%d)", romfs ? "in a stored RomFS file" : "in an Oodle-packed file", k);
}

// One frame at the frame limit, in seconds, for this whole run; the limit in Hz goes to *hz.
static double script_step(int *hz) {
    static int rate;
    if (!rate) {
        int r = frame_limit_hz();
        // the monitor's rate (TOTK_FPS_LIMIT=display) when the window has measured it, else the usual limit
        if (!r) r = (int)(gfx_window_refresh_hz() + 0.5);
        __atomic_store_n(&rate, r < 30 ? 60 : r > 360 ? 360 : r, __ATOMIC_RELAXED);
    }
    *hz = rate;
    return 1.0 / rate;
}

static void find_in(int romfs, uint8_t *p, uint64_t n, uint64_t base, uint64_t unpacked, uint64_t print,
                    uint64_t packed) {
    for (uint64_t i = 0; i + 8 <= n; i++) {
        if (p[i + 7] != 0x3F || p[i + 6] != 0xA1) continue;
        uint64_t v;
        memcpy(&v, p + i, 8);
        if (v != D130) continue;
        if (find_on) {
            if (romfs) find_note(1, base + i, 0, 0, 0);
            else find_note(0, unpacked, print, i, packed);
        }
        if (step_on > 0) {
            int hz;
            double step = script_step(&hz);
            memcpy(p + i, &step, 8);
            int64_t k = plat_inc64(&steps_set);
            if (k <= 16 || !(k & (k - 1)))
                rt_log("script step: double 1/30 #%lld in %s set to 1/%d (TOTK_SCRIPT_STEP=0 keeps it)", (long long)k,
                       romfs ? "a stored RomFS file" : "an Oodle-packed file", hz);
        }
    }
}

// A read of n bytes at byte offset off of romfs.bin landed in buf.
void data_patch_read(uint64_t off, void *buf, uint64_t n) {
    if (find_on || step_wanted()) find_in(1, buf, n, off, 0, 0, 0);
    if (!nfields || off >= hi || off + n <= lo) return;
    int i = 0, j = nfields;
    while (i < j) {   // first field at or after off
        int m = (i + j) / 2;
        if (fields[m].at < off) i = m + 1;
        else j = m;
    }
    for (; i < nfields; i++) {
        Field *f = &fields[i];
        uint64_t w = width(f);
        if (f->at + w > off + n) break;
        scale(f, (uint8_t *)buf + (f->at - off));
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s at romfs %#llx %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
}

// The game's Oodle decoder unpacked n bytes to out from packed_n bytes at packed (data_watch.c host_oodle).
// TOTK_MOD_DRY=1 finds the fields but leaves them (A/B test of the redirect alone).
void data_patch_decoded(const uint8_t *packed, uint64_t packed_n, uint8_t *out, uint64_t n) {
    static int dry = -1;
    if (dry < 0) {
        const char *e = cfg_get("TOTK_MOD_DRY");
        dry = e && *e && strcmp(e, "0");
    }
    uint64_t print = 0, print_head = 0;
    if (find_on || step_wanted()) {
        uint64_t head = packed_n < 4096 ? packed_n : 4096;
        find_in(0, out, n, 0, n, fnv1a64(packed, head), packed_n);
    }
    for (int i = 0; i < ndecoded; i++) {
        Field *f = &decoded[i];
        uint64_t head = f->packed < 4096 ? f->packed : 4096, w = width(f);
        if (f->unpacked != n || packed_n < head || f->at + w > n) continue;
        if (print_head != head) print = fnv1a64(packed, head), print_head = head;
        if (print != f->print) continue;
        if (!dry) scale(f, out + f->at);
        __atomic_store_n(&f->done, 1, __ATOMIC_RELAXED);
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s in a packed file (+%#llx) %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
    for (int i = 0; i < ntexts; i++) {
        MsgText *t = &texts[i];
        uint64_t head = t->packed < 4096 ? t->packed : 4096;
        if (t->unpacked != n || packed_n < head) continue;
        if (print_head != head) print = fnv1a64(packed, head), print_head = head;
        if (print != t->print) continue;
        if (!dry) apply_text(t, out, n);
        __atomic_store_n(&t->done, 1, __ATOMIC_RELAXED);
    }
}
