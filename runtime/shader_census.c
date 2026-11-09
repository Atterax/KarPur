// KarPur: where Violet keeps its shader programs (TOTK_SHADER_CENSUS=1, a diagnostic run that ends the process).
//
// Purpose (docs/progress.md, shader pre-build): building every shader after the install needs the shader code from the
// player's own game files. The code is inside Oodle-packed files of arc/data.trpfs (tools/shader_where.py found none of
// the recorded programs in the unpacked RomFS), and Oodle is unpacked only by the game's own decoder. So the census runs
// inside the game: at the game's first decode (data_watch.c, the decoder's hook) it unpacks every Oodle-packed file of
// data.trpfs with the game's OodleLZ_Decompress itself, on that thread and with the arguments of that call (the six
// arguments on the guest stack stay where they are: rt_call leaves sp alone), into a buffer of its own below
// RT_ADDRESS_LIMIT, where guest addresses are host addresses. Each unpacked file is searched for the programs the
// runtime recorded in play (shader-programs.bin next to karpur.exe, the player's own data): an 8-byte anchor from the
// middle of each program's code, at every byte, confirmed by 64 bytes around it.
//
// The report (game/report/shader-census.txt and the log) has numbers and names only: files and packs that hold
// programs, the programs' offsets in them, the files' sizes and their 4-character tag when it is printable text (a
// format name such as "BNSH"). No file contents. The process ends when it is written.
//
// To plan the extractor it also describes each holder's container (format names only): the SARC entry whose data
// holds the code (its file name), the nearest "BNSH" before the code and how many the file has, the nearest
// format-name-like word (4 of A-Z 0-9 _, 4-aligned) before the code, and for the first files of each tag a map of
// such words (first offset and count). Sums by tag, by pack type and by SARC entry type, and the programs not found.
//
// The code always sits in a BNSH (test CI). To learn its layout, for the first two programs of each file the census
// measures (numbers only): where the program's control section lies (exact match, or how many of its first bytes
// match), where u32 fields of that BNSH hold the code's and the control section's offsets from the BNSH's start, and
// where the code length (control+0x704) and the control size 0x880 sit next to the code's offset field. For the first
// files of each tag it also lists the BNSH's first 24 u32 header words (offsets, sizes and versions).
//
// Extraction (test CJ found the layout): inside a BNSH, a program's shader info holds u64 control offset, u64 code
// offset and u32 code length, offsets from the BNSH's start, and the control section's length field (+0x704) repeats
// that length. Every BNSH of every unpacked file (header: "BNSH", 0, BOM 0xFEFF at +0xC, file size at +0x1C) is walked
// for such triples; the stage comes from the shader program header after the code's first 0x30 bytes (Maxwell SPH:
// type 1 with stage 1..4, or type 2 with stage 5), else compute. Each distinct program (by its code hash, the same as
// gfx_shader.c records) goes to seed-programs.bin next to karpur.exe in shader-programs.bin's form (the player's own
// data, never shared), with a seed answers record in seed-answers.bin: karpur.exe --prepare-shaders then translates
// all of them and builds their stage libraries into the driver's cache. The report counts them and checks them
// against the recorded programs: how many the extraction reproduces exactly, and where its stage differs.
//
// The seed answers: no Violet shader asks for texture or image types (the use masks of all 3401 recorded variants have
// no slots), so they are the defaults: float vertex attributes, float colour outputs and, for compute, the local size
// of the control section (+0x748, 0 read as 1), as vk_exec_draw.c answers_for gives them. Against play this matches
// 3385 of the 3401 recorded variants (7 vertex shaders read integer attributes, 2 fragment shaders write integer
// targets, 7 compute records carry a shared memory size); those few are translated in play as before. The priority
// byte is 0 for programs play recorded, 2 for the rest.
//
// Formats as tools/trpfs.py reads them (FlatBuffers): data.trpfd root {file_hashes [u64], pack_names [string],
// file_info [..], pack_info [{size u64, file_count u64}]}; data.trpfs "ONEPACK\0", u64 offset of root {pack_hashes
// [u64], pack_offsets [u64]}; a pack root {file_hashes [u64], files [{?, compression i8, level u8, size u64, data
// [u8]}]} with the data vector the table's last field.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "fs.h"
#include "gfx.h"
#include "host.h"
#include "platform/platform.h"

#define CONTROL 0x880
#define PRG_HEAD (12 + 32 + CONTROL)   // gfx_shader.c
#define BUF_SIZE (1ull << 30)          // packed + unpacked file, guest-visible

int shader_census_wanted(void) {
    static int on = -1;
    if (on < 0) on = rt_knob("SHADER_CENSUS", "0");
    return on;
}

