// KarPur: value scan and poke, for finding the game's frame step (the 60 fps work).
//
// Violet steps its world by a fixed amount per frame: paced at 60 fps (TOTK_PRESENT_INTERVAL=1) it runs twice as fast
// (docs/progress.md 2026-11-02). If that step is a float kept in memory (1/30 s, or 30 per second, or 2 vsyncs), a
// scan finds where it lives and a poke tests a candidate.
//
// TOTK_VALUE_SCAN=1: at present 900 and again at present 1200 (in play, after loading), every writable page of the
// modules' data, the heap and the mapped (alias) memory is searched for the 32-bit floats 1/30 and 30.0 and the 64-bit
// float 1/30. The log gets the count per kind and region and the first hits as module offsets or guest addresses; the
// second scan says which hits of the first one still hold the value (a variable that keeps it, rather than a
// temporary). Reports addresses only, never memory contents.
//
// TOTK_POKE_SCAN=KIND:LO-HI[,KIND:LO-HI...] (needs TOTK_VALUE_SCAN=1): from the first scan on, the hits LO..HI (scan
// order, 0-based) of KIND get the 60 fps value: f32 (float 1/30) and d64 (double 1/30) get 1/60, f30 (float 30.0)
// gets 60.0. A thread writes them every millisecond, so a step the game recomputes each frame is mostly overwritten
// before it is read. With TOTK_PRESENT_INTERVAL=1: normal game speed means the step is among those hits (then halve).
//
// TOTK_POKE=ADDR=VALUE[,ADDR=VALUE...]: before each present the 32-bit word at ADDR gets VALUE. ADDR is main+0x...,
// subsdk0+0x... or a 0x... guest address; VALUE is f:<float>, d:<double> (64-bit) or i:<integer>. Writes only to pages
// that are writable guest memory.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

enum { KIND_F32_30TH, KIND_F32_30, KIND_F64_30TH, KIND_N };
static const char *const kind_name[KIND_N] = {"float 1/30", "float 30.0", "double 1/30"};
static const uint32_t f32_30th = 0x3D088889u, f32_30 = 0x41F00000u;
static const uint64_t f64_30th = 0x3FA1111111111111ull;

enum { REG_MODULE, REG_HEAP, REG_ALIAS, REG_N };
static const char *const reg_name[REG_N] = {"module data", "heap", "mapped memory"};

#define SHOW 24            // hits logged per kind
static uint64_t *hits[KIND_N];   // every hit of the first scan, in scan order
static int nhits[KIND_N], chits[KIND_N];
static uint64_t counts[KIND_N][REG_N];

static void name_of(uint64_t a, char *buf, size_t n) {
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        if (a - m->base < m->size) {
            snprintf(buf, n, "%s+%#llx", m->name, (unsigned long long)(a - m->base));
            return;
        }
    }
    snprintf(buf, n, "%#llx", (unsigned long long)a);
}

static void scan_range(uint64_t lo, uint64_t hi, int reg) {
    for (uint64_t a = lo; a < hi;) {
        RtMemoryInfo mi;
        mem_query(a, &mi);
        uint64_t e = mi.base + mi.size < hi ? mi.base + mi.size : hi;
        if (e <= a) break;
        if ((mi.perm & 3) == 3 && plat_mem_readable(a, e - a)) {
            const uint32_t *w = (const uint32_t *)(uintptr_t)a;
            uint64_t n = (e - a) / 4;
            for (uint64_t i = 0; i < n; i++) {
                int k = -1;
                if (w[i] == f32_30th) k = KIND_F32_30TH;
                else if (w[i] == f32_30) k = KIND_F32_30;
                else if (!(i & 1) && i + 1 < n && ((uint64_t)w[i + 1] << 32 | w[i]) == f64_30th) k = KIND_F64_30TH;
                if (k < 0) continue;
                counts[k][reg]++;
                if (nhits[k] == chits[k]) {
                    chits[k] = chits[k] ? 2 * chits[k] : 1024;
                    if (!(hits[k] = realloc(hits[k], sizeof *hits[k] * (size_t)chits[k]))) rt_fatal("value scan: out of memory");
                }
                hits[k][nhits[k]++] = a + i * 4;
            }
        }
        a = e;
    }
}

