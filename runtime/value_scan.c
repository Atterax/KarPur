// KarPur: value scan and poke, for finding the game's frame step (the 60 fps work).
//
// Violet steps its world by a fixed amount per frame: paced at 60 fps (TOTK_PRESENT_INTERVAL=1) it runs twice as fast
// (docs/progress.md 2026-11-02). If that step is a float kept in memory (1/30 s, or 30 per second, or 2 vsyncs), a
// scan finds where it lives and a poke tests a candidate.
//
// TOTK_VALUE_SCAN=1: at present 900 (TOTK_VALUE_SCAN=f9: when F9 is pressed in the game) and again 300 presents later (in play, after loading), every writable page of the
// modules' data, the heap and the mapped (alias) memory is searched for the 32-bit floats 1/30 and 30.0, the 64-bit
// float 1/30 and the integer 33333334 (ns, 32 or 64 bits). The log gets the count per kind and region and the first hits as module offsets or guest addresses; the
// second scan says which hits of the first one still hold the value (a variable that keeps it, rather than a
// temporary). Reports addresses only, never memory contents.
//
// TOTK_POKE_SCAN=KIND:LO-HI[,KIND:LO-HI...] (needs TOTK_VALUE_SCAN=1): from the first scan on, the hits LO..HI (scan
// order, 0-based) of KIND are candidates for a bisection by key (see below): f32 (float 1/30) and d64 (double 1/30)
// get 1/60, f30 (float 30.0) gets 60.0, n32 / n64 (33333334 ns) get 16666667. A thread writes them every millisecond, so a step the game recomputes each
// frame is mostly overwritten before it is read. Run it with TOTK_PRESENT_INTERVAL=1 (the game then runs too fast).
//
// TOTK_PATCH_BISECT=1: the same bisection over the frame-step patch sets (see patch_bisect_start).
//
// TOTK_POKE=ADDR=VALUE[,ADDR=VALUE...]: before each present the 32-bit word at ADDR gets VALUE. ADDR is main+0x...,
// subsdk0+0x... or a 0x... guest address; VALUE is f:<float>, d:<double> (64-bit) or i:<integer>. Writes only to pages
// that are writable guest memory, or to a module's read-only constants (their host pages are made writable).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "input_map.h"
#include "input_wsys.h"
#include "platform/platform.h"

enum { KIND_F32_30TH, KIND_F32_30, KIND_F64_30TH, KIND_I32_NS, KIND_I64_NS, KIND_N };
static const char *const kind_name[KIND_N] = {"float 1/30", "float 30.0", "double 1/30", "int32 33333334 ns",
                                              "int64 33333334 ns"};
// KarPur (2026-11-03): Violet moves the player indoors in fixed 1/30 s steps taken from the frame time; the step was
// none of the code constants (tools/consts.py), so it may be a nanosecond value the game keeps in memory.
static const uint32_t ns_30 = 33333334u, ns_60 = 16666667u;
static const uint32_t f32_30th = 0x3D088889u, f32_30 = 0x41F00000u;
static const uint64_t f64_30th = 0x3FA1111111111111ull;

enum { REG_MODULE, REG_HEAP, REG_ALIAS, REG_N };

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
                else if (w[i] == ns_30) k = !(i & 1) && i + 1 < n && !w[i + 1] ? KIND_I64_NS : KIND_I32_NS;
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

// Through the guest memory map, not plat_mem_readable: VirtualQuery on the large mapped views made the recheck of
// ~50000 hits take 25 s.
static int holds(int k, uint64_t a) {
    RtMemoryInfo mi;
    mem_query(a, &mi);
    if ((mi.perm & 3) != 3 || a + 8 > mi.base + mi.size) return 0;
    const uint32_t *w = (const uint32_t *)(uintptr_t)a;
    if (k == KIND_F32_30TH) return w[0] == f32_30th;
    if (k == KIND_F32_30) return w[0] == f32_30;
    if (k == KIND_I32_NS) return w[0] == ns_30;
    if (k == KIND_I64_NS) return w[0] == ns_30 && !w[1];
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
typedef struct { uint64_t addr; uint64_t value; int wide; int unlocked; } Poke;
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

// A poke into a module's read-only part (a constant in .rodata that the code loads, as tools/consts.py lists them)
// makes the host pages writable once; the guest's view of the permissions stays as it was.
static int in_module(uint64_t a) {
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        if (a - m->base < m->size) return 1;
    }
    return 0;
}

