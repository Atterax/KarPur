// NVN proc table. Every name the game's proc loader asks for gets a synthetic host
// address through hostfn_register before hostfn_seal; nvnBootstrapLoader and nvnDeviceGetProcAddress hand them out.
// nvnp_<name> is the implementation of one proc: the weak definitions here stop with the proc's name, nvn_api_*.c
// and nvn_ext.c define the strong ones. The name list is runtime/nvn_procs.inc (tools/gen_nvn_procs.py).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "host.h"
#include "platform/platform.h"

// The strong nvnp_nvnDeviceGetProcAddress below replaces the stand-in generated for the same row.
#define nvnp_nvnDeviceGetProcAddress nvnp_stub_nvnDeviceGetProcAddress

#define NVN_PROC(n) void nvnp_##n(Cpu *c);
#include "nvn_procs.inc"
#undef NVN_PROC

#define NVN_PROC(n)                                                                                                  \
    __attribute__((weak)) void nvnp_##n(Cpu *c) {                                                                     \
        (void)c;                                                                                                     \
        rt_fatal("NVN " #n " is not implemented");                                                                   \
    }
#include "nvn_procs.inc"
#undef NVN_PROC
#undef nvnp_nvnDeviceGetProcAddress
void nvnp_nvnDeviceGetProcAddress(Cpu *c);

// KarPur: nvn_has_<name> is 0 here and 1 where NVN_API (nvn_api.h) defines the proc, so a lookup can tell a weak
// stand-in from an implementation. nvnDeviceGetProcAddress is defined below without NVN_API.
#define NVN_PROC(n) __attribute__((weak)) const char nvn_has_##n = 0;
#include "nvn_procs.inc"
#undef NVN_PROC

typedef struct { const char *name; uint64_t addr; const volatile char *has; } Proc;

#define NVN_PROC(n) +1
enum { PROC_COUNT = 0
#include "nvn_procs.inc"
};
#undef NVN_PROC

static Proc procs[PROC_COUNT];
static int nprocs;

static int by_name(const void *a, const void *b) { return strcmp(((const Proc *)a)->name, ((const Proc *)b)->name); }

void nvn_procs_register(void) {
    if (nprocs) return;
#define NVN_PROC(n) procs[nprocs++] = (Proc){#n, hostfn_register("nvn:" #n, nvnp_##n), &nvn_has_##n};
#include "nvn_procs.inc"
#undef NVN_PROC
    qsort(procs, (size_t)nprocs, sizeof *procs, by_name);
}

// KarPur: the game took the address of a proc without an implementation. Each such proc is logged once, with a running
// count, so one run lists every missing proc the game resolves instead of stopping at the first one it calls.
static void proc_missing(const Proc *p) {
    static PlatMutex lock = PLAT_MUTEX_INIT;
    static const Proc *seen[PROC_COUNT];
    static int nseen;
    int fresh = 1;
    plat_mutex_lock(&lock);
    for (int i = 0; i < nseen; i++)
        if (seen[i] == p) fresh = 0;
    if (fresh) seen[nseen++] = p;
    int n = nseen;
    plat_mutex_unlock(&lock);
    if (fresh) rt_log("nvn: missing proc %s resolved by the game (%d so far)", p->name, n);
}

uint64_t nvn_proc_address(const char *name) {
    if (!name) return 0;
    const Proc *p = bsearch(&(Proc){name, 0, NULL}, procs, (size_t)nprocs, sizeof *procs, by_name);
    if (p) {
        if (!*p->has && strcmp(p->name, "nvnDeviceGetProcAddress")) proc_missing(p);
        return p->addr;
    }
    // Unknown names get NULL, as on hardware; each distinct one is logged once.
    static PlatMutex lock = PLAT_MUTEX_INIT;
    static char seen[32][96];
    static int nseen;
    plat_mutex_lock(&lock);
    int fresh = 1;
    for (int i = 0; i < nseen; i++)
        if (!strncmp(seen[i], name, sizeof seen[i] - 1)) fresh = 0;
    if (fresh && nseen < 32) {
        strncpy(seen[nseen], name, sizeof seen[0] - 1);
        nseen++;
    } else {
        fresh = 0;
    }
    plat_mutex_unlock(&lock);
    if (fresh) rt_log("nvn: no such proc \"%s\", returning NULL", name);
    return 0;
}

// PFNNVNGENERICFUNC nvnDeviceGetProcAddress(const NVNdevice *device, const char *name)
void nvnp_nvnDeviceGetProcAddress(Cpu *c) {
    const char *name = arg_p(c, 1);
    ret_x(c, nvn_proc_address(name));
}
