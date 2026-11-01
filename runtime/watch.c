// KARPUR_WATCH: log calls to chosen SDK imports, for crash analysis. Written for KarPur (MIT).
//
// KARPUR_WATCH is a comma-separated list of symbol names as karpur.log prints them (mangled, e.g.
// _ZN2nn2os11GetTlsValueENS0_7TlsSlotE). Each import of such a symbol through a JUMP_SLOT gets a host observer
// (hostfn.c, the HOST_OBSERVER mechanism) that calls the guest definition and logs caller, x0-x3, the result and the
// guest thread's TLS address. A line is written only for a new (symbol, caller, result) combination, at most
// WATCH_MAX lines a run, so hot functions stay readable.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"

#define WATCH_MAX 600
#define SEEN_CAP 4096

static struct { const char *name; uint64_t lr, ret; } seen[SEEN_CAP];
static int nseen, nlines;
static PlatMutex lock = PLAT_MUTEX_INIT;

static void watch_fn(Cpu *c) {
    const char *name = hostfn_name(c->pc);
    uint64_t lr = c->x[30], a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3];
    uint64_t ret = rt_original(c);
    int fresh = 1;
    plat_mutex_lock(&lock);
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
