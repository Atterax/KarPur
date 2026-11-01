// setjmp/longjmp of guest code. Written for KarPur (MIT).
//
// A guest longjmp returns to its setjmp a second time, in the middle of the lifted function that called setjmp. The
// generator (recomp/gen.py, setjmp sites) lifts each call to setjmp as `if (!RT_SETJMP(c)) <call>; <reload>`: a host
// setjmp in that lifted function's own frame, which lives as long as the guest frame does. runtime/setjmp_host.c
// records the guest registers setjmp saves, and its longjmp override restores them, sets x0 and returns there with a
// host longjmp. Included first in every generated chunk that has a setjmp site.
#pragma once
#define __USE_MINGW_SETJMP_NON_SEH   // setjmp(b) = _setjmp(b, NULL): longjmp restores registers, no SEH unwinding
#include <setjmp.h>

#include "cpu.h"

jmp_buf *rt_setjmp_slot(Cpu *c);   // records the guest state for the jmp_buf at x0; the host jmp_buf to set
#define RT_SETJMP(c) setjmp(*rt_setjmp_slot(c))