// ---- FlatBuffers, read-only (no bounds trust beyond the buffer)
typedef struct { const uint8_t *b; uint64_t n; uint64_t pos; uint16_t nslots; const uint8_t *vt; } Fb;

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static int fb_at(Fb *t, const uint8_t *b, uint64_t n, uint64_t pos) {
    if (pos + 4 > n) return 0;
    int32_t back;
    memcpy(&back, b + pos, 4);
    uint64_t vt = pos - back;
    if (vt + 4 > n) return 0;
    uint16_t vsize;
    memcpy(&vsize, b + vt, 2);
    t->b = b, t->n = n, t->pos = pos, t->vt = b + vt, t->nslots = vsize >= 4 ? (uint16_t)((vsize - 4) / 2) : 0;
    return 1;
}
static int fb_root(Fb *t, const uint8_t *b, uint64_t n, uint64_t at) { return at + 4 <= n && fb_at(t, b, n, at + u32(b + at)); }
static uint16_t fb_slot(const Fb *t, int i) { uint16_t s = 0; if (i < t->nslots) memcpy(&s, t->vt + 4 + 2 * i, 2); return s; }
// vector field i: start of its elements and their count
static int fb_vec(const Fb *t, int i, uint64_t *start, uint64_t *count) {
    uint16_t s = fb_slot(t, i);
    if (!s) return 0;
    uint64_t at = t->pos + s;
    if (at + 4 > t->n) return 0;
    uint64_t v = at + u32(t->b + at);
    if (v + 4 > t->n) return 0;
    *start = v + 4, *count = u32(t->b + v);
    return 1;
}
static int64_t fb_i8(const Fb *t, int i, int64_t def) { uint16_t s = fb_slot(t, i); return s ? (int8_t)t->b[t->pos + s] : def; }
static uint64_t fb_u64(const Fb *t, int i) { uint16_t s = fb_slot(t, i); return s && t->pos + s + 8 <= t->n ? u64(t->b + t->pos + s) : 0; }
static int fb_table(const Fb *t, uint64_t vstart, uint64_t k, Fb *out) {
    uint64_t e = vstart + 4 * k;
    return e + 4 <= t->n && fb_at(out, t->b, t->n, e + u32(t->b + e));
}
static const char *fb_string(const Fb *t, uint64_t vstart, uint64_t k, uint32_t *len) {
    uint64_t e = vstart + 4 * k;
    if (e + 4 > t->n) return NULL;
    uint64_t s = e + u32(t->b + e);
    if (s + 4 > t->n) return NULL;
    *len = u32(t->b + s);
    return s + 4 + *len <= t->n ? (const char *)t->b + s + 4 : NULL;
}

static uint64_t fnv1a64(const char *s, uint32_t n) {
    uint64_t h = 0xCBF29CE484222645ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 0x100000001B3ull;
    return h;
}

// ---- the recorded programs and their anchors (open addressing on the anchor)
typedef struct { const uint8_t *code; uint32_t len, stage; uint64_t at; int found; } Prog;
typedef struct { uint64_t key; int prog; } Slot;
static Prog *progs;
static int nprogs;
static Slot *tab;
static uint64_t tab_mask;
static uint8_t *filter;   // a bit per (anchor * K) >> 40: rules out most positions before the table

static uint64_t mix(uint64_t v) { return v * 0x9E3779B97F4A7C15ull; }

static int load_programs(void) {
    char path[1024];
    int n = plat_exe_path(path, sizeof path - 32);
    char *cut = n ? plat_last_sep(path) : NULL;
    if (!cut) return 0;
    strcpy(cut + 1, "shader-programs.bin");
    uint64_t size = 0;
    uint8_t *b = plat_file_read(path, &size);
    if (!b || size < 16 || memcmp(b, "TOTKSPRG", 8)) {   // a fresh install: extraction only, nothing to compare with
        rt_log("shader census: no %s: the extraction runs, the search for recorded programs does not", path);
        return 0;
    }
    int cap = 1024;
    progs = calloc((size_t)cap, sizeof *progs);
    for (uint64_t o = 16; size - o >= PRG_HEAD;) {
        uint32_t stage = u32(b + o), len = u32(b + o + 4), c1 = u32(b + o + 8);
        uint64_t rec = PRG_HEAD + (uint64_t)len + c1;
        if (len > (64u << 20) || c1 > len || rec > size - o) break;
        if (nprogs == cap) progs = realloc(progs, sizeof *progs * (size_t)(cap *= 2));
        progs[nprogs++] = (Prog){b + o + PRG_HEAD, len, stage, 0, 0};
        o += rec;
    }
    uint64_t want = 1;
    while (want < (uint64_t)nprogs * 4) want <<= 1;
    tab = calloc(want, sizeof *tab), tab_mask = want - 1;
    filter = calloc(1u << 21, 1);   // 2^24 bits
    int anchors = 0;
    for (int i = 0; i < nprogs; i++) {
        if (progs[i].len < 128) { progs[i].found = -1; continue; }
        uint64_t at = ((uint64_t)progs[i].len / 2) & ~7ull, key = u64(progs[i].code + at);
        if (!key || key == ~0ull) { progs[i].found = -1; continue; }
        progs[i].at = at;
        uint64_t h = mix(key);
        filter[(h >> 40) >> 3] |= (uint8_t)(1u << ((h >> 40) & 7));
        for (uint64_t s = h & tab_mask;; s = (s + 1) & tab_mask)
            if (!tab[s].key) { tab[s] = (Slot){key, i + 1}; anchors++; break; }
    }
    rt_log("shader census: %d recorded programs, %d anchored", nprogs, anchors);
    return nprogs > 0;
}

