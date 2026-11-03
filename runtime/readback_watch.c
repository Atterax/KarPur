// KarPur: readback watch (TOTK_READBACK_WATCH=1, a diagnostic; docs/progress.md 2026-11-03 "Plan Speichermodell").
//
// With VRAM mirrors the GPU's writes reach guest memory only at the end of a submission (vk_memory.c
// vk_mem_mirror_end), and Violet reads some of them back (tree culling and LOD data). TOTK_SUBMIT_SYNC=1 makes every
// submit wait for the GPU so that those reads see the results; this watch finds out where and when the game reads
// them, so that a targeted wait can replace it. Every page a submission's write-back covers is made inaccessible when
// the write-back is recorded (memory.c mem_rbw_arm); the first CPU access faults, the page becomes read-write again,
// and the access is counted for its reader: the lifted guest function holding the faulting instruction, or for host
// code (the runtime's memcpy, a driver) the guest caller in x30. Each touch is classed by whether the submission's
// fence had completed (else the CPU read the bytes before the GPU's results were there) and whether the game had
// waited, through nvnSyncWait or nvnQueueFinish, for a fence that covers it. Run it with TOTK_SUBMIT_SYNC=0: with the
// wait every read comes after the GPU. Page granularity: a reader of bytes next to the written ones counts too.
// Output, on the player's PC only: log lines with guest addresses (main+offset), counts and times.
//
// TOTK_READBACK_GUARD=1 (implies the watch) is the targeted wait that can replace TOTK_SUBMIT_SYNC: a touch by lifted
// game code inside the written bytes of a submission whose fence has not completed waits, off its virtual core, until
// it has, so the read sees the GPU's results as it would on the Switch, where the GPU writes the memory itself. Reads of
// other data that shares the page, host code (the runtime, drivers) and touches after the GPU finished go on at once.
// The first watch run (forest, 2026-11-03) found one such reader, main+0xb88fc0, about 3 ms after the submit.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "gfx.h"
#include "host.h"
#include "vk.h"
#include "platform/platform.h"

#define ARM_SLOTS (1u << 16)   // page -> fence and record time; a collision overwrites (the older page reports fence 0)
#define READERS 512
#define EVENTS 24
#define REPORT_FRAMES 600
#define GUARD_TIMEOUT_NS 100000000ull   // a guarded read waits at most 100 ms for its fence

// lo, hi: the bytes of the page the write-backs covered since it was armed (offsets in the page), so a touch can be
// told apart from one of other data that only shares the page.
typedef struct { uint64_t page, fence, t_ns; uint16_t lo, hi; } Arm;
typedef struct {
    uint64_t guest, host;      // the reader: guest function or guest caller (x30), and the host rip for host code
    uint64_t n, inside, early, early_in, unsynced, unknown, sum_us, max_us;
} Reader;
typedef struct { uint64_t addr, fence, guest, host, done, synced, us; int inside; } Event;

static int on = -1, guard;
static int ready;
static Arm arms[ARM_SLOTS];
static uint64_t done_max, synced_max, last_registered;
static PlatMutex rd_lock = PLAT_MUTEX_INIT;   // readers, events, counters below; taken in the fault handler
static Reader rd[READERS];
static int nrd;
static uint64_t rd_dropped;
static Event ev[EVENTS];
static int nev;
static uint64_t n_armed, n_refused, n_touch, n_early, n_unsynced;
static uint64_t n_waits, n_wait_fail, wait_us, wait_max_us;   // rd_lock: guarded reads (TOTK_READBACK_GUARD)

static int watch_on(void) {
    if (on < 0) {
        guard = rt_knob("READBACK_GUARD", "0") != 0;
        on = guard || rt_knob("READBACK_WATCH", "0");
    }
    return on;
}

static uint32_t slot(uint64_t page) { return (uint32_t)((page >> 12) * 0x9E3779B97F4A7C15ull >> 48) & (ARM_SLOTS - 1); }