static void poke_all(void) {
    for (int i = 0; i < npokes; i++) {
        RtMemoryInfo mi;
        mem_query(pokes[i].addr, &mi);
        if ((mi.perm & 3) != 3) {
            if (!in_module(pokes[i].addr) || pokes[i].unlocked < 0) continue;
            if (!pokes[i].unlocked) {
                uint64_t pg = pokes[i].addr & ~0xFFFull, end = (pokes[i].addr + 8 + 0xFFF) & ~0xFFFull;
                pokes[i].unlocked = plat_mem_protect(pg, end - pg, PLAT_PROT_RW) ? 1 : -1;
                char nm[64];
                name_of(pokes[i].addr, nm, sizeof nm);
                rt_log("value scan: poke %s: read-only module page %s", nm,
                       pokes[i].unlocked > 0 ? "made writable" : "could not be made writable (poke skipped)");
                if (pokes[i].unlocked < 0) continue;
            }
        } else if (!plat_mem_readable(pokes[i].addr, 8)) continue;
        if (pokes[i].wide) memcpy((void *)(uintptr_t)pokes[i].addr, &pokes[i].value, 8);
        else {
            uint32_t v = (uint32_t)pokes[i].value;
            memcpy((void *)(uintptr_t)pokes[i].addr, &v, 4);
        }
    }
}

// ---- poke of scan hits with a bisection by key (TOTK_POKE_SCAN)
// The candidates are the hits that TOTK_POKE_SCAN selects. The thread sets the first half of the current candidates
// to the 60 fps value and keeps the rest at 1/30 (30.0); the player says what they see: F6 = the game now runs at
// normal speed (the step is in the poked half), F7 = still twice as fast (it is in the other half). Each answer halves
// the candidates, so a few hundred hits take about ten presses, all in one run (the scan order differs between runs).
// Only words that still hold the 30 fps value (or our 60 fps value) are written: memory that moved on to other data
// is left alone, which keeps the poke from corrupting the game (an earlier version crashed on reused heap words).
typedef struct { int kind, lo, hi; } PokeRange;
static PokeRange pranges[8];
static int npranges;

typedef struct { uint64_t addr; int kind; int ours; int live; int set; } Cand;   // set >= 0: a patch set
static Cand *cands;
static int ncands, ccands;
static volatile int blo, bhi;   // current candidates [blo, bhi); the poked half is [blo, bmid)

static int bmid(void) { return bhi - blo > 1 ? blo + (bhi - blo) / 2 : bhi; }

static void poke_scan_parse(void) {
    const char *e = cfg_get("TOTK_POKE_SCAN");
    if (!e || !*e) return;
    for (const char *p = e; *p && npranges < 8;) {
        int k = !strncmp(p, "f32:", 4) ? KIND_F32_30TH : !strncmp(p, "d64:", 4) ? KIND_F64_30TH
              : !strncmp(p, "f30:", 4) ? KIND_F32_30 : !strncmp(p, "n32:", 4) ? KIND_I32_NS
              : !strncmp(p, "n64:", 4) ? KIND_I64_NS : -1;
        if (k < 0) { rt_log("value scan: TOTK_POKE_SCAN: kind must be f32:, d64:, f30:, n32: or n64: (\"%s\")", p); return; }
        char *q;
        long lo = strtol(p + 4, &q, 10), hi = lo;
        if (*q == '-') hi = strtol(q + 1, &q, 10);
        pranges[npranges++] = (PokeRange){k, (int)lo, (int)hi};
        p = *q == ',' ? q + 1 : q;
    }
}

// Writes the value of kind k for 30 fps (fast = 0) or 60 fps (fast = 1) when the word holds the other one.
static void poke_one(Cand *c, int fast) {
    if (c->set >= 0) {
        if (rt_patch_enabled(c->set) != fast) rt_patch_switch(c->set, fast);
        return;
    }
    static const uint32_t f32_60th = 0x3C888889u, f32_60 = 0x42700000u;
    static const uint64_t f64_60th = 0x3F91111111111111ull;
    if (!c->live) return;
    volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)c->addr;
    if (c->kind == KIND_F64_30TH) {
        volatile uint64_t *d = (volatile uint64_t *)(uintptr_t)c->addr;
        uint64_t v = *d, from = fast ? f64_30th : f64_60th, to = fast ? f64_60th : f64_30th;
        if (v == from && (fast || c->ours)) { *d = to; c->ours = 1; }
        return;
    }
    int ns = c->kind == KIND_I32_NS || c->kind == KIND_I64_NS;   // the upper word of an int64 stays 0
    uint32_t a30 = ns ? ns_30 : c->kind == KIND_F32_30TH ? f32_30th : f32_30;
    uint32_t a60 = ns ? ns_60 : c->kind == KIND_F32_30TH ? f32_60th : f32_60;
    uint32_t v = *w;
    if (fast && v == a30) { *w = a60; c->ours = 1; }
    else if (!fast && c->ours && v == a60) *w = a30;
}

static void cand_name(const Cand *c, char *buf, size_t n) {
    if (c->set >= 0) snprintf(buf, n, "patch set %s", rt_patch_name(c->set));
    else name_of(c->addr, buf, n);
}