// ---- extraction from BNSH
#define HSET_BITS 20
typedef struct { uint8_t h[32]; uint8_t used; } HSlot;
static HSlot *rec_set, *out_set;          // recorded code hashes, extracted ones
static uint64_t n_not_shader, n_bnsh, n_triples, n_out, n_out_stage[6], n_reproduced, n_stage_wrong, out_bytes, n_hash_fail;
static uint64_t conf[6][6];               // [recorded stage][guessed stage] for programs reproduced under any stage
static FILE *out_f, *ans_f;
static uint8_t *out_buf;
static size_t out_n;

static int hset_put(HSlot *set, const uint8_t h[32]) {   // 1 when new
    uint64_t i;
    memcpy(&i, h, 8);
    for (i &= (1u << HSET_BITS) - 1;; i = (i + 1) & ((1u << HSET_BITS) - 1)) {
        if (!set[i].used) { memcpy(set[i].h, h, 32), set[i].used = 1; return 1; }
        if (!memcmp(set[i].h, h, 32)) return 0;
    }
}
static int hset_has(const HSlot *set, const uint8_t h[32]) {
    uint64_t i;
    memcpy(&i, h, 8);
    for (i &= (1u << HSET_BITS) - 1; set[i].used; i = (i + 1) & ((1u << HSET_BITS) - 1))
        if (!memcmp(set[i].h, h, 32)) return 1;
    return 0;
}

static int guess_stage(const uint8_t *code, uint32_t len) {
    if (len < 0x30 + 0x50) return GFX_STAGE_CS;
    uint32_t w = u32(code + 0x30), type = w & 0x1f, ver = (w >> 5) & 0x1f, st = (w >> 10) & 0xf;
    if (ver == 0) return GFX_STAGE_CS;
    if (type == 1 && st >= 1 && st <= 4) return (int)st - 1;   // vertex, tessellation control / evaluation, geometry
    if (type == 2 && st == 5) return GFX_STAGE_FS;
    return GFX_STAGE_CS;
}

static void out_flush(void) {
    if (out_f && out_n) fwrite(out_buf, 1, out_n, out_f);
    out_n = 0;
}

static void extract_program(const uint8_t *ctrl, const uint8_t *code, uint32_t len) {
    int g = guess_stage(code, len);
    if (g == GFX_STAGE_CS) {   // no shader program header: compute only with a sane local size (test CL: the 146
        uint64_t total = 1;    // others had sizes like 37054 x 0 x 36, failed to translate and are no shaders)
        for (int k = 0; k < 3; k++) {
            uint32_t l = u32(ctrl + 0x748 + 4 * k);
            total *= l > 1024 ? 1u << 20 : l ? l : 1;
        }
        if (total > 1024) { n_not_shader++; return; }
    }
    uint8_t h[32];
    if (!gfx_shader_code_hash(g, ctrl, code, h)) { n_hash_fail++; return; }
    if (n_out >= (7u << HSET_BITS) / 8) { n_hash_fail++; return; }   // the set is full: counted, not kept
    if (!hset_put(out_set, h)) return;
    // checked against the recorded programs, also under the other stages
    if (hset_has(rec_set, h)) n_reproduced++, conf[g][g]++;
    else
        for (int t = 0; t < 6; t++) {
            uint8_t h2[32];
            if (t == g || !gfx_shader_code_hash(t, ctrl, code, h2) || !hset_has(rec_set, h2)) continue;
            n_stage_wrong++, conf[t][g]++;
            break;
        }
    n_out++, n_out_stage[g]++;
    uint32_t c1_off = u32(ctrl + 0x700), c1 = len - c1_off;
    size_t rec = PRG_HEAD + (size_t)len + c1;
    if (out_n + rec > (64u << 20)) out_flush();
    if (rec > (64u << 20)) return;
    uint8_t *o = out_buf + out_n;
    uint32_t head[3] = {(uint32_t)g, len, c1};
    memcpy(o, head, 12), memcpy(o + 12, h, 32), memcpy(o + 44, ctrl, CONTROL), memcpy(o + PRG_HEAD, code, len);
    memcpy(o + PRG_HEAD + len, code + c1_off, c1);
    out_n += rec, out_bytes += rec;
    // its seed answers record (gfx_shader.c ANS_REC: stage u32, code hash, input id 0, packed answers, priority)
    uint8_t a[4 + 32 + 32 + 147 + 1] = {0};
    a[0] = (uint8_t)g;
    memcpy(a + 4, h, 32);
    if (g == GFX_STAGE_CS)
        for (int k = 0; k < 3; k++) {
            uint32_t l = u32(ctrl + 0x748 + 4 * k);
            l = l ? l : 1;
            a[68 + 136 + 2 * k] = (uint8_t)l, a[68 + 137 + 2 * k] = (uint8_t)(l >> 8);
        }
    a[sizeof a - 1] = hset_has(rec_set, h) ? 0 : 2;
    if (ans_f) fwrite(a, sizeof a, 1, ans_f);
}

