// KARPUR_WATCH: log calls to chosen SDK imports, for crash analysis. Written for KarPur (MIT).
//
// KARPUR_WATCH is a comma-separated list of symbol names as karpur.log prints them (mangled, e.g.
// _ZN2nn2os11GetTlsValueENS0_7TlsSlotE). Each import of such a symbol through a JUMP_SLOT gets a host observer
// (hostfn.c, the HOST_OBSERVER mechanism) that calls the guest definition and logs caller, x0-x6, the 16 bytes x0
// points to (a search key or a name; only 16 bytes, so no game data of any size reaches a report), the result and the
// guest thread's TLS address. A line is written before the call for each new (symbol, caller), so functions that never
// return (__cxa_throw) show up too, and one after it for each new result; at most WATCH_MAX lines a run.
//
// KARPUR_WATCH_FROM limits the lines to callers in the listed ranges, comma-separated as module+0xLO-0xHI (offsets in
// the module, HI exclusive), e.g. main+0x14ff4dc-0x14ff9ac: one function's calls among thousands elsewhere.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"

#define WATCH_MAX 600
#define SEEN_CAP 4096

static struct { const char *name; uint64_t lr, ret; int pre; } seen[SEEN_CAP];
static int nseen, nlines;
static PlatMutex lock = PLAT_MUTEX_INIT;

#define MAX_RANGES 16
static struct { uint64_t lo, hi; } ranges[MAX_RANGES];
static int nranges = -1;   // -1: KARPUR_WATCH_FROM not read yet (the modules must be loaded first)

static void read_ranges(void) {
    nranges = 0;
    const char *e = getenv("KARPUR_WATCH_FROM");
    while (e && *e && nranges < MAX_RANGES) {
        char mod[32];
        unsigned long long lo, hi;
        int used = 0;
        if (sscanf(e, "%31[^+]+%llx-%llx%n", mod, &lo, &hi, &used) != 3 || hi <= lo) {
            rt_log("watch: KARPUR_WATCH_FROM: cannot read '%s' (module+0xLO-0xHI)", e);
            break;
        }
        int found = 0;
        for (int i = 0; i < ldr_count(); i++) {
            const RtLoaded *m = ldr_module(i);
            if (!strcmp(m->name, mod)) {
                ranges[nranges].lo = m->base + lo;
                ranges[nranges].hi = m->base + hi;
                nranges++;
                found = 1;
                rt_log("watch: only callers in %s+%#llx-%#llx", mod, lo, hi);
            }
        }
        if (!found) rt_log("watch: KARPUR_WATCH_FROM: no module '%s'", mod);
        e += used;
        if (*e == ',') e++;
    }
}

static int caller_wanted(uint64_t at) {
    if (!nranges) return 1;
    for (int i = 0; i < nranges; i++)
        if (at >= ranges[i].lo && at < ranges[i].hi) return 1;
    return 0;
}

// 1 if (name, lr, ret, pre) is new and a line may be written; the line number then goes to *n.
static int fresh_line(const char *name, uint64_t lr, uint64_t ret, int pre, int *n) {
    int fresh = 1;
    plat_mutex_lock(&lock);
    for (int i = 0; i < nseen && fresh; i++)
        if (seen[i].name == name && seen[i].lr == lr && seen[i].ret == ret && seen[i].pre == pre) fresh = 0;
    if (fresh && nseen < SEEN_CAP) seen[nseen++] = (typeof(seen[0])){name, lr, ret, pre};
    *n = fresh && nlines < WATCH_MAX ? ++nlines : 0;
    plat_mutex_unlock(&lock);
    if (*n == WATCH_MAX) rt_log("watch: %d lines written, no more this run", WATCH_MAX);
    return *n != 0;
}

// Two lines per new caller: one before the call (arguments; also for functions that never return, such as
// __cxa_throw), one after it for each new result.
static void watch_fn(Cpu *c) {
    const char *name = hostfn_name(c->pc);
    uint64_t lr = c->x[30], a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3], a4 = c->x[4], a5 = c->x[5],
             a6 = c->x[6];
    plat_mutex_lock(&lock);
    if (nranges < 0) read_ranges();
    int wanted = caller_wanted(lr - 4);
    plat_mutex_unlock(&lock);
    if (!wanted) {
        rt_original(c);
        return;
    }
    char from[160];
    ldr_describe(lr - 4, from, sizeof from);
    int n;
    if (fresh_line(name, lr, 0, 1, &n)) {
        // The first 16 bytes x0 points to (a search key, a name): hex, and as text when printable. x1 named when it
        // is a module address (the type_info of __cxa_throw, a vtable).
        char mem[80] = "", x1[160] = "";
        if (a0 >= 0x10000 && plat_mem_readable(a0, 16)) {
            const uint8_t *b = (const uint8_t *)(uintptr_t)a0;
            char hex[40], txt[17];
            int printable = 1;
            for (int i = 0; i < 16; i++) {
                snprintf(hex + 2 * i, 3, "%02x", b[i]);
                txt[i] = b[i] >= 32 && b[i] < 127 ? (char)b[i] : '.';
                if (i < 4 && (b[i] < 32 || b[i] >= 127)) printable = 0;
            }
            txt[16] = 0;
            snprintf(mem, sizeof mem, ", [x0] %s%s%s%s", hex, printable ? " \"" : "", printable ? txt : "",
                     printable ? "\"" : "");
        }
        for (int i = 0; i < ldr_count(); i++) {
            const RtLoaded *m = ldr_module(i);
            if (a1 >= m->base && a1 < m->base + m->size) {
                char d[140];
                snprintf(x1, sizeof x1, ", x1 = %s", ldr_describe(a1, d, sizeof d));
            }
        }
        rt_log("watch %d: %s(%#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#llx) from %s, tls %#llx%s%s", n,
               name ? name : "?", (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2,
               (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5, (unsigned long long)a6, from,
               (unsigned long long)c->tpidrro, mem, x1);
    }
    uint64_t ret = rt_original(c);
    if (fresh_line(name, lr, ret, 0, &n))
        rt_log("watch %d: %s from %s -> %#llx", n, name ? name : "?", from, (unsigned long long)ret);
}

// The observer for name if KARPUR_WATCH lists it, else NULL.
GuestFn watch_observer(const char *name) {
    const char *list = getenv("KARPUR_WATCH");
    if (!list || !name) return NULL;
    size_t len = strlen(name);
    for (const char *p = list; *p;) {
        size_t n = strcspn(p, ",");
        if (n == len && !strncmp(p, name, n)) {
            rt_log("watch: observing %s", name);
            return watch_fn;
        }
        p += n + (p[n] == ',');
    }
    return NULL;
}