// ---- write watch on the last candidate (KarPur 2026-11-03)
// When a bisection over memory hits ends with one word left, the page holding it is watched (plat_write_watch): every
// guest write to the word names the lifted function that wrote it, the guest return address and two frames further
// up, logged every 300 presents. That finds the code that sets the value, which a code patch can then change for good
// (the word's address differs between runs). Host writes (our own poke thread, copies of the runtime) count apart.
// TOTK_WATCH_READS=1 (default; 0: writes only) watches reads of the word too: a value set once at load is found by the
// code that reads it every frame.
typedef struct { uint64_t fn, lr, up1, up2; int write; volatile int64_t n; } Writer;
#define MAX_WRITERS 16
static Writer writers[MAX_WRITERS];
static volatile int nwriters, writers_lock;
static volatile int64_t w_host, w_page, w_lost;
static volatile uint64_t watch_addr, watch_req;

static void watch_write(uint64_t a, uintptr_t rip, int write) {   // the accessing thread, inside the exception handler
    uint64_t w = watch_addr;
    if (a + 8 <= w || a >= w + 8) { plat_inc64(&w_page); return; }
    uint64_t fn = rt_lifted_function(rip);
    Cpu *c = kern_cpu();
    if (!fn && !c) { plat_inc64(&w_host); return; }
    uint64_t lr = c ? c->x[30] : 0, up1 = 0, up2 = 0, fp = c ? c->x[29] : 0, fr[2];
    if (fp && !(fp & 7) && plat_mem_read(fp, fr, 16)) {
        up1 = fr[1];
        if (fr[0] && !(fr[0] & 7) && plat_mem_read(fr[0], fr, 16)) up2 = fr[1];
    }
    while (__atomic_exchange_n(&writers_lock, 1, __ATOMIC_ACQUIRE)) {}
    int i = 0;
    while (i < nwriters && !(writers[i].fn == fn && writers[i].lr == lr && writers[i].write == write)) i++;
    if (i == nwriters && nwriters < MAX_WRITERS) writers[nwriters++] = (Writer){fn, lr, up1, up2, write, 0};
    if (i < nwriters) writers[i].n++;
    else plat_inc64(&w_lost);
    __atomic_store_n(&writers_lock, 0, __ATOMIC_RELEASE);
}

static void watch_report(void) {
    char a[96], b[96], c[96], d[96], nm[64];
    name_of(watch_addr, nm, sizeof nm);
    rt_log("value scan: accesses to the watched word %s: %d readers/writers in lifted code, %lld host accesses, %lld "
           "other accesses in its page%s", nm, nwriters, (long long)w_host, (long long)w_page,
           w_lost ? " (table full)" : "");
    for (int i = 0; i < nwriters; i++)
        rt_log("value scan:   %lld x %s by f_%llx = %s, returns to %s, then %s, then %s", (long long)writers[i].n,
               writers[i].write ? "written" : "read",
               (unsigned long long)writers[i].fn, ldr_describe(writers[i].fn, a, sizeof a),
               ldr_describe(writers[i].lr, b, sizeof b), ldr_describe(writers[i].up1, c, sizeof c),
               ldr_describe(writers[i].up2, d, sizeof d));
}

static void bisect_log(const char *why) {
    int lo = blo, hi = bhi, mid = bmid();
    if (hi - lo <= 1) {
        if (hi > lo) {
            char nm[64];
            cand_name(&cands[lo], nm, sizeof nm);
            rt_log("value scan: bisect %s: one candidate left: %s%s%s (it stays poked)", why,
                   cands[lo].set >= 0 ? "" : kind_name[cands[lo].kind], cands[lo].set >= 0 ? "" : " at ", nm);
            if (cands[lo].set < 0) __atomic_store_n(&watch_req, cands[lo].addr, __ATOMIC_RELEASE);   // armed at present
        } else rt_log("value scan: bisect %s: no candidates left", why);
        return;
    }
    char a[64], b[64];
    cand_name(&cands[lo], a, sizeof a);
    cand_name(&cands[mid - 1], b, sizeof b);
    rt_log("value scan: bisect %s: %d candidates left, poking %d of them (%s .. %s); F6 = the change shows now "
           "(normal speed, smooth effects), F7 = it does not", why, hi - lo, mid - lo, a, b);
}