static void extract(const uint8_t *b, uint64_t n) {
    for (uint64_t base = 0; base + 0x60 <= n; base += 4) {
        if (u32(b + base) != 0x48534E42u || u32(b + base + 4) || (u32(b + base + 0xC) & 0xFFFF) != 0xFEFF) continue;
        uint64_t size = u32(b + base + 0x1C);
        if (size < 0x60 || size > n - base) continue;
        const uint8_t *bn = b + base;
        n_bnsh++;
        for (uint64_t j = 0x20; j + 24 <= size; j += 8) {
            uint64_t co = u64(bn + j), xo = u64(bn + j + 8);
            uint32_t len = u32(bn + j + 16);
            if (!len || (xo & 0xFF) || co & 7 || co + CONTROL > size || xo >= size || len > size - xo) continue;
            if (u32(bn + co + 0x704) != len || u32(bn + co + 0x700) > len) continue;
            n_triples++;
            extract_program(bn + co, bn + xo, len);
        }
        base += size - 4;   // the next BNSH after this one
    }
}

static void extract_begin(void) {
    rec_set = calloc(1u << HSET_BITS, sizeof *rec_set), out_set = calloc(1u << HSET_BITS, sizeof *out_set);
    out_buf = malloc(64u << 20);
    for (int i = 0; i < nprogs; i++) hset_put(rec_set, progs[i].code - CONTROL - 32);
    char path[1024];
    int n = plat_exe_path(path, sizeof path - 32);
    char *cut = n ? plat_last_sep(path) : NULL;
    if (!cut) return;
    strcpy(cut + 1, "seed-programs.bin");
    out_f = fopen(path, "wb");
    strcpy(cut + 1, "seed-answers.bin");
    ans_f = fopen(path, "wb");
    if (!out_f || !ans_f) { rt_log("shader census: cannot write the seed files next to karpur.exe"); return; }
    struct { char magic[8]; uint32_t version, rec_size; } hd = {"TOTKSPRG", 1, 0}, ha = {"TOTKSANS", 1, 4 + 32 + 32 + 147 + 1};
    fwrite(&hd, sizeof hd, 1, out_f);
    fwrite(&ha, sizeof ha, 1, ans_f);
}

// ---- results
typedef struct {
    uint64_t pack, file, size, first_off, last_off;
    uint32_t hits, tag;
    int stored;
    char inner[64];                 // SARC entry holding the first code start ("" when not a SARC)
    int inner_count;                // SARC entries holding code
    uint32_t near_magic;            // the nearest format-name-like word before the first code start
    uint64_t near_dist;
    uint32_t bnsh_count;            // "BNSH" words in the file
    uint64_t bnsh_dist;             // from the nearest "BNSH" before the first code start (~0: none)
    char map[640];                  // format-name-like words of the file, for the first files of each tag
    int sample[2];                  // the first programs found in the file (index + 1) and their code starts
    uint64_t sample_at[2];
    char layout[2][400];            // their BNSH layout measurements
    char head[300];                 // the BNSH's first u32 words, for the first files of each tag
} Holder;
static Holder holders[4096];
static int nholders;

static int magic_like(const uint8_t *p) {
    if (p[0] < 'A' || p[0] > 'Z') return 0;
    for (int k = 1; k < 4; k++)
        if (!((p[k] >= 'A' && p[k] <= 'Z') || (p[k] >= '0' && p[k] <= '9') || p[k] == '_')) return 0;
    return 1;
}

// SARC: the entry whose data holds off (its name copied), and how many entries hold code starts in [first, last]
static void sarc_entry(const uint8_t *b, uint64_t n, uint64_t off, Holder *hd) {
    if (n < 0x20 || memcmp(b, "SARC", 4) || memcmp(b + 0x14, "SFAT", 4)) return;
    uint64_t data = u32(b + 0x0C);
    uint16_t count;
    memcpy(&count, b + 0x1A, 2);
    uint64_t nodes = 0x20, names = nodes + 16ull * count;
    if (names + 8 > n || memcmp(b + names, "SFNT", 4)) return;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = b + nodes + 16ull * i;
        uint64_t s0 = data + u32(e + 8), s1 = data + u32(e + 12);
        if (s1 <= hd->first_off || s0 > hd->last_off) continue;
        hd->inner_count++;
        if (off < s0 || off >= s1 || hd->inner[0]) continue;
        uint32_t attr = u32(e + 4);
        uint64_t at = names + 8 + 4ull * (attr & 0xFFFFFF);
        if (!(attr >> 24) || at >= n) { snprintf(hd->inner, sizeof hd->inner, "(entry %u, no name)", i); continue; }
        size_t k = 0;
        while (k + 1 < sizeof hd->inner && at + k < n && b[at + k] >= 0x20 && b[at + k] < 0x7f) hd->inner[k] = (char)b[at + k], k++;
        hd->inner[k] = 0;
    }
}

// how many files of this tag were mapped before (the first 3 of each tag get a word map)
static int map_samples(uint32_t tag) {
    static uint32_t tags[64];
    static int counts[64], ntags;
    for (int i = 0; i < ntags; i++)
        if (tags[i] == tag) return counts[i]++;
    if (ntags < 64) tags[ntags] = tag, counts[ntags++] = 1;
    return 0;
}

