"""Write the patch table for Violet's frame-rate and LOD patches (recomp/patches.py format) from the game's own main.

The table goes to build/ (it holds instruction words of the game, so it stays out of the repository). Every site is
checked against the instruction it expects (register and immediate), so a different game version stops here with a
report instead of producing a broken build.

Sets (switched with their TOTK_ variable):

  fps_dt   (on by default) main+0x92dd2c: the thunk main+0x92dd20 (tail call of vtable slot 2 of the object in x0) gets x2 = the
           frame time in ns from the patch page +64, which runtime/frame_dt.c fills with the measured frame time
           (the game passes 33333334 there at 30 fps; the third-party mod sets the same register). With it on, the
           runtime paces frames to the menu's frame limit (TOTK_FPS_LIMIT, 30..360 or display) and the game keeps
           its speed at any rate (confirmed at 60 fps, docs/progress.md 2026-11-02).

  lod_bias (off by default; TOTK_LOD=1) main+0xc07dc4 and main+0x7f1a6c: each loads a float (ldr s0, [x19, #0x1c] /
           [x20, #0x10]) that a virtual call right after receives as its argument; a third-party mod for Violet 3.0.1
           replaces both loads with fmov s0, #-15.0 and calls it "LodBias" (read for behaviour only). The set loads s0
           from the patch page +72 instead (adrp + ldr s0 through a scratch register from x9-x17 that no instruction
           between the load and the call names; the call clobbers it), which runtime/lod.c fills from TOTK_LOD_BIAS
           (default -15).

  data_watch (off by default; TOTK_DATA_WATCH=1) every direct call (bl) of main+0xcc8750, the game's Oodle decoder
           (OodleLZ_Decompress: the function names that string), goes to a host function at patch page +0xfd0
           (runtime/data_watch.c), which runs the decoder and then looks at what it unpacked: the game's data files
           (arc/data.trpfs) are Oodle Kraken data. Checked: the function at that offset must reference the string
           "OodleLZ_Decompress"; tail calls (b) to it are counted and left alone.

Tried and dropped: the 30 fps frame time and vsync count in main+0x2b79f60 (no effect on the interval or the speed).

tools/lift.py uses build/fps_patches.txt by itself when the file exists.
Background: docs/progress.md 2026-11-02 (a third-party frame-rate mod for Violet, read for behaviour only, and
tools/consts.py / calls.py --insn reports of 4.0.0).

  .venv\\Scripts\\python tools\\fps_patches.py
  .venv\\Scripts\\python tools\\lift.py --patches build\\fps_patches.txt --gen-only
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'recomp'))

from layout import layout  # noqa: E402
from module import Module  # noqa: E402

EXEFS = ROOT / 'game' / 'extracted' / 'update' / 'exefs'


def describe(w):
    """Short text of a 64-bit or SIMD 32-bit LDR (immediate) or move-wide word for the mismatch report."""
    if (w & 0xFFC00000) == 0xBD400000:
        return f'ldr s{w & 31}, [x{(w >> 5) & 31}, #{((w >> 10) & 0xFFF) * 4:#x}]'
    if (w & 0xFFC00000) == 0xF9400000:
        return f'ldr x{w & 31}, [x{(w >> 5) & 31}, #{((w >> 10) & 0xFFF) * 8:#x}]'
    if (w & 0x1F800000) == 0x12800000:
        name = {0: 'movn', 2: 'movz', 3: 'movk'}.get((w >> 29) & 3, 'move-wide')
        return f'{name} {"x" if w >> 31 else "w"}{w & 31}, #{(w >> 5) & 0xFFFF:#x} lsl {16 * ((w >> 21) & 3)}'
    return 'another kind of instruction'


def ldr_x(rt, rn, off):
    return 0xF9400000 | (off // 8) << 10 | rn << 5 | rt


PAGE_FRAME_NS = 64   # patch page byte offset of the frame time in ns (runtime/frame_dt.c; recomp/patches.py layout)
PAGE_LOD_BIAS = 72   # patch page byte offset of the LOD bias float (runtime/lod.c)


def ldr_s(rt, rn, off):
    return 0xBD400000 | (off // 4) << 10 | rn << 5 | rt


SCRATCH = (17, 16, 15, 14, 13, 12, 11, 10, 9)   # registers a call clobbers that are no argument (x9-x17)


def free_reg(m, a):
    """A register from SCRATCH that no instruction after a names (any register field) before the next call, or None.
    The call clobbers it, so the patch may use it; the field check is coarse: an immediate that looks like the register
    only makes it pick another one."""
    window = []
    for k in range(1, 12):
        w = m.word(a + 4 * k)
        if (w & 0xFC000000) == 0x94000000 or (w & 0xFFFFFC1F) == 0xD63F0000:
            break
        window.append(w)
    else:
        return None   # no call within reach: nothing is known to be free
    for r in SCRATCH:
        if not any(((w >> sh) & 31) == r for w in window for sh in (0, 5, 10, 16)):
            return r
    return None


# (set, switch, default, module offset, expected word, replacement word or words; '@page:x2' is adrp x2 of the
# patch page)
SITES = [
    ('fps_dt', 'TOTK_FPS_DT', 1, 0x92dd2c, ldr_x(8, 0, 0), [ldr_x(8, 0, 0), '@page:x2', ldr_x(2, 2, PAGE_FRAME_NS)]),
    # 'scratch': adrp into a free register from SCRATCH, then ldr s0 from the patch page through it (main picks it)
    ('lod_bias', 'TOTK_LOD', 0, 0xc07dc4, ldr_s(0, 19, 0x1c), 'scratch'),
    ('lod_bias', 'TOTK_LOD', 0, 0x7f1a6c, ldr_s(0, 20, 0x10), 'scratch'),
]
OPTIONAL = {'lod_bias', 'data_watch'}
OODLE_DECOMPRESS = 0xcc8750   # main offset of OodleLZ_Decompress in 4.0.0 (calls.py --string oodle, 2026-11-02)
PAGE_OODLE_HOST = 0xfd0       # patch page offset of the host wrapper (runtime/data_watch.c)


def names_string(m, base, func, text, span=0x600):
    """True when an ADRP+ADD in the first span bytes of the function at base+func computes the address of the
    NUL-terminated string text."""
    img = m.image
    at = img.find(b'\0' + text.encode() + b'\0')
    if at < 0:
        return False
    target = base + at + 1
    for k in range(0, span, 4):
        a = base + func + k
        if not m.in_text(a + 4):
            break
        w = m.word(a)
        if (w & 0x9F000000) != 0x90000000:
            continue
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2)
        imm -= (1 << 21) if imm & (1 << 20) else 0
        if (a & ~0xFFF) + (imm << 12) != target & ~0xFFF:
            continue
        for j in range(1, 6):
            x = m.word(a + 4 * j)
            if (x & 0xFFC00000) == 0x91000000 and ((x >> 5) & 31) == (w & 31) and ((x >> 10) & 0xFFF) == target & 0xFFF:
                return True
    return False


def direct_calls(m, target):
    """(bl sites, b sites) in main's .text that branch to target."""
    import numpy as np
    w = m.words
    idx = np.arange(len(w), dtype=np.int64)
    off = (w & 0x03FFFFFF).astype(np.int64)
    off = np.where(off & 0x2000000, off - 0x4000000, off)
    hit = (m.lo + (idx + off) * 4) == target
    bl = np.flatnonzero(hit & ((w & 0xFC000000) == 0x94000000))
    b = np.flatnonzero(hit & ((w & 0xFC000000) == 0x14000000))
    return [m.lo + 4 * int(i) for i in bl], [m.lo + 4 * int(i) for i in b]   # a mismatch drops the set with a note instead of stopping


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'fps_patches.txt')
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    base = next(b for n, b, _ in layout(a.exefs) if n == 'main')
    m = Module('main', a.exefs / 'main', base, 'c')
    # Check every site first: an optional set with any failing site is left out whole.
    dropped, bad, words_of = set(), 0, {}
    for name, env, default, off, want, repl in SITES:
        have = m.word(base + off)
        ok = have == want
        note = 'ok' if ok else 'MISMATCH'
        if ok and repl == 'scratch':
            r = free_reg(m, base + off)
            if r is None:
                ok, note = False, 'no free scratch register before the call'
            else:
                repl, note = [f'@page:x{r}', ldr_s(0, r, PAGE_LOD_BIAS)], f'ok (scratch x{r})'
        words_of[off] = repl
        print(f'main+{off:#x}: expected {describe(want)}, found {describe(have)}: {note}')
        if not ok:
            if name in OPTIONAL:
                dropped.add(name)
            else:
                bad += 1
    # data_watch: every bl of the Oodle decoder
    if names_string(m, base, OODLE_DECOMPRESS, 'OodleLZ_Decompress'):
        calls, tails = direct_calls(m, base + OODLE_DECOMPRESS)
        print(f'main+{OODLE_DECOMPRESS:#x}: references "OodleLZ_Decompress": ok; {len(calls)} calls (bl), '
              f'{len(tails)} tail calls (b, left alone)')
        for site in calls:
            off = site - base
            SITES.append(('data_watch', 'TOTK_DATA_WATCH', 0, off, m.word(site), [f'@pagebl:{PAGE_OODLE_HOST:x}']))
            words_of[off] = SITES[-1][5]
        if not calls:
            dropped.add('data_watch')
    else:
        print(f'main+{OODLE_DECOMPRESS:#x}: does not reference "OodleLZ_Decompress": set data_watch left out')
        dropped.add('data_watch')
    if bad:
        sys.exit(f'{bad} sites differ from what this table expects (another game version?): nothing written')
    for name in sorted(dropped):
        print(f'set {name} left out (one of its sites failed the check above)')
    lines, sets, n = ['# frame-rate and LOD patch sets from tools/fps_patches.py (holds instruction words: not for the '
                      'repository)'], [], 0
    for name, env, default, off, want, _ in SITES:
        if name in dropped:
            continue
        if name not in sets:
            sets.append(name)
            lines.append(f'set  {name} {env} {default}')
        repl = words_of[off]
        words = repl if isinstance(repl, list) else [repl]
        text = ' '.join(r if isinstance(r, str) else f'{r:08x}' for r in words)
        lines.append(f'site {name} main {off:x} {want:08x} {text}')
        n += 1
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n')
    print(f'patch table: {len(sets)} sets, {n} sites written to {a.out}')


if __name__ == '__main__':
    main()
