// KarPur: the player's current team for the launcher's Home page (TOTK_TEAM, default on; TOTK_TEAM_PROBE=1 reports).
//
// Scarlet/Violet's save file is encrypted with a static pad from the game, which cannot ship with KarPur. The game's
// memory holds the Pokémon themselves, though, in the console's long-known Pokémon data format (PK9): 0x148 stored bytes
// plus 0x10 bytes of battle stats in the party, encrypted with a linear congruential generator seeded by the Pokémon's
// own encryption constant (EC, its first u32) and its four 0x50-byte blocks shuffled by ((EC >> 13) & 31) % 24. The format
// carries its own check: a u16 checksum of the decrypted bytes 0x08..0x148. No key and nothing from the game is
// needed to read it; this file holds the published algorithm only.
//
// When the game commits its save, a background thread scans the game's heap and alias region for such records. The
// party is a run of up to six checksummed records at a fixed stride whose battle stats hold a level of 1-100 and a
// current HP no higher than the maximum, outside the boxes' runs of 30. The result goes to <profile>/team.json: per
// Pokémon its national species number as stored, form, level, shiny flag and nickname (the player's own text). The
// first scan remembers where the party was; later saves read there first.
//
// TOTK_TEAM_PROBE=1 logs what a scan found (counts, strides, runs, levels; no names) to plan and check the heuristics.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "host.h"
#include "platform/platform.h"

#define PK_STORED 0x148
#define PK_PARTY 0x158
#define BLOCK 0x50

int mem_info(uint32_t id, uint64_t *out);   // memory.c

static int team_on(void) {
    static int v = -1;
    if (v < 0) v = rt_knob("TEAM", "1");
    return v;
}
static int team_probe(void) {
    static int v = -1;
    if (v < 0) v = rt_knob("TEAM_PROBE", "0");
    return v;
}

static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