// up to 4 positions, 4-aligned, in [lo, hi) where a u32 equals v (0 when v does not fit 32 bits)
static int find_u32(const uint8_t *b, uint64_t lo, uint64_t hi, uint64_t v, uint64_t *at) {
    int k = 0;
    if (v >> 32) return 0;
    for (uint64_t j = lo; j + 4 <= hi && k < 4; j += 4)
        if (u32(b + j) == (uint32_t)v) at[k++] = j;
    return k;
}

static void measure(const uint8_t *b, uint64_t n, const Prog *pr, uint64_t start, char *out, size_t cap) {
    size_t o = 0;
#define OUT(...) (o += (size_t)snprintf(out + o, o < cap ? cap - o : 0, __VA_ARGS__))
    if (start >= n) { OUT("code start outside the file"); return; }
    uint64_t base = ~0ull;
    for (uint64_t j = start + 1; j-- > 0;)
        if (j + 4 <= n && b[j] == 'B' && !memcmp(b + j, "BNSH", 4)) { base = j; break; }
    const uint8_t *ctrl = pr->code - CONTROL;
    uint32_t c1_off, length;
    memcpy(&c1_off, ctrl + 0x700, 4), memcpy(&length, ctrl + 0x704, 4);
    OUT("stage %u, code %u bytes (length %u, c1 at %u)", pr->stage, pr->len, length, c1_off);
    if (base == ~0ull) { OUT(", no BNSH before the code"); return; }
    OUT(", BNSH at %#llx, code at BNSH+%#llx", (unsigned long long)base, (unsigned long long)(start - base));
    // the control section: exact, else the longest prefix found at a 4-aligned place
    uint64_t cat = ~0ull, best = 0, best_at = 0;
    for (uint64_t j = base; j + 16 <= n; j += 4) {
        if (u64(b + j) != u64(ctrl)) continue;
        uint64_t m = 0;
        while (m < CONTROL && j + m < n && b[j + m] == ctrl[m]) m++;
        if (m == CONTROL) { cat = j; break; }
        if (m > best) best = m, best_at = j;
    }
    if (cat != ~0ull) OUT(", control at BNSH+%#llx (code%+lld)", (unsigned long long)(cat - base), (long long)cat - (long long)start);
    else if (best) OUT(", control: first %llu of 0x880 bytes at BNSH+%#llx", (unsigned long long)best, (unsigned long long)(best_at - base));
    else OUT(", control not in the file");
    uint64_t at[4];
    int k = find_u32(b, base, n, start - base, at);
    OUT("; code offset u32 at");
    for (int i = 0; i < k; i++) OUT(" BNSH+%#llx", (unsigned long long)(at[i] - base));
    if (!k) OUT(" none");
    if (cat != ~0ull) {
        uint64_t ca[4];
        int kc = find_u32(b, base, n, cat - base, ca);
        OUT("; control offset u32 at");
        for (int i = 0; i < kc; i++) OUT(" BNSH+%#llx", (unsigned long long)(ca[i] - base));
        if (!kc) OUT(" none");
    }
    // near the first code offset field: the length, the code size and 0x880
    if (k) {
        uint64_t lo = at[0] >= base + 0x80 ? at[0] - 0x80 : base, hi = at[0] + 0x80 < n ? at[0] + 0x80 : n, w[4];
        const struct { const char *name; uint64_t v; } want[] = {{"length", length}, {"code size", pr->len}, {"0x880", CONTROL}};
        for (int q = 0; q < 3; q++) {
            int kw = find_u32(b, lo, hi, want[q].v, w);
            OUT("; %s at", want[q].name);
            for (int i = 0; i < kw; i++) OUT(" field%+lld", (long long)w[i] - (long long)at[0]);
            if (!kw) OUT(" none");
        }
    }
#undef OUT
}

static void describe(const uint8_t *b, uint64_t n, Holder *hd) {
    for (int i = 0; i < 2; i++)
        if (hd->sample[i]) measure(b, n, &progs[hd->sample[i] - 1], hd->sample_at[i], hd->layout[i], sizeof hd->layout[i]);
    sarc_entry(b, n, hd->first_off, hd);
    hd->bnsh_dist = ~0ull;
    for (uint64_t i = 0; i + 4 <= n; i++)
        if (b[i] == 'B' && !memcmp(b + i, "BNSH", 4)) {
            hd->bnsh_count++;
            if (i <= hd->first_off) hd->bnsh_dist = hd->first_off - i;
        }
    for (uint64_t i = hd->first_off & ~3ull; i >= 4;) {
        i -= 4;
        if (magic_like(b + i)) { hd->near_magic = u32(b + i), hd->near_dist = hd->first_off - i; break; }
    }
    if (map_samples(hd->tag) >= 3) return;
    if (hd->bnsh_dist != ~0ull) {
        uint64_t base = hd->first_off - hd->bnsh_dist;
        size_t o = 0;
        for (int w = 0; w < 24 && base + 4 * w + 4 <= n; w++)
            o += (size_t)snprintf(hd->head + o, sizeof hd->head - o, "%s%#x", w ? " " : "", u32(b + base + 4 * w));
    }
    // up to 24 distinct words, in order of first appearance
    uint32_t w[24], cnt[24];
    uint64_t first[24];
    int nw = 0, more = 0;
    for (uint64_t i = 0; i + 4 <= n; i += 4) {
        if (!magic_like(b + i)) continue;
        uint32_t v = u32(b + i);
        int k = 0;
        while (k < nw && w[k] != v) k++;
        if (k < nw) { cnt[k]++; continue; }
        if (nw == 24) { more++; continue; }
        w[nw] = v, first[nw] = i, cnt[nw++] = 1;
    }
    size_t o = 0;
    for (int k = 0; k < nw && o + 40 < sizeof hd->map; k++)
        o += (size_t)snprintf(hd->map + o, sizeof hd->map - o, "%s%.4s@%#llx x%u", k ? ", " : "", (const char *)&w[k],
                              (unsigned long long)first[k], cnt[k]);
    if (more && o + 24 < sizeof hd->map) snprintf(hd->map + o, sizeof hd->map - o, ", %d more", more);
}

