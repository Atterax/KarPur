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
           from the patch page +72 instead (adrp x17 + ldr s0), which runtime/lod.c fills from TOTK_LOD_BIAS (default
           -15). x17 is free there: the check below finds no use of x16/x17 between the load and the call.

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


def ip_free(m, a):
    """No instruction from the one after a up to the next call names x16 or x17 in any register field (a coarse check:
    immediates may look like them and then the site is refused, never wrongly accepted)."""
    for k in range(1, 12):
        w = m.word(a + 4 * k)
        if (w & 0xFC000000) == 0x94000000 or (w & 0xFFFFFC1F) == 0xD63F0000:
            return True
        if any(((w >> sh) & 31) in (16, 17) for sh in (0, 5, 10, 16)):
            return False
    return False

# (set, switch, default, module offset, expected word, replacement word or words; '@page:x2' is adrp x2 of the
# patch page)
SITES = [
    ('fps_dt', 'TOTK_FPS_DT', 1, 0x92dd2c, ldr_x(8, 0, 0), [ldr_x(8, 0, 0), '@page:x2', ldr_x(2, 2, PAGE_FRAME_NS)]),
    ('lod_bias', 'TOTK_LOD', 0, 0xc07dc4, ldr_s(0, 19, 0x1c), ['@page:x17', ldr_s(0, 17, PAGE_LOD_BIAS)]),
    ('lod_bias', 'TOTK_LOD', 0, 0x7f1a6c, ldr_s(0, 20, 0x10), ['@page:x17', ldr_s(0, 17, PAGE_LOD_BIAS)]),
]
OPTIONAL = {'lod_bias'}   # a mismatch drops the set with a note instead of stopping


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
    dropped, bad = set(), 0
    for name, env, default, off, want, repl in SITES:
        have = m.word(base + off)
        ok = have == want
        free = ok and (name != 'lod_bias' or ip_free(m, base + off))
        note = 'ok' if free else ('x16/x17 used before the call' if ok else 'MISMATCH')
        print(f'main+{off:#x}: expected {describe(want)}, found {describe(have)}: {note}')
        if not free:
            if name in OPTIONAL:
                dropped.add(name)
            else:
                bad += 1
    if bad:
        sys.exit(f'{bad} sites differ from what this table expects (another game version?): nothing written')
    for name in sorted(dropped):
        print(f'set {name} left out (its sites differ from what this table expects)')
    lines, sets, n = ['# frame-rate and LOD patch sets from tools/fps_patches.py (holds instruction words: not for the '
                      'repository)'], [], 0
    for name, env, default, off, want, repl in SITES:
        if name in dropped:
            continue
        if name not in sets:
            sets.append(name)
            lines.append(f'set  {name} {env} {default}')
        words = repl if isinstance(repl, list) else [repl]
        text = ' '.join(r if isinstance(r, str) else f'{r:08x}' for r in words)
        lines.append(f'site {name} main {off:x} {want:08x} {text}')
        n += 1
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n')
    print(f'patch table: {len(sets)} sets, {n} sites written to {a.out}')


if __name__ == '__main__':
    main()