static int holds(int k, uint64_t a) {
    if (!plat_mem_readable(a, 8)) return 0;
    const uint32_t *w = (const uint32_t *)(uintptr_t)a;
    if (k == KIND_F32_30TH) return w[0] == f32_30th;
    if (k == KIND_F32_30) return w[0] == f32_30;
    return ((uint64_t)w[1] << 32 | w[0]) == f64_30th;
}

static void scan_all(void) {
    memset(nhits, 0, sizeof nhits);
    memset(counts, 0, sizeof counts);
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        scan_range(m->base + m->data_off, m->base + m->size, REG_MODULE);
    }
    const RtGuestLayout *L = mem_layout();
    scan_range(L->heap.base, L->heap.base + L->heap.size, REG_HEAP);
    scan_range(L->alias.base, L->alias.base + L->alias.size, REG_ALIAS);
}

static void report(uint64_t frame) {
    rt_log("value scan at present %llu:", (unsigned long long)frame);
    for (int k = 0; k < KIND_N; k++) {
        rt_log("value scan:   %s: %llu in module data, %llu in the heap, %llu in mapped memory", kind_name[k],
               (unsigned long long)counts[k][REG_MODULE], (unsigned long long)counts[k][REG_HEAP],
               (unsigned long long)counts[k][REG_ALIAS]);
        for (int i = 0; i < nhits[k] && i < SHOW; i++) {
            char nm[64];
            name_of(hits[k][i], nm, sizeof nm);
            rt_log("value scan:     %s at %s", kind_name[k], nm);
        }
    }
}

static void recheck(uint64_t frame) {
    rt_log("value scan at present %llu: first-scan hits that still hold their value:", (unsigned long long)frame);
    for (int k = 0; k < KIND_N; k++) {
        int kept = 0;
        for (int i = 0; i < nhits[k]; i++) {
            if (!holds(k, hits[k][i])) continue;
            if (kept++ < SHOW) {
                char nm[64];
                name_of(hits[k][i], nm, sizeof nm);
                rt_log("value scan:     %s still at %s", kind_name[k], nm);
            }
        }
        rt_log("value scan:   %s: %d of %d first-scan hits kept", kind_name[k], kept, nhits[k]);
    }
}

// ---- poke
typedef struct { uint64_t addr; uint64_t value; int wide; } Poke;
static Poke pokes[16];
static int npokes = -1;

static uint64_t parse_addr(const char *s, const char **end) {
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        size_t n = strlen(m->name);
        if (!strncmp(s, m->name, n) && s[n] == '+') return m->base + strtoull(s + n + 1, (char **)end, 16);
    }
    return strtoull(s, (char **)end, 16);
}

static void pokes_init(void) {
    npokes = 0;
    const char *e = cfg_get("TOTK_POKE");
    if (!e || !*e) return;
    for (const char *p = e; *p && npokes < 16;) {
        const char *q;
        uint64_t a = parse_addr(p, &q);
        if (*q != '=' || !a) { rt_log("value scan: TOTK_POKE: cannot read \"%s\"", p); return; }
        q++;
        Poke k = {.addr = a};
        if (q[0] == 'f' && q[1] == ':') {
            float f = strtof(q + 2, (char **)&q);
            uint32_t u;
            memcpy(&u, &f, 4);
            k.value = u;
        } else if (q[0] == 'd' && q[1] == ':') {
            double d = strtod(q + 2, (char **)&q);
            memcpy(&k.value, &d, 8);
            k.wide = 1;
        } else if (q[0] == 'i' && q[1] == ':') {
            k.value = (uint32_t)strtol(q + 2, (char **)&q, 0);
        } else { rt_log("value scan: TOTK_POKE: value needs f:, d: or i: (\"%s\")", q); return; }
        pokes[npokes++] = k;
        char nm[64];
        name_of(a, nm, sizeof nm);
        rt_log("value scan: poke %s before each present (TOTK_POKE)", nm);
        p = *q == ',' ? q + 1 : q;
    }
}