// The block order for each shuffle value: position of blocks A, B, C, D in the stored data (Gen 8/9 table).
static const uint8_t ORDER[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

// Decrypts an encrypted party record into out (PK_PARTY bytes, blocks in A B C D order); 1 when its checksum holds.
static int pk_decrypt(const uint8_t *in, uint8_t *out) {
    uint32_t ec = rd32(in), seed = ec;
    if (!ec || rd16(in + 4)) return 0;   // EC set, the sanity word zero
    uint8_t tmp[PK_PARTY];
    memcpy(tmp, in, PK_PARTY);
    for (int o = 8; o < PK_PARTY; o += 2) {   // one stream over the stored part and the party stats
        seed = seed * 0x41C64E6Du + 0x6073u;
        uint16_t w = rd16(tmp + o) ^ (uint16_t)(seed >> 16);
        memcpy(tmp + o, &w, 2);
    }
    // the party stats are encrypted with a stream seeded afresh by the EC (Gen 5+ convention)
    seed = ec;
    for (int o = PK_STORED; o < PK_PARTY; o += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        uint16_t w = rd16(in + o) ^ (uint16_t)(seed >> 16);
        memcpy(tmp + o, &w, 2);
    }
    const uint8_t *ord = ORDER[((ec >> 13) & 31) % 24];
    memcpy(out, tmp, 8);
    for (int b = 0; b < 4; b++) memcpy(out + 8 + b * BLOCK, tmp + 8 + ord[b] * BLOCK, BLOCK);
    memcpy(out + PK_STORED, tmp + PK_STORED, PK_PARTY - PK_STORED);
    uint16_t sum = 0;
    for (int o = 8; o < PK_STORED; o += 2) sum += rd16(out + o);
    return sum == rd16(in + 6) && rd16(out + 8) != 0;   // and a species
}

// ---- levels: EXP and the growth curves the shader census took from the player's personal table (growth.bin)
static uint8_t curves[1101];
static int curves_loaded;

static void load_curves(void) {
    if (curves_loaded) return;
    curves_loaded = 1;
    memset(curves, 255, sizeof curves);
    char path[1024];
    int n = plat_exe_path(path, sizeof path - 32);
    char *cut = n ? plat_last_sep(path) : NULL;
    if (!cut) return;
    strcpy(cut + 1, "growth.bin");
    uint64_t size = 0;
    uint8_t *b = plat_file_read(path, &size);
    if (b && size == 8 + sizeof curves && !memcmp(b, "KPGROW1", 8)) memcpy(curves, b + 8, sizeof curves);
    free(b);
}

// The EXP a Pokemon needs for level n on each curve (the series' published formulas)
static uint64_t exp_for(int curve, uint64_t n) {
    if (n <= 1) return 0;
    uint64_t c = n * n * n;
    switch (curve) {
    case 0: return c;                                                                          // medium fast
    case 1: return n < 50 ? c * (100 - n) / 50 : n < 68 ? c * (150 - n) / 100                  // erratic
                 : n < 98 ? c * ((1911 - 10 * n) / 3) / 500 : c * (160 - n) / 100;
    case 2: return n < 15 ? c * ((n + 1) / 3 + 24) / 50 : n < 36 ? c * (n + 14) / 50          // fluctuating
                 : c * (n / 2 + 32) / 50;
    case 3: return 6 * c / 5 + 100 * n - 15 * n * n - 140;                                     // medium slow
    case 4: return 4 * c / 5;                                                                  // fast
    default: return 5 * c / 4;                                                                 // slow
    }
}

static uint8_t level_of(uint16_t species, uint32_t exp) {
    load_curves();
    if (species >= sizeof curves || curves[species] > 5) return 0;
    int lv = 1;
    while (lv < 100 && exp_for(curves[species], (uint64_t)lv + 1) <= exp) lv++;
    return (uint8_t)lv;
}

// The fields the team file names (offsets in the decrypted record; PKHeX's published PK9 layout).
typedef struct { uint16_t species, form; uint8_t level, stat_level; int shiny; uint16_t hp, max_hp; uint32_t exp; char nick[64]; } Mon;

static void pk_fields(const uint8_t *d, Mon *m) {
    m->species = rd16(d + 0x08);
    m->exp = rd32(d + 0x10);
    m->form = d[0x24];
    uint32_t pid = rd32(d + 0x1C), tid = rd32(d + 0x0C);
    uint32_t x = (pid >> 16) ^ (pid & 0xFFFF) ^ (tid >> 16) ^ (tid & 0xFFFF);
    m->shiny = x < 16;
    m->level = level_of(m->species, m->exp);   // not in a stored record: from the EXP and the species' growth curve
    m->stat_level = d[0x148];
    m->hp = rd16(d + 0x8A), m->max_hp = rd16(d + 0x14A);   // current HP in the stored part, the maximum in the party stats
    // nickname: UTF-16 at 0x58, 13 characters, to UTF-8 (BMP only)
    size_t o = 0;
    for (int i = 0; i < 13 && o + 4 < sizeof m->nick; i++) {
        uint16_t c = rd16(d + 0x58 + 2 * i);
        if (!c) break;
        if (c < 0x80) m->nick[o++] = (char)c;
        else if (c < 0x800) m->nick[o++] = (char)(0xC0 | c >> 6), m->nick[o++] = (char)(0x80 | (c & 0x3F));
        else m->nick[o++] = (char)(0xE0 | c >> 12), m->nick[o++] = (char)(0x80 | ((c >> 6) & 0x3F)), m->nick[o++] = (char)(0x80 | (c & 0x3F));
    }
    m->nick[o] = 0;
}

static int party_like(const Mon *m) { return m->stat_level >= 1 && m->stat_level <= 100 && m->max_hp && m->hp <= m->max_hp; }

// A player's Pokemon as the party slots hold it (test CT): a stored record (no battle stats) with a species, EXP up to the
// slowest curve's level 100, current HP below 1000 and a name of 1-12 characters (UTF-8: up to 36 bytes).
#define SLOT 0x150   // the party's slot stride: 0x148 stored bytes, padded
static int member_like(const Mon *m) {
    size_t n = strlen(m->nick);
    return m->species >= 1 && m->species <= 1025 && m->exp <= 1640000 && m->hp < 1000 && n >= 1 && n <= 36;
}

// ---- the scan
typedef struct { uint64_t addr; Mon m; int plain; } Hit;
static Hit *hits;
static size_t nhits, caphits;
static uint64_t party_addr;    // where the party was last found
static uint32_t party_stride;

// The LCRNG advanced by n steps: seed' = a_n * seed + c_n (for the jump to block A's first word)
static void lcrng_jump(uint32_t n, uint32_t *a, uint32_t *c) {
    uint32_t A = 1, C = 0;
    for (uint32_t k = 0; k < n; k++) A *= 0x41C64E6Du, C = C * 0x41C64E6Du + 0x6073u;
    *a = A, *c = C;
}
static uint32_t jump_a[4], jump_c[4];   // to the first word of stored block 0..3 (1 + 40 * k steps)
static uint64_t scanned, tested;

// Cheap test before the full decryption: block A's species word, decrypted alone, is a species (1..1100).
static int pk_maybe(const uint8_t *p) {
    uint32_t ec = rd32(p);
    int k = ORDER[((ec >> 13) & 31) % 24][0];
    uint32_t seed = jump_a[k] * ec + jump_c[k];
    uint16_t species = rd16(p + 8 + k * BLOCK) ^ (uint16_t)(seed >> 16);
    return species && species <= 1100;
}

static void scan_range(uint64_t lo, uint64_t hi) {
    uint8_t dec[PK_PARTY];
    uint64_t base, size;
    for (uint64_t at = lo; at < hi && plat_mem_next_readable(at, hi, &base, &size); at = base + size) {
        scanned += size;
        uint64_t end = base + size;
        for (uint64_t a = base; a + PK_PARTY <= end; a += 4) {
            const uint8_t *p = (const uint8_t *)(uintptr_t)a;
            if (rd16(p + 4) || !rd32(p) || !rd16(p + 6)) continue;
            int plain = 0;
            if (!pk_maybe(p) || (tested++, !pk_decrypt(p, dec))) continue;
            if (nhits == caphits) {
                caphits = caphits ? caphits * 2 : 1024;
                Hit *n = realloc(hits, caphits * sizeof *hits);
                if (!n) return;
                hits = n;
            }
            hits[nhits].addr = a;
            hits[nhits].plain = plain;
            pk_fields(dec, &hits[nhits].m);
            nhits++;
            a += PK_STORED - 4;   // records do not overlap
        }
    }
}

static int hit_cmp(const void *x, const void *y) {
    uint64_t a = ((const Hit *)x)->addr, b = ((const Hit *)y)->addr;
    return a < b ? -1 : a > b;
}

// Reads up to six records at addr with stride; returns how many decrypt and look like party members.
static int read_party(uint64_t addr, uint32_t stride, Mon *out) {
    uint8_t dec[PK_PARTY];
    int n = 0;
    for (int i = 0; i < 6; i++) {
        uint64_t a = addr + (uint64_t)i * stride;
        if (!plat_mem_readable(a, PK_PARTY)) break;
        if (!pk_decrypt((const uint8_t *)(uintptr_t)a, dec)) break;
        pk_fields(dec, &out[n]);
        if (!member_like(&out[n])) break;
        n++;
    }
    return n;
}

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s < 0x20) continue;
        fputc(*s, f);
    }
    fputc('"', f);
}

