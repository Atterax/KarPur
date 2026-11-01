// KARPUR_WATCH: log calls to chosen SDK imports, for crash analysis. Written for KarPur (MIT).
//
// KARPUR_WATCH is a comma-separated list of symbol names as karpur.log prints them (mangled, e.g.
// _ZN2nn2os11GetTlsValueENS0_7TlsSlotE). Each import of such a symbol through a JUMP_SLOT gets a host observer
// (hostfn.c, the HOST_OBSERVER mechanism) that calls the guest definition and logs caller, x0-x3, the result and the
// guest thread's TLS address. A line is written only for a new (symbol, caller, result) combination, at most
// WATCH_MAX lines a run, so hot functions stay readable.
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

static struct { const char *name; uint64_t lr, ret; } seen[SEEN_CAP];
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

static void watch_fn(Cpu *c) {
    const char *name = hostfn_name(c->pc);
    uint64_t lr = c->x[30], a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3];
    uint64_t ret = rt_original(c);
    int fresh = 1;
    plat_mutex_lock(&lock);
    if (nranges < 0) read_ranges();
    if (!caller_wanted(lr - 4)) {
        plat_mutex_unlock(&lock);
        return;
    }
    for (int i = 0; i < nseen && fresh; i++)
        if (seen[i].name == name && seen[i].lr == lr && seen[i].ret == ret) fresh = 0;
    if (fresh && nseen < SEEN_CAP) seen[nseen++] = (typeof(seen[0])){name, lr, ret};
    int n = fresh && nlines < WATCH_MAX ? ++nlines : 0;
    plat_mutex_unlock(&lock);
    if (!n) return;
    char from[160];
    rt_log("watch %d: %s(%#llx, %#llx, %#llx, %#llx) -> %#llx from %s, tls %#llx", n, name ? name : "?",
           (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2, (unsigned long long)a3,
           (unsigned long long)ret, ldr_describe(lr - 4, from, sizeof from), (unsigned long long)c->tpidrro);
    if (n == WATCH_MAX) rt_log("watch: %d lines written, no more this run", WATCH_MAX);
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