static void poke_scan_thread(void *arg) {
    (void)arg;
    int was6 = 0, was7 = 0;
    uint64_t tick = 0;
    for (;;) {
        int lo = blo, hi = bhi, mid = bmid();
        // Whether each word is still mapped guest memory, once a second through the guest memory map (cheap, no
        // system call). Asking Windows per word and tick (VirtualQuery) slowed the game to about 1 fps: on the large
        // mapped views it is slow and it takes the address space lock the game's own mappings need.
        if (tick++ % 1000 == 0) {
            for (int i = 0; i < ncands; i++) {
                if (cands[i].set >= 0) continue;
                RtMemoryInfo mi;
                mem_query(cands[i].addr, &mi);
                cands[i].live = (mi.perm & 3) == 3 && cands[i].addr + 8 <= mi.base + mi.size;
            }
        }
        for (int i = 0; i < ncands; i++) poke_one(&cands[i], i >= lo && i < mid);
        int fg = ikb_foreground(), k6 = fg && ikb_down(KEY_F6), k7 = fg && ikb_down(KEY_F7);
        if (k6 && !was6 && hi - lo > 1) { bhi = mid; bisect_log("F6 (normal speed)"); }
        if (k7 && !was7 && hi - lo > 1) { blo = mid; bisect_log("F7 (still too fast)"); }
        was6 = k6;
        was7 = k7;
        plat_sleep_ms(1);
    }
}

static void cand_add(Cand c) {
    if (ncands == ccands) {
        ccands += 1024;
        if (!(cands = realloc(cands, sizeof *cands * (size_t)ccands))) rt_fatal("value scan: out of memory");
    }
    cands[ncands++] = c;
}

static void bisect_go(void) {
    blo = 0;
    bhi = ncands;
    bisect_log("start");
    if (!plat_thread_spawn(poke_scan_thread, NULL, "value poke", 64 << 10)) rt_log("value scan: poke thread failed");
}

// TOTK_PATCH_BISECT=1: the candidates are the frame-step patch sets of tools/consts.py --patch-file (names fs_, fr_,
// us_, fh_, fx_, ns_ + module offset, each halving one constant), switched on and off instead of poked. Same keys, from present 900 on.
static void patch_bisect_start(void) {
    for (int i = 0; i < rt_patch_count(); i++) {
        const char *n = rt_patch_name(i);
        if (n && (!strncmp(n, "fs_", 3) || !strncmp(n, "fr_", 3) || !strncmp(n, "us_", 3) || !strncmp(n, "fh_", 3) ||
                  !strncmp(n, "fx_", 3) || !strncmp(n, "ns_", 3)))
            cand_add((Cand){.set = i});
    }
    rt_log("value scan: %d frame-step patch sets of %d are candidates (TOTK_PATCH_BISECT)", ncands, rt_patch_count());
    if (ncands) bisect_go();
}

static void poke_scan_start(void) {
    if (!npranges) return;
    for (int r = 0; r < npranges; r++) {
        int k = pranges[r].kind, hi = pranges[r].hi < nhits[k] - 1 ? pranges[r].hi : nhits[k] - 1;
        rt_log("value scan: candidates: %s hits %d-%d (of %d)", kind_name[k], pranges[r].lo, hi, nhits[k]);
        for (int i = pranges[r].lo; i <= hi; i++) cand_add((Cand){.addr = hits[k][i], .kind = k, .set = -1});
    }
    bisect_go();
}

void value_scan_present(uint64_t frame) {
    static int on = -1, pb = -1;
    static uint64_t scan_frame;   // the present of the first scan (900, or the F9 press with TOTK_VALUE_SCAN=f9)
    if (on < 0) {
        on = rt_knob("VALUE_SCAN", "0");
        const char *e = cfg_get("TOTK_VALUE_SCAN");
        if (on && e && !strcmp(e, "f9")) on = 2, rt_log("value scan: press F9 in the game to scan (TOTK_VALUE_SCAN=f9)");
    }
    if (pb < 0) pb = rt_knob("PATCH_BISECT", "0");
    if (npokes < 0) pokes_init();
    if (npokes) poke_all();
    if (pb && frame == 900) patch_bisect_start();
    if (!on) return;
    uint64_t req = __atomic_exchange_n(&watch_req, 0, __ATOMIC_ACQUIRE);
    if (req) {
        rt_lifted_function(0);   // builds its table here, not inside the exception handler
        watch_addr = req;
        char nm[64];
        name_of(req, nm, sizeof nm);
        int reads = rt_knob("WATCH_READS", "1");
        int ok = plat_write_watch(req, reads, watch_write);
        rt_log("value scan: %s %s %s (leave and enter the building again to catch the code that sets it)",
               ok ? "watching" : "cannot watch (platform)", reads ? "reads and writes of" : "writes to", nm);
    }
    if (watch_addr && frame % 300 == 0) watch_report();
    if (!scan_frame && (on == 2 ? ikb_foreground() && ikb_down(KEY_F9) : frame == 900)) {
        scan_frame = frame;
        scan_all();
        report(frame);
        poke_scan_parse();
        poke_scan_start();
    } else if (scan_frame && frame == scan_frame + 300) {
        recheck(frame);
    }
}