static void write_team(const Mon *m, int n) {
    char path[1200];
    if (!fs_profile_dir(path, sizeof path - 16)) return;
    strcat(path, "/team.json");
    char tmp[1220];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    fprintf(f, "{\"version\": 1, \"time\": %llu, \"team\": [", (unsigned long long)(plat_unix_time_ns() / 1000000000ull));
    for (int i = 0; i < n; i++) {
        fprintf(f, "%s\n  {\"species\": %u, \"form\": %u, \"level\": %u, \"exp\": %u, \"hp\": %u, \"shiny\": %s, \"nickname\": ",
                i ? "," : "", m[i].species, m[i].form, m[i].level, m[i].exp, m[i].hp, m[i].shiny ? "true" : "false");
        json_str(f, m[i].nick);
        fputc('}', f);
    }
    fprintf(f, "\n]}\n");
    fclose(f);
    remove(path);
    rename(tmp, path);
}

// Probe: every record with party stats (species, level), and arrays in memory of two or more consecutive u64 that
// point at such records or up to 0x100 bytes before one (an object holding the record): the party may be a list of
// pointers to separate objects.
static void probe_pointers(uint64_t hb, uint64_t hs, uint64_t ab, uint64_t as) {
    uint64_t pa[64];
    int np = 0;
    for (size_t i = 0; i < nhits && np < 64; i++) {   // every record: the party's members are matched by species and HP
        pa[np++] = hits[i].addr;
        uint32_t exp;
        memcpy(&exp, (const void *)(uintptr_t)hits[i].addr, 4);   // (the EC; the decrypted EXP is logged below)
        rt_log("team: record %d at %#llx (%s, shuffle %u): species %u, HP %u, exp %u, party stats %s (level %u, max HP %u), name %zu chars",
               np - 1, (unsigned long long)hits[i].addr, hits[i].plain ? "plain" : "encrypted",
               (rd32((const uint8_t *)(uintptr_t)hits[i].addr) >> 13) & 31, hits[i].m.species, hits[i].m.hp, hits[i].m.exp,
               party_like(&hits[i].m) ? "plausible" : "no", hits[i].m.stat_level, hits[i].m.max_hp, strlen(hits[i].m.nick));
        (void)exp;
    }
    if (!np) return;
    // the slots next to each record (stride 0x150) that did not check: what they hold (numbers only)
    int nb = 0;
    for (size_t i = 0; i < nhits && nb < 240; i++) {
        // only around records that look like a player's Pokemon (the log keeps a limited number of lines per message)
        const Mon *mm = &hits[i].m;
        size_t nl = strlen(mm->nick);
        if (!mm->hp || mm->exp > 1640000 || !nl || nl > 36) continue;
        for (int k = -3; k <= 3 && nb < 240; k++) {
            if (!k) continue;
            uint64_t a = hits[i].addr + (int64_t)k * 0x150;
            int known = 0;
            for (size_t j = 0; j < nhits && !known; j++) known = hits[j].addr == a;
            if (known || !plat_mem_readable(a, PK_PARTY)) continue;
            const uint8_t *q = (const uint8_t *)(uintptr_t)a;
            uint32_t ec = rd32(q), seed = ec;
            uint8_t tmp[PK_STORED];
            memcpy(tmp, q, PK_STORED);
            uint16_t esum = 0, psum = 0;
            for (int o = 8; o < PK_STORED; o += 2) {
                seed = seed * 0x41C64E6Du + 0x6073u;
                esum += rd16(q + o) ^ (uint16_t)(seed >> 16);
                psum += rd16(q + o);
            }
            int ord0 = ORDER[((ec >> 13) & 31) % 24][0];
            seed = ec;
            for (int s2 = 0; s2 <= ord0 * 40; s2++) seed = seed * 0x41C64E6Du + 0x6073u;
            uint16_t sp_e = rd16(q + 8 + ord0 * BLOCK) ^ (uint16_t)(seed >> 16);
            rt_log("team: next to record %zu, slot %+d at %#llx: EC %#x, word4 %#x, checksum %#x, sum decrypted %#x, sum plain %#x, "
                   "species decrypted %u, plain %u", i, k, (unsigned long long)a, ec, rd16(q + 4), rd16(q + 6), esum, psum, sp_e,
                   rd16(q + 8));
            nb++;
            if (esum == rd16(q + 6)) {   // a valid encrypted record: each stored block read as A (species, EXP) and as B (HP)
                uint8_t d[PK_STORED];
                memcpy(d, q, PK_STORED);
                seed = ec;
                for (int o = 8; o < PK_STORED; o += 2) {
                    seed = seed * 0x41C64E6Du + 0x6073u;
                    uint16_t w = rd16(d + o) ^ (uint16_t)(seed >> 16);
                    memcpy(d + o, &w, 2);
                }
                for (int b = 0; b < 4; b++) {
                    const uint8_t *blk = d + 8 + b * BLOCK;
                    int nl = 0;
                    while (nl < 13 && rd16(blk + 2 * nl)) nl++;
                    rt_log("team:   EC %#x shuffle %u: stored block %d as A: species %u, exp %u; as B: HP %u, name %d chars", ec,
                           (ec >> 13) & 31, b, rd16(blk), rd32(blk + 8), rd16(blk + 0x32), nl);
                }
            }
            nb++;
        }
    }
    uint64_t ranges[2][2] = {{hb, hb + hs}, {ab, ab + as}};
    int logged = 0;
    for (int r = 0; r < 2; r++) {
        uint64_t base, size;
        for (uint64_t at = ranges[r][0]; at < ranges[r][1] && plat_mem_next_readable(at, ranges[r][1], &base, &size); at = base + size) {
            int run = 0;
            uint64_t run_at = 0;
            char what[256];
            size_t wo = 0;
            for (uint64_t a = base; a + 8 <= base + size; a += 8) {
                uint64_t v;
                memcpy(&v, (const void *)(uintptr_t)a, 8);
                int idx = -1;
                uint64_t delta = 0;
                for (int k = 0; k < np && idx < 0; k++)
                    if (v <= pa[k] && pa[k] - v <= 0x800) idx = k, delta = pa[k] - v;
                if (idx >= 0) {
                    if (!run) run_at = a, wo = 0;
                    run++;
                    if (wo < sizeof what - 16) wo += (size_t)snprintf(what + wo, sizeof what - wo, " %d+%#llx", idx, (unsigned long long)delta);
                    continue;
                }
                if (run >= 2 && logged++ < 60)
                    rt_log("team: %d pointers at %#llx to records:%s", run, (unsigned long long)run_at, what);
                run = 0;
            }
        }
    }
    rt_log("team: pointer probe done (%d arrays logged)", logged);
}