static void poke_all(void) {
    for (int i = 0; i < npokes; i++) {
        RtMemoryInfo mi;
        mem_query(pokes[i].addr, &mi);
        if ((mi.perm & 3) != 3 || !plat_mem_readable(pokes[i].addr, 8)) continue;
        if (pokes[i].wide) memcpy((void *)(uintptr_t)pokes[i].addr, &pokes[i].value, 8);
        else {
            uint32_t v = (uint32_t)pokes[i].value;
            memcpy((void *)(uintptr_t)pokes[i].addr, &v, 4);
        }
    }
}

// ---- poke of scan hits (TOTK_POKE_SCAN)
typedef struct { int kind, lo, hi; } PokeRange;
static PokeRange pranges[8];
static int npranges;
static volatile int poke_scan_live;

static void poke_scan_parse(void) {
    const char *e = cfg_get("TOTK_POKE_SCAN");
    if (!e || !*e) return;
    for (const char *p = e; *p && npranges < 8;) {
        int k = !strncmp(p, "f32:", 4) ? KIND_F32_30TH : !strncmp(p, "d64:", 4) ? KIND_F64_30TH
              : !strncmp(p, "f30:", 4) ? KIND_F32_30 : -1;
        if (k < 0) { rt_log("value scan: TOTK_POKE_SCAN: kind must be f32:, d64: or f30: (\"%s\")", p); return; }
        char *q;
        long lo = strtol(p + 4, &q, 10), hi = lo;
        if (*q == '-') hi = strtol(q + 1, &q, 10);
        pranges[npranges++] = (PokeRange){k, (int)lo, (int)hi};
        p = *q == ',' ? q + 1 : q;
    }
}

static void poke_scan_once(void) {
    const float f60th = 1.0f / 60.0f, f60 = 60.0f;
    const double d60th = 1.0 / 60.0;
    for (int r = 0; r < npranges; r++) {
        int k = pranges[r].kind;
        for (int i = pranges[r].lo; i <= pranges[r].hi && i < nhits[k]; i++) {
            void *a = (void *)(uintptr_t)hits[k][i];
            if (k == KIND_F32_30TH) memcpy(a, &f60th, 4);
            else if (k == KIND_F32_30) memcpy(a, &f60, 4);
            else memcpy(a, &d60th, 8);
        }
    }
}

static void poke_scan_thread(void *arg) {
    (void)arg;
    for (;;) {
        poke_scan_once();
        plat_sleep_ms(1);
    }
}

static void poke_scan_start(void) {
    if (!npranges) return;
    int n = 0;
    for (int r = 0; r < npranges; r++) {
        int k = pranges[r].kind, hi = pranges[r].hi < nhits[k] - 1 ? pranges[r].hi : nhits[k] - 1;
        if (hi >= pranges[r].lo) n += hi - pranges[r].lo + 1;
        rt_log("value scan: poking %s hits %d-%d (of %d) with the 60 fps value", kind_name[k], pranges[r].lo, hi, nhits[k]);
    }
    rt_log("value scan: %d addresses poked every millisecond from now on (TOTK_POKE_SCAN)", n);
    poke_scan_once();
    poke_scan_live = 1;
    if (!plat_thread_spawn(poke_scan_thread, NULL, "value poke", 64 << 10)) rt_log("value scan: poke thread failed");
}

void value_scan_present(uint64_t frame) {
    static int on = -1;
    if (on < 0) on = rt_knob("VALUE_SCAN", "0");
    if (npokes < 0) pokes_init();
    if (npokes) poke_all();
    if (!on) return;
    if (frame == 900) {
        scan_all();
        report(frame);
        poke_scan_parse();
        poke_scan_start();
    } else if (frame == 1200) {
        recheck(frame);
    }
}
