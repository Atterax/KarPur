// Runs every lifted test function of tests/lifter/guest.c on many inputs and compares each result with the same
// function compiled for the host. Built and run by tests/lifter/test_lifter.py; Linux only (fixed-address mmap).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "cpu.h"
#include "layout.h"

extern const uint64_t main_fn_addrs[];
extern const GuestFn main_fn_ptrs[];
extern const uint64_t main_fn_count;

volatile int g_fnprof = 0;
void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0) { (void)addr; (void)lr; (void)x0; }

static GuestFn lookup(uint64_t a) {
    uint64_t lo = 0, hi = main_fn_count;
    while (lo < hi) {
        uint64_t mid = (lo + hi) / 2;
        if (main_fn_addrs[mid] < a) lo = mid + 1; else hi = mid;
    }
    return lo < main_fn_count && main_fn_addrs[lo] == a ? main_fn_ptrs[lo] : NULL;
}

void rt_dispatch(Cpu *c) {
    GuestFn f = lookup(c->pc);
    if (!f) { fprintf(stderr, "rt_dispatch: no function at %#llx\n", (unsigned long long)c->pc); exit(3); }
    f(c);
}
void rt_spin_gate(Cpu *c, uint64_t head) { (void)c; (void)head; }
void rt_svc(Cpu *c, uint32_t imm, uint64_t pc) { (void)c; fprintf(stderr, "svc %#x at %#llx\n", imm, (unsigned long long)pc); exit(4); }
void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) { (void)c; fprintf(stderr, "brk %#x at %#llx\n", imm, (unsigned long long)pc); exit(5); }
void rt_udf(Cpu *c, uint64_t pc) { (void)c; fprintf(stderr, "unsupported instruction at %#llx\n", (unsigned long long)pc); exit(6); }
uint64_t rt_ticks(void) { return 0; }

typedef uint64_t (*NatFn)(uint64_t, uint64_t, uint64_t);
typedef struct { const char *name; uint64_t offset; NatFn native; } Test;
#include "tests.h"   // TESTS[], written by test_lifter.py

static const uint64_t SPECIAL[] = {
    0, 1, 2, 3, 7, 63, 64, 1000, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 0xFFFFFFFFull,
    0x80000000ull, 0x3FF0000000000000ull /* 1.0 */, 0xBFF8000000000000ull /* -1.5 */, 0x7FF0000000000000ull /* inf */,
    0xFFF0000000000000ull /* -inf */, 0x7FF8000000000000ull /* NaN */, 0x0000000000000001ull /* denormal */,
    0x8000000000000000ull /* -0.0 */, 0x4059000000000000ull /* 100.0 */, 0x7FEFFFFFFFFFFFFFull /* max */,
};
#define NSPECIAL (sizeof SPECIAL / sizeof SPECIAL[0])

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (rng & 3) == 0 ? SPECIAL[(rng >> 8) % NSPECIAL] : (rng & 3) == 1 ? (rng >> 40) : rng;
}

static uint8_t stack[1 << 20] __attribute__((aligned(16)));

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: harness IMAGE\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    void *mem = mmap((void *)MOD_MAIN_BASE, MOD_MAIN_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem != (void *)MOD_MAIN_BASE) { perror("mmap"); return 2; }
    size_t got = fread(mem, 1, MOD_MAIN_SIZE, f);
    fclose(f);
    if (got == 0) { fprintf(stderr, "empty image\n"); return 2; }

    int failures = 0, runs = 0;
    for (size_t t = 0; t < sizeof TESTS / sizeof TESTS[0]; t++) {
        GuestFn fn = lookup(MOD_MAIN_BASE + TESTS[t].offset);
        if (!fn) { printf("FAIL %-12s not found by function discovery\n", TESTS[t].name); failures++; continue; }
        int bad = 0;
        for (int i = 0; i < 400; i++) {
            uint64_t a = next(), b = next(), cc = next();
            static Cpu cpu;
            memset(&cpu, 0, sizeof cpu);
            cpu.sp = (uint64_t)(uintptr_t)(stack + sizeof stack);
            cpu.x[0] = a; cpu.x[1] = b; cpu.x[2] = cc;
            fn(&cpu);
            uint64_t want = TESTS[t].native(a, b, cc);
            runs++;
            if (cpu.x[0] != want) {
                if (bad++ < 3)
                    printf("FAIL %-12s (%#llx, %#llx, %#llx): lifted %#llx, native %#llx\n", TESTS[t].name,
                           (unsigned long long)a, (unsigned long long)b, (unsigned long long)cc,
                           (unsigned long long)cpu.x[0], (unsigned long long)want);
            }
        }
        if (bad) failures++;
        else printf("ok   %s\n", TESTS[t].name);
    }
    printf("%d runs, %d failing tests\n", runs, failures);
    return failures ? 1 : 0;
}
