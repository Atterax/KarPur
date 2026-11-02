"""Write the patch table for Violet's frame-rate experiments (recomp/patches.py format) from the game's own main.

The table goes to build/ (it holds instruction words of the game, so it stays out of the repository). Every site is
checked against the instruction it expects (register and immediate), so a different game version stops here with a
report instead of producing a broken build.

Sets (all off by default, each switched with its TOTK_ variable):

  fps_ns   main+0x2b7a7f8/+0x2b7a7fc: the 33333334 ns frame time the frame-mode function main+0x2b79f60 picks for
           30 fps becomes 16666667 ns (its 60 fps value; same register w10)
  fps_iv   main+0x2b7a808: the vsync count 2 that goes with it becomes 1

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


def movz(rd, imm, hw=0, x=False):
    return (0xD2800000 if x else 0x52800000) | hw << 21 | imm << 5 | rd


def movk(rd, imm, hw=0, x=False):
    return (0xF2800000 if x else 0x72800000) | hw << 21 | imm << 5 | rd


def describe(w):
    """Short text of a move-wide word for the mismatch report (no other instruction kinds are expected here)."""
    if (w & 0x1F800000) == 0x12800000:
        name = {0: 'movn', 2: 'movz', 3: 'movk'}.get((w >> 29) & 3, 'move-wide')
        return f'{name} {"x" if w >> 31 else "w"}{w & 31}, #{(w >> 5) & 0xFFFF:#x} lsl {16 * ((w >> 21) & 3)}'
    return 'another kind of instruction'


# (set, switch, module offset, expected word, replacement word)
SITES = [
    ('fps_ns', 'TOTK_FPS_NS', 0x2b7a7f8, movz(10, 0xa056), movz(10, 0x502b)),
    ('fps_ns', 'TOTK_FPS_NS', 0x2b7a7fc, movk(10, 0x1fc, 1), movk(10, 0xfe, 1)),
    ('fps_iv', 'TOTK_FPS_IV', 0x2b7a808, movz(9, 2), movz(9, 1)),
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
    for name, env, off, want, repl in SITES:
        have = m.word(base + off)
        ok = have == want
        bad += not ok
        print(f'main+{off:#x}: expected {describe(want)}, found {describe(have)}: {"ok" if ok else "MISMATCH"}')
        if name not in sets:
            sets.append(name)
            lines.append(f'set  {name} {env} 0')
        lines.append(f'site {name} main {off:x} {want:08x} {repl:08x}')
    if bad:
        sys.exit(f'{bad} sites differ from what this table expects (another game version?): nothing written')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n')
    print(f'patch table: {len(sets)} sets, {len(SITES)} sites written to {a.out}')


if __name__ == '__main__':
    main()
