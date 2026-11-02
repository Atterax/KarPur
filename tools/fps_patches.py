"""Write the patch table for Violet's frame-rate experiments (recomp/patches.py format) from the game's own main.

The table goes to build/ (it holds instruction words of the game, so it stays out of the repository). Every site is
checked against the instruction it expects (register and immediate), so a different game version stops here with a
report instead of producing a broken build.

Sets (switched with their TOTK_ variable):

  fps_dt   (on by default) main+0x92dd2c: the thunk main+0x92dd20 (tail call of vtable slot 2 of the object in x0) gets x2 = the
           frame time in ns from the patch page +64, which runtime/frame_dt.c fills with the measured frame time
           (the game passes 33333334 there at 30 fps; the third-party mod sets the same register). With it on, the
           runtime paces frames to the menu's frame limit (TOTK_FPS_LIMIT, 30..360 or display) and the game keeps
           its speed at any rate (confirmed at 60 fps, docs/progress.md 2026-11-02).

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
    """Short text of a 64-bit LDR (immediate) or move-wide word for the mismatch report."""
    if (w & 0xFFC00000) == 0xF9400000:
        return f'ldr x{w & 31}, [x{(w >> 5) & 31}, #{((w >> 10) & 0xFFF) * 8:#x}]'
    if (w & 0x1F800000) == 0x12800000:
        name = {0: 'movn', 2: 'movz', 3: 'movk'}.get((w >> 29) & 3, 'move-wide')
        return f'{name} {"x" if w >> 31 else "w"}{w & 31}, #{(w >> 5) & 0xFFFF:#x} lsl {16 * ((w >> 21) & 3)}'
    return 'another kind of instruction'


def ldr_x(rt, rn, off):
    return 0xF9400000 | (off // 8) << 10 | rn << 5 | rt


PAGE_FRAME_NS = 64   # patch page byte offset of the frame time in ns (runtime/frame_dt.c; recomp/patches.py layout)

# (set, switch, default, module offset, expected word, replacement word or words; '@page:x2' is adrp x2 of the
# patch page)
SITES = [
    ('fps_dt', 'TOTK_FPS_DT', 1, 0x92dd2c, ldr_x(8, 0, 0), [ldr_x(8, 0, 0), '@page:x2', ldr_x(2, 2, PAGE_FRAME_NS)]),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'fps_patches.txt')
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    base = next(b for n, b, _ in layout(a.exefs) if n == 'main')
    m = Module('main', a.exefs / 'main', base, 'c')
    lines, sets, bad = ['# frame-rate patch sets from tools/fps_patches.py (holds instruction words: not for the '
                        'repository)'], [], 0
    for name, env, default, off, want, repl in SITES:
        have = m.word(base + off)
        ok = have == want
        bad += not ok
        print(f'main+{off:#x}: expected {describe(want)}, found {describe(have)}: {"ok" if ok else "MISMATCH"}')
        if name not in sets:
            sets.append(name)
            lines.append(f'set  {name} {env} {default}')
        words = repl if isinstance(repl, list) else [repl]
        text = ' '.join(r if isinstance(r, str) else f'{r:08x}' for r in words)
        lines.append(f'site {name} main {off:x} {want:08x} {text}')
    if bad:
        sys.exit(f'{bad} sites differ from what this table expects (another game version?): nothing written')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n')
    print(f'patch table: {len(sets)} sets, {len(SITES)} sites written to {a.out}')


if __name__ == '__main__':
    main()