static void atomic_max(uint64_t *p, uint64_t v) {
    uint64_t cur = __atomic_load_n(p, __ATOMIC_RELAXED);
    while (v > cur && !__atomic_compare_exchange_n(p, &cur, v, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {}
}

static void fence_done(void *arg) { atomic_max(&done_max, (uint64_t)(uintptr_t)arg); }

static Reader *reader(uint64_t guest, uint64_t host) {
    for (int i = 0; i < nrd; i++)
        if (rd[i].guest == guest && rd[i].host == host) return &rd[i];
    if (nrd == READERS) return NULL;
    rd[nrd] = (Reader){.guest = guest, .host = host};
    return &rd[nrd++];
}

// The access hook (platform.h plat_mem_access_hook): runs on the faulting thread inside the exception handler, so it
// formats nothing and logs nothing.
static int access_fault(uint64_t a, uintptr_t rip, int write) {
    int r = mem_rbw_fault(a, write);
    if (r != 1) return r != 0;
    uint64_t page = a & ~0xFFFull, now = plat_now_ns();
    const Arm *s = &arms[slot(page)];
    uint64_t fence = s->page == page ? s->fence : 0, t = s->page == page ? s->t_ns : 0;
    int inside = s->page == page && (a & 0xFFF) >= s->lo && (a & 0xFFF) < s->hi;
    uint64_t guest = rt_lifted_function(rip), host = 0;
    if (!guest) {   // host code: name it by the guest code that called into it
        Cpu *c = kern_cpu();
        guest = c ? c->x[30] : 0;
        host = rip - plat_exe_base();
    }
    uint64_t done = __atomic_load_n(&done_max, __ATOMIC_ACQUIRE), synced = __atomic_load_n(&synced_max, __ATOMIC_ACQUIRE);
    uint64_t us = t && now > t ? (now - t) / 1000 : 0;
    uint64_t waited = 0;
    int wait_ok = 1;
    if (guard && inside && fence > done && !host && kern_cpu()) {   // a game read of results the GPU has not written yet
        int away = kern_core_away();
        wait_ok = gfx_backend()->fence_wait(fence, GUARD_TIMEOUT_NS);
        kern_core_back(&away);
        waited = (plat_now_ns() - now) / 1000 + 1;
    }
    plat_mutex_lock(&rd_lock);
    if (waited) {
        n_waits++, n_wait_fail += !wait_ok, wait_us += waited;
        if (waited > wait_max_us) wait_max_us = waited;
    }
    n_touch++;
    Reader *x = reader(guest, host);
    if (!x) rd_dropped++;
    if (fence && fence > done) n_early++;
    if (fence && fence > synced) n_unsynced++;
    if (x) {
        x->n++;
        x->inside += inside;
        if (!fence) x->unknown++;
        else {
            x->early += fence > done;
            x->early_in += inside && fence > done;
            x->unsynced += fence > synced;
            x->sum_us += us;
            if (us > x->max_us) x->max_us = us;
        }
    }
    if (nev < EVENTS) ev[nev++] = (Event){a, fence, guest, host, done, synced, us, inside};
    plat_mutex_unlock(&rd_lock);
    return 1;
}

static void init_once(void) {
    if (ready) return;
    ready = 1;
    if (!mem_rbw_init()) {
        rt_log("readback: out of memory for the page bits; the watch is off");
        on = 0;
        return;
    }
    rt_lifted_function(0);   // builds its table here, not inside the exception handler
    plat_mem_access_hook(access_fault);
    rt_log("readback: watch on: pages the GPU writes back are inaccessible until the CPU first touches them; summary "
           "every %d presents (TOTK_READBACK_WATCH)%s", REPORT_FRAMES,
           guard ? "; guard on: game reads of results not written yet wait for their fence (TOTK_READBACK_GUARD)" : "");
}

void rbw_arm_range(uint64_t addr, uint64_t size, uint64_t fence) {
    if (!size || !watch_on()) return;
    init_once();
    if (!on) return;
    uint64_t now = plat_now_ns(), armed = 0, refused = 0, end = addr + size;
    for (uint64_t p = addr & ~0xFFFull; p < end; p += 0x1000) {
        uint16_t lo = (uint16_t)(addr > p ? addr - p : 0), hi = (uint16_t)(end < p + 0x1000 ? end - p : 0x1000);
        int k = mem_rbw_arm(p);
        if (k) {
            Arm *s = &arms[slot(p)];
            if (k == 2 && s->page == p) {   // still armed, untouched since: widen to the new bytes, newest fence
                if (lo < s->lo) s->lo = lo;
                if (hi > s->hi) s->hi = hi;
                s->fence = fence, s->t_ns = now;
            } else {
                *s = (Arm){p, fence, now, lo, hi};
            }
            armed++;
        } else {
            refused++;
        }
    }
    if (fence && fence != __atomic_load_n(&last_registered, __ATOMIC_RELAXED)) {
        __atomic_store_n(&last_registered, fence, __ATOMIC_RELAXED);
        vk_completion_add(fence, fence_done, (void *)(uintptr_t)fence);
    }
    plat_mutex_lock(&rd_lock);
    n_armed += armed, n_refused += refused;
    plat_mutex_unlock(&rd_lock);
}

void rbw_cpu_synced(uint64_t fence) {
    if (on > 0) atomic_max(&synced_max, fence);
}

static int by_count(const void *a, const void *b) {
    uint64_t x = ((const Reader *)a)->n, y = ((const Reader *)b)->n;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void report(const char *when) {
    static Reader snap[READERS];
    static Event es[EVENTS];
    plat_mutex_lock(&rd_lock);
    int n = nrd, ne = nev;
    memcpy(snap, rd, sizeof *rd * (size_t)n);
    memcpy(es, ev, sizeof *ev * (size_t)ne);
    uint64_t armed = n_armed, refused = n_refused, touch = n_touch, early = n_early, unsynced = n_unsynced,
             dropped = rd_dropped, waits = n_waits, wfail = n_wait_fail, wus = wait_us, wmax = wait_max_us;
    nrd = nev = 0;
    n_armed = n_refused = n_touch = n_early = n_unsynced = rd_dropped = 0;
    n_waits = n_wait_fail = wait_us = wait_max_us = 0;
    plat_mutex_unlock(&rd_lock);
    if (guard)
        rt_log("readback: %s: guard: %llu reads waited for the GPU, %.3f ms on average (max %.3f), %llu timed out", when,
               (unsigned long long)waits, waits ? (double)wus / (double)waits / 1000.0 : 0.0, (double)wmax / 1000.0,
               (unsigned long long)wfail);
    rt_log("readback: %s: armed %llu pages (refused %llu: not watched or not read-write), first CPU touches %llu: %llu "
           "before the GPU had finished that submission, %llu without a sync wait covering it; %d readers%s",
           when, (unsigned long long)armed, (unsigned long long)refused, (unsigned long long)touch,
           (unsigned long long)early, (unsigned long long)unsynced, n, dropped ? " (more not kept)" : "");
    qsort(snap, (size_t)n, sizeof *snap, by_count);
    for (int i = 0; i < n && i < 24; i++) {
        const Reader *x = &snap[i];
        char g[128], h[48] = "";
        ldr_describe(x->guest, g, sizeof g);
        if (x->host) snprintf(h, sizeof h, " via host karpur+%#llx", (unsigned long long)x->host);
        uint64_t timed = x->n - x->unknown;
        rt_log("readback:   %6llu touches (%llu in the written bytes), %llu before the GPU finished (%llu in the written "
               "bytes), %llu unsynced, %.2f ms after the submit (max %.2f)%s: %s%s",
               (unsigned long long)x->n, (unsigned long long)x->inside, (unsigned long long)x->early,
               (unsigned long long)x->early_in, (unsigned long long)x->unsynced,
               timed ? (double)x->sum_us / (double)timed / 1000.0 : 0.0, (double)x->max_us / 1000.0,
               x->unknown ? " [some without record]" : "", g, h);
    }
    for (int i = 0; i < ne && i < 8; i++) {
        char g[128];
        ldr_describe(es[i].guest, g, sizeof g);
        rt_log("readback:   touch %#llx%s: fence %llu (done %llu, synced %llu), %.2f ms after the submit, by %s%s",
               (unsigned long long)es[i].addr, es[i].inside ? " in the written bytes" : " beside them",
               (unsigned long long)es[i].fence, (unsigned long long)es[i].done, (unsigned long long)es[i].synced,
               (double)es[i].us / 1000.0, g, es[i].host ? " (host code)" : "");
    }
}

void rbw_present(uint64_t frame) {
    static uint64_t last;
    if (on <= 0 || !ready) return;
    if (!last) last = frame;
    if (frame - last < REPORT_FRAMES) return;
    char w[64];
    snprintf(w, sizeof w, "presents %llu-%llu", (unsigned long long)last, (unsigned long long)frame);
    last = frame;
    report(w);
}

void rbw_exit(void) {
    static int done;
    if (on <= 0 || !ready || __atomic_exchange_n(&done, 1, __ATOMIC_ACQ_REL)) return;
    report("end of the run");
}
