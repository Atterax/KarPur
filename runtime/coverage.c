// KarPur: scene coverage, which functions of main run in which phase of play (TOTK_COVERAGE=1).
//
// Every lifted function starts with FN_ENTER (cpu.h), which calls fnprof_hit while g_fnprof is set. With
// TOTK_COVERAGE=1 this file sets it and marks each function of main that runs: one 16-bit mask per instruction word of
// main, bit p for phase p. The player starts the next phase with F5 (boot = phase 0; e.g. title, intro, character
// editor, ...; up to 16) and writes the lists with F4 (also written at exit). Output, on the player's PC only:
//   game/coverage/phases.txt   one line per phase: its number, the present it began at, functions seen, functions
//                              seen in no earlier phase, functions seen in this phase only
//   game/coverage/functions.txt one line per function of main that ran: main offset (hex) and the phase mask (hex)
// Addresses and counts only, nothing of the game's code or data; tools turn the offsets into names and reports. The
// call per function costs frame rate, so it is for analysis runs, not for play. docs/progress.md 2026-11-03.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "input_map.h"
#include "input_wsys.h"
#include "platform/platform.h"

#define MAX_PHASES 16

volatile int g_fnprof = 0;
static uint16_t *mask;           // per instruction word of main
static uint64_t base, words;
static volatile int phase;
static uint64_t phase_frame[MAX_PHASES];

void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0) {
    (void)lr, (void)x0;
    uint64_t i = (addr - base) >> 2;
    if (i < words) {
        uint16_t bit = (uint16_t)(1u << phase);
        if (!(mask[i] & bit)) mask[i] |= bit;   // a racing writer may drop a bit once; the next call sets it
    }
}

static void write_lists(uint64_t frame) {
    plat_file_mkdir_p("game/coverage");
    FILE *f = fopen("game/coverage/functions.txt", "w"), *p = fopen("game/coverage/phases.txt", "w");
    if (!f || !p) {
        if (f) fclose(f);
        if (p) fclose(p);
        rt_log("coverage: cannot write game/coverage");
        return;
    }
    uint64_t seen[MAX_PHASES] = {0}, fresh[MAX_PHASES] = {0}, only[MAX_PHASES] = {0}, total = 0;
    fprintf(f, "# main offset, phase mask (bit p = phase p); KarPur TOTK_COVERAGE\n");
    for (uint64_t i = 0; i < words; i++) {
        uint16_t m = mask[i];
        if (!m) continue;
        total++;
        fprintf(f, "%llx %x\n", (unsigned long long)(i << 2), m);
        int first = __builtin_ctz(m);
        fresh[first]++;
        for (int k = 0; k < MAX_PHASES; k++)
            if (m >> k & 1) seen[k]++;
        if (!(m & (m - 1))) only[first]++;
    }
    fprintf(p, "# phase, present it began at, functions seen, new (seen in no earlier phase), only in this phase\n");
    for (int k = 0; k <= phase && k < MAX_PHASES; k++)
        fprintf(p, "%d %llu %llu %llu %llu\n", k, (unsigned long long)phase_frame[k], (unsigned long long)seen[k],
                (unsigned long long)fresh[k], (unsigned long long)only[k]);
    fclose(f);
    fclose(p);
    rt_log("coverage: present %llu: %llu functions of main ran over %d phases; lists in game/coverage",
           (unsigned long long)frame, (unsigned long long)total, phase + 1);
    for (int k = 0; k <= phase && k < MAX_PHASES; k++)
        rt_log("coverage:   phase %d (from present %llu): %llu functions, %llu new, %llu only here", k,
               (unsigned long long)phase_frame[k], (unsigned long long)seen[k], (unsigned long long)fresh[k],
               (unsigned long long)only[k]);
}

static void at_exit(void) {
    if (mask) write_lists(0);
}

void coverage_present(uint64_t frame) {
    static int on = -1, was5, was4;
    if (on < 0) {
        on = rt_knob("COVERAGE", "0");
        if (!on) return;
        const RtLoaded *m = ldr_module(0);
        base = m->base, words = m->size >> 2;
        mask = calloc(words, sizeof *mask);
        if (!mask) {
            rt_log("coverage: out of memory");
            on = 0;
            return;
        }
        g_fnprof = 1;
        atexit(at_exit);
        rt_log("coverage: on for %s (%llu words); F5 starts the next phase, F4 writes game/coverage", m->name,
               (unsigned long long)words);
    }
    if (!on) return;
    int fg = ikb_foreground(), k5 = fg && ikb_down(KEY_F5), k4 = fg && ikb_down(KEY_F4);
    if (k5 && !was5 && phase + 1 < MAX_PHASES) {
        phase_frame[phase + 1] = frame;
        __atomic_store_n(&phase, phase + 1, __ATOMIC_RELEASE);
        rt_log("coverage: phase %d begins at present %llu (F5)", phase, (unsigned long long)frame);
    }
    if (k4 && !was4) write_lists(frame);
    was5 = k5, was4 = k4;
}