static void scan_main(void *arg) {
    (void)arg;
    plat_thread_priority(PLAT_PRIO_BELOW_NORMAL);
    Mon team[6];
    if (party_addr) {   // where it was: a cheap read first
        int n = read_party(party_addr, party_stride, team);
        if (n) { write_team(team, n); if (team_probe()) rt_log("team: %d party members at the known place", n); return; }
    }
    uint64_t t0 = plat_now_ns();
    nhits = 0, scanned = 0, tested = 0;
    for (int k = 0; k < 4; k++) lcrng_jump(1 + 40 * k, &jump_a[k], &jump_c[k]);
    uint64_t hb = 0, hs = 0, ab = 0, as = 0;
    mem_info(4, &hb), mem_info(5, &hs), mem_info(2, &ab), mem_info(3, &as);
    rt_log("team: the game saved; looking for the party (heap %.1f GiB, alias region %.1f GiB reserved)", hs / 1073741824.0,
           as / 1073741824.0);
    if (hs) scan_range(hb, hb + hs);
    if (as) scan_range(ab, ab + as);
    qsort(hits, nhits, sizeof *hits, hit_cmp);
    // runs: records at one stride after another
    int best_len = 0;
    uint64_t best_addr = 0;
    uint32_t best_stride = 0;
    size_t runs = 0, box_like = 0;
    for (size_t i = 0; i < nhits;) {
        size_t j = i + 1;
        uint64_t stride = j < nhits ? hits[j].addr - hits[i].addr : 0;
        while (j < nhits && hits[j].addr - hits[j - 1].addr == stride && stride <= 0x400) j++;
        int len = (int)(j - i);
        if (len == 1) stride = 0;
        runs++;
        if (len > 6) box_like++;
        int party = stride == SLOT || len == 1;
        for (size_t k = i; k < j; k++) party &= member_like(&hits[k].m);
        static int logged;
        if (team_probe() && (len > 1 || party) && logged++ < 120)
            rt_log("team: run at %#llx: %d records, stride %#llx, party-like %s", (unsigned long long)hits[i].addr,
                   len, (unsigned long long)stride, party ? "yes" : "no");
        if (party && len <= 6 && len > best_len) best_len = len, best_addr = hits[i].addr, best_stride = (uint32_t)stride;
        i = j;
    }
    rt_log("team: scan of %.2f GiB found %zu Pokemon records (%llu fully checked) in %zu runs (%zu longer than 6) in %.1f s; "
           "party: %d at %#llx stride %#x", scanned / 1073741824.0, nhits, (unsigned long long)tested, runs, box_like,
           (plat_now_ns() - t0) / 1e9, best_len, (unsigned long long)best_addr, best_stride);
    if (team_probe()) probe_pointers(hb, hs, ab, as);
    if (!best_len) return;
    party_addr = best_addr, party_stride = SLOT;
    int n = read_party(party_addr, party_stride, team);
    if (n) write_team(team, n);
}

// fs_host.c: the game committed its save. The scan runs on a thread of its own, one at a time.
static volatile int32_t busy;

static void team_thread(void *arg) {
    scan_main(arg);
    plat_xchg32(&busy, 0);
}

void team_on_save(void) {
    if (!team_on() || plat_cas32(&busy, 1, 0) != 0) return;   // (p, new, expected), returns the old value: 0 = ours
    if (!plat_thread_spawn(team_thread, NULL, "team", 0)) plat_xchg32(&busy, 0);
}