static void search(const uint8_t *b, uint64_t n, uint64_t pack, uint64_t file, int stored) {
    if (n < 64 || !nprogs) return;
    Holder *hd = NULL;
    for (uint64_t i = 0; i + 8 <= n; i++) {
        uint64_t v = u64(b + i), h = mix(v);
        if (!(filter[(h >> 40) >> 3] >> ((h >> 40) & 7) & 1)) continue;
        for (uint64_t s = h & tab_mask; tab[s].key; s = (s + 1) & tab_mask) {
            if (tab[s].key != v) continue;
            Prog *p = &progs[tab[s].prog - 1];
            if (p->found) continue;
            uint64_t a0 = p->at >= 32 ? p->at - 32 : 0, a1 = p->at + 32 <= p->len ? p->at + 32 : p->len;
            if (i < p->at - a0 || i - (p->at - a0) + (a1 - a0) > n || memcmp(b + i - (p->at - a0), p->code + a0, a1 - a0)) continue;
            p->found = 1;
            uint64_t start = i - p->at;   // the code's start in the file
            if (!hd) {
                if (nholders == (int)(sizeof holders / sizeof *holders)) return;
                hd = &holders[nholders++];
                *hd = (Holder){pack, file, n, start, start, 0, u32(b), stored};
            }
            if (hd->hits < 2) hd->sample[hd->hits] = tab[s].prog, hd->sample_at[hd->hits] = start;
            hd->hits++;
            if (start < hd->first_off) hd->first_off = start;
            if (start > hd->last_off) hd->last_off = start;
        }
    }
    if (hd) describe(b, n, hd);
}

static int printable(uint32_t tag) {
    for (int k = 0; k < 4; k++) {
        uint8_t c = (uint8_t)(tag >> (8 * k));
        if (c < 0x20 || c > 0x7e) return 0;
    }
    return 1;
}

// Reads n bytes at off of a RomFS file into a new buffer
static uint8_t *read_file(uint64_t h, uint64_t off, uint64_t n) {
    uint8_t *b = malloc(n ? n : 1);
    uint64_t got = 0;
    if (b && (romfs_read(&got, h, (int64_t)off, b, n) != 0 || got != n)) { free(b); b = NULL; }
    return b;
}

