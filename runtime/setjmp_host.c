// Host side of guest setjmp/longjmp (runtime/rt_setjmp.h). Written for KarPur (MIT).
//
// rt_setjmp_slot runs where the lifted code calls setjmp (before the guest setjmp itself, which still fills the guest
// jmp_buf): it records x19-x30, sp and v8-v15 as they are at the call (what AArch64 setjmp saves, x30 being the
// return address) and the thread's host call depth, keyed by the guest jmp_buf address. The longjmp override finds
// that record, restores the registers, sets x0 to the value (1 for 0) and host-longjmps into the frame of the lifted
// function that called setjmp; the generated code there reloads its register locals and continues after the call.
#include <stdint.h>
#include <string.h>

#include "host.h"
#include "rt_setjmp.h"

#define SLOTS 64

typedef struct {
    uint64_t env;          // guest jmp_buf address, 0 = free
    uint64_t use;          // last use, for replacing the oldest record when all are taken
    jmp_buf hb;
    uint64_t x[12], sp;    // x19-x30
    __uint128_t v[8];      // v8-v15
    int depth;             // kern_host_depth at setjmp
} Slot;

// Per host thread: a guest thread runs on one host thread (its fibers too), and longjmp stays on the thread of its
// setjmp.
static _Thread_local Slot slots[SLOTS];
static _Thread_local uint64_t uses;

static Slot *find(uint64_t env) {
    for (int i = 0; i < SLOTS; i++)
        if (slots[i].env == env) return &slots[i];
    return NULL;
}

jmp_buf *rt_setjmp_slot(Cpu *c) {
    uint64_t env = c->x[0];
    Slot *s = find(env);
    if (!s) {
        s = &slots[0];
        for (int i = 0; i < SLOTS; i++) {
            if (!slots[i].env) { s = &slots[i]; break; }
            if (slots[i].use < s->use) s = &slots[i];
        }
    }
    s->env = env;
    s->use = ++uses;
    memcpy(s->x, &c->x[19], sizeof s->x);
    s->sp = c->sp;
    memcpy(s->v, &c->v[8], sizeof s->v);
    s->depth = c->thread ? *kern_host_depth(c->thread) : 0;
    return &s->hb;
}

// void longjmp(jmp_buf env, int val)
static void ov_longjmp(Cpu *c) {
    Slot *s = find(c->x[0]);
    if (!s) {
        char at[160];
        rt_fatal("longjmp(%#llx, %d) from %s: no setjmp recorded for this buffer on this thread",
                 (unsigned long long)c->x[0], (int)c->x[1], ldr_describe(c->x[30] - 4, at, sizeof at));
    }
    int val = (int)c->x[1];
    memcpy(&c->x[19], s->x, sizeof s->x);
    c->sp = s->sp;
    memcpy(&c->v[8], s->v, sizeof s->v);
    c->x[0] = (uint64_t)(uint32_t)(val ? val : 1);
    if (c->thread) *kern_host_depth(c->thread) = s->depth;
    longjmp(s->hb, 1);
}

HOST_OVERRIDE("longjmp", ov_longjmp);
HOST_OVERRIDE("_longjmp", ov_longjmp);
HOST_OVERRIDE("siglongjmp", ov_longjmp);