void shader_census_run(Cpu *c, uint64_t decoder, const uint64_t a[8]) {
    static int ran;
    if (__atomic_exchange_n(&ran, 1, __ATOMIC_ACQ_REL)) return;
    uint64_t t0 = plat_now_ns();
    rt_log("shader census: start (this run ends when it is done)");
    load_programs();
    extract_begin();
    // a guest-visible buffer: reserved at a free address below RT_ADDRESS_LIMIT
    uint64_t gbuf = 0;
    for (uint64_t at = 0x7E00000000ull; at >= 0x4000000000ull; at -= 0x200000000ull)
        if (plat_mem_reserve(at, BUF_SIZE, 0) && plat_mem_commit(at, BUF_SIZE)) { gbuf = at; break; }
    if (!gbuf) { rt_log("shader census: no buffer below the guest address limit"); exit(2); }

    uint64_t hd_, hs_;
    if (romfs_open(&hd_, "data:/arc/data.trpfd", FS_OPEN_READ) || romfs_open(&hs_, "data:/arc/data.trpfs", FS_OPEN_READ)) {
        rt_log("shader census: cannot open arc/data.trpfd / data.trpfs");
        exit(2);
    }
    int64_t fd_size = 0, fs_size = 0;
    romfs_size(&fd_size, hd_), romfs_size(&fs_size, hs_);
    uint8_t *fdb = read_file(hd_, 0, (uint64_t)fd_size);
    uint8_t head[16];
    uint64_t got = 0;
    romfs_read(&got, hs_, 0, head, 16);
    uint64_t root_at = u64(head + 8);
    uint8_t *fsb = fdb ? read_file(hs_, root_at, (uint64_t)fs_size - root_at) : NULL;
    Fb fd, fs;
    uint64_t names_v, names_n, info_v, info_n, ph_v, ph_n, po_v, po_n;
    if (!fdb || !fsb || !fb_root(&fd, fdb, (uint64_t)fd_size, 0) || !fb_root(&fs, fsb, (uint64_t)fs_size - root_at, 0) ||
        !fb_vec(&fd, 1, &names_v, &names_n) || !fb_vec(&fd, 3, &info_v, &info_n) || !fb_vec(&fs, 0, &ph_v, &ph_n) ||
        !fb_vec(&fs, 1, &po_v, &po_n) || ph_n != po_n) {
        rt_log("shader census: the archive's tables do not read as expected");
        exit(2);
    }
    rt_log("shader census: %llu packs, data.trpfs %.2f GiB; guest buffer at %#llx", (unsigned long long)names_n,
           fs_size / 1073741824.0, (unsigned long long)gbuf);

    uint64_t decodes = 0, failed = 0, stored_files = 0, too_big = 0, out_total = 0;
    for (uint64_t p = 0; p < names_n; p++) {
        uint32_t nl = 0;
        const char *nm = fb_string(&fd, names_v, p, &nl);
        Fb info;
        if (!nm || p >= info_n || !fb_table(&fd, info_v, p, &info)) continue;
        uint64_t psize = fb_u64(&info, 0), h = fnv1a64(nm, nl), poff = 0;
        int have = 0;
        for (uint64_t k = 0; k < ph_n; k++)
            if (u64(fsb + ph_v + 8 * k) == h) { poff = u64(fsb + po_v + 8 * k), have = 1; break; }
        if (!have || !psize || psize > BUF_SIZE) continue;
        uint8_t *pk = read_file(hs_, poff, psize);
        Fb pt;
        uint64_t fv, fn;
        if (!pk || !fb_root(&pt, pk, psize, 0) || !fb_vec(&pt, 1, &fv, &fn)) { free(pk); continue; }
        for (uint64_t k = 0; k < fn; k++) {
            Fb e;
            uint64_t ds, dn;
            if (!fb_table(&pt, fv, k, &e) || !fb_vec(&e, e.nslots - 1, &ds, &dn)) continue;
            int64_t comp = fb_i8(&e, 1, -1);
            uint64_t unpacked = fb_u64(&e, 3);
            if (comp == -1) { stored_files++; search(pk + ds, dn, p, k, 1); extract(pk + ds, dn); continue; }
            if (!unpacked || dn + unpacked > BUF_SIZE) { too_big++; continue; }
            uint8_t *in = (uint8_t *)(uintptr_t)gbuf, *outp = in + ((dn + 4095) & ~4095ull);
            memcpy(in, pk + ds, dn);
            uint64_t r = rt_call(c, decoder, 8, (uint64_t)(uintptr_t)in, dn, (uint64_t)(uintptr_t)outp, unpacked,
                                 a[4], a[5], a[6], a[7]);
            decodes++;
            if (r != unpacked) { failed++; continue; }
            out_total += r;
            search(outp, r, p, k, 0);
            extract(outp, r);
        }
        free(pk);
        if (!(p % 500))
            rt_log("shader census: %llu of %llu packs, %llu decodes (%.1f GiB), %d holders, %.0f s", (unsigned long long)p,
                   (unsigned long long)names_n, (unsigned long long)decodes, out_total / 1073741824.0, nholders,
                   (plat_now_ns() - t0) / 1e9);
    }

    // ---- the report
    char *rep = malloc(4 << 20);
    size_t o = 0;
#define PUT(...) (o += (size_t)snprintf(rep + o, o < (4u << 20) ? (4u << 20) - o : 0, __VA_ARGS__))
    int found = 0, by_stage[8] = {0}, of_stage[8] = {0};
    for (int i = 0; i < nprogs; i++) {
        if (progs[i].stage < 8) of_stage[progs[i].stage]++;
        if (progs[i].found == 1) { found++; if (progs[i].stage < 8) by_stage[progs[i].stage]++; }
    }
    PUT("========================= KarPur shader census =========================\n");
    PUT("packs %llu; Oodle decodes %llu (%.2f GiB unpacked), failed %llu, too big %llu; stored files searched %llu; %.0f s\n",
        (unsigned long long)names_n, (unsigned long long)decodes, out_total / 1073741824.0, (unsigned long long)failed,
        (unsigned long long)too_big, (unsigned long long)stored_files, (plat_now_ns() - t0) / 1e9);
    PUT("recorded programs found: %d of %d (stage 0: %d of %d, 4: %d of %d, 5: %d of %d)\n", found, nprogs, by_stage[0],
        of_stage[0], by_stage[4], of_stage[4], by_stage[5], of_stage[5]);
    PUT("files holding programs: %d\n", nholders);
    // sums: by the file's tag, by the pack's type (its name's last ending before .trpak), by the SARC entry's ending
    struct Sum { char key[24]; int files, progs, bnsh; };
    static struct Sum sums[3][64];
    int nsum[3] = {0};
    for (int i = 0; i < nholders; i++) {
        Holder *hh = &holders[i];
        char keys[3][24] = {"----", "?", ""};
        if (printable(hh->tag)) memcpy(keys[0], &hh->tag, 4), keys[0][4] = 0;
        uint32_t nl = 0;
        const char *nm = fb_string(&fd, names_v, hh->pack, &nl);
        if (nm) {
            uint32_t e = nl;
            if (e > 6 && !memcmp(nm + e - 6, ".trpak", 6)) e -= 6;
            uint32_t d = e;
            while (d && nm[d - 1] != '.' && nm[d - 1] != '/') d--;
            snprintf(keys[1], sizeof keys[1], "%.*s", (int)(e - d), nm + d);
        }
        if (hh->inner[0]) {
            const char *dot = strrchr(hh->inner, '.');
            snprintf(keys[2], sizeof keys[2], "%s", dot ? dot : "(no ending)");
        }
        for (int s = 0; s < 3; s++) {
            if (!keys[s][0]) continue;
            int k = 0;
            while (k < nsum[s] && strcmp(sums[s][k].key, keys[s])) k++;
            if (k == nsum[s]) {
                if (k == 64) continue;
                memset(&sums[s][k], 0, sizeof sums[s][k]);
                strcpy(sums[s][k].key, keys[s]), nsum[s]++;
            }
            sums[s][k].files++, sums[s][k].progs += (int)hh->hits, sums[s][k].bnsh += hh->bnsh_count > 0;
        }
    }
    static const char *const sum_name[3] = {"by file tag", "by pack type", "by SARC entry type"};
    for (int s = 0; s < 3; s++) {
        PUT("%s:", sum_name[s]);
        for (int k = 0; k < nsum[s]; k++)
            PUT("%s %s %d files / %d programs (%d with BNSH)", k ? ";" : "", sums[s][k].key, sums[s][k].files,
                sums[s][k].progs, sums[s][k].bnsh);
        PUT("\n");
    }
    PUT("all holders (pack, file, programs, size, packing, tag, code starts, SARC entry, BNSH count and distance, nearest word before code):\n");
    for (int i = 0; i < nholders; i++) {
        Holder *hh = &holders[i];
        uint32_t nl = 0;
        const char *nm = fb_string(&fd, names_v, hh->pack, &nl);
        char tag[5] = "----", near[5] = "----", bn[48] = "none";
        if (printable(hh->tag)) memcpy(tag, &hh->tag, 4);
        if (hh->near_magic) memcpy(near, &hh->near_magic, 4);
        if (hh->bnsh_count && hh->bnsh_dist == ~0ull) snprintf(bn, sizeof bn, "%u, none before", hh->bnsh_count);
        else if (hh->bnsh_count) snprintf(bn, sizeof bn, "%u, -%#llx", hh->bnsh_count, (unsigned long long)hh->bnsh_dist);
        PUT("  %-60.*s file %4llu: %5u, %9llu, %s, %s, %#llx..%#llx, %s%s, BNSH %s, %s -%#llx\n", (int)(nl < 60 ? nl : 60),
            nm ? nm : "?", (unsigned long long)hh->file, hh->hits, (unsigned long long)hh->size,
            hh->stored ? "stored" : "Oodle", tag, (unsigned long long)hh->first_off, (unsigned long long)hh->last_off,
            hh->inner[0] ? hh->inner : "-", hh->inner_count > 1 ? " +more" : "", bn, near,
            (unsigned long long)hh->near_dist);
        if (hh->map[0]) PUT("      words: %s\n", hh->map);
        if (hh->head[0]) PUT("      BNSH words: %s\n", hh->head);
        for (int k = 0; k < 2; k++)
            if (hh->layout[k][0]) PUT("      program %d: %s\n", k + 1, hh->layout[k]);
    }
    out_flush();
    if (out_f) fclose(out_f);
    if (ans_f) fclose(ans_f);
    PUT("extraction: %llu BNSH, %llu shader infos, %llu distinct programs (vertex %llu, tess %llu/%llu, geometry %llu, "
        "fragment %llu, compute %llu), %.1f MiB in seed-programs.bin (+ seed-answers.bin); %llu left out (no shader "
        "program header and no sane compute local size); hash failures %llu\n",
        (unsigned long long)n_bnsh, (unsigned long long)n_triples, (unsigned long long)n_out,
        (unsigned long long)n_out_stage[0], (unsigned long long)n_out_stage[1], (unsigned long long)n_out_stage[2],
        (unsigned long long)n_out_stage[3], (unsigned long long)n_out_stage[4], (unsigned long long)n_out_stage[5],
        out_bytes / 1048576.0, (unsigned long long)n_not_shader, (unsigned long long)n_hash_fail);
    PUT("extraction vs recorded: %llu of %d recorded programs reproduced exactly; %llu more only under another stage;"
        " stage (recorded > guessed):", (unsigned long long)n_reproduced, nprogs, (unsigned long long)n_stage_wrong);
    for (int r = 0; r < 6; r++)
        for (int q = 0; q < 6; q++)
            if (conf[r][q]) PUT(" %d>%d %llu", r, q, (unsigned long long)conf[r][q]);
    PUT("\n");
    PUT("programs not found:");
    int nf = 0;
    for (int i = 0; i < nprogs; i++)
        if (progs[i].found != 1)
            PUT("%s #%d stage %u, %u bytes%s", nf++ ? ";" : "", i, progs[i].stage, progs[i].len,
                progs[i].found < 0 ? " (too short or no anchor)" : "");
    PUT("%s\n", nf ? "" : " none");
    PUT("==========================================================================\n");
    rt_log("%s", rep);
    plat_file_mkdir_p("game/report");
    plat_file_write("game/report/shader-census.txt", rep, o);
    rt_log("shader census: report in game/report/shader-census.txt; the run ends here");
    exit(0);
}
