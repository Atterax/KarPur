"""List the calls a guest function makes, as a report: for crash analysis without sharing game code.

For each address (as karpur.log prints them, e.g. main+0x14fe804) the tool finds the function that contains it, using
the same function discovery as the lifter (recomp/gen.py discover), and lists the calls the function makes in order:
offset in the function, target (SDK symbol name for imports and exports, else module+offset) and the size of a
directly called game function. The line of the given address is marked with <==. Indirect calls (BLR) and tail
branches out of the function are listed too. Output: addresses, sizes and symbol names only, never instruction bytes.

  .venv\\Scripts\\python tools\\calls.py main+0x11bcec8 main+0x14fe804

--before / --after limit how many calls around the marked address are printed (default 40 / 15). For the start
address of a function every call is listed.
"""

import argparse
import sys
from bisect import bisect_right
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'recomp'))

from gen import discover  # noqa: E402
from layout import layout  # noqa: E402
from module import Module  # noqa: E402
from nso import R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT  # noqa: E402

EXEFS = ROOT / 'game' / 'extracted' / 'update' / 'exefs'


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Mod:
    def __init__(self, name, base, path):
        self.name, self.base = name, base
        self.m = Module(name, path, base, 'c')
        starts, _ = discover(self.m)
        self.starts = (self.m.lo + starts * 4).tolist()
        n = self.m.nso
        # GOT slot -> imported symbol, for decoding PLT stubs
        self.got = {}
        for off, typ, sym, _ in n.relocs:
            if typ in (R_AARCH64_JUMP_SLOT, R_AARCH64_GLOB_DAT) and sym and sym < len(n.syms) and n.syms[sym][0]:
                self.got[base + off] = n.syms[sym][0]
        # exported function symbols of this module
        self.exports = {base + s[3]: s[0] for s in n.exports() if (s[1] & 0xF) == 2}

    def function(self, a):
        i = bisect_right(self.starts, a) - 1
        if i < 0:
            return None
        hi = self.starts[i + 1] if i + 1 < len(self.starts) else self.m.hi
        return self.starts[i], hi

    def plt_target(self, a):
        """Symbol a PLT stub at a jumps to (ADRP x16; LDR x17, [x16, #imm]; ...; BR x17), else None."""
        if not self.m.in_text(a) or not self.m.in_text(a + 12):
            return None
        w0, w1, w3 = self.m.word(a), self.m.word(a + 4), self.m.word(a + 12)
        if (w0 & 0x9F00001F) != 0x90000010 or (w1 & 0xFFC003FF) != 0xF9400211 or w3 != 0xD61F0220:
            return None
        imm = ((w0 >> 29) & 3) | (((w0 >> 5) & 0x7FFFF) << 2)
        page = (a & ~0xFFF) + (sext(imm, 21) << 12)
        return self.got.get(page + ((w1 >> 10) & 0xFFF) * 8)

    def label(self, t):
        if t in self.exports:
            return self.exports[t]
        sym = self.plt_target(t)
        if sym:
            return sym
        return f'{self.name}+{t - self.base:#x}'


def parse(addr, mods):
    if '+' not in addr:
        sys.exit(f'{addr}: expected module+offset, e.g. main+0x14fe804')
    name, off = addr.split('+', 1)
    if name not in mods:
        sys.exit(f'{addr}: no module {name} (have {", ".join(mods)})')
    return mods[name], mods[name].base + int(off, 16)


def report(mod, a, before, after):
    f = mod.function(a)
    if not f:
        print(f'{mod.name}+{a - mod.base:#x}: not inside a function')
        return
    lo, hi = f
    print(f'{mod.name}+{a - mod.base:#x}: in function {mod.name}+{lo - mod.base:#x} ({(hi - lo) // 4} instructions)')
    rows, mark = [], None
    for p in range(lo, hi, 4):
        w = mod.m.word(p)
        kind = None
        if (w & 0xFC000000) == 0x94000000:            # BL
            t = p + sext(w & 0x3FFFFFF, 26) * 4
            kind = 'call'
        elif (w & 0xFC000000) == 0x14000000:          # B: only branches leaving the function (tail calls)
            t = p + sext(w & 0x3FFFFFF, 26) * 4
            if lo <= t < hi:
                continue
            kind = 'tail'
        elif (w & 0xFFFFFC1F) == 0xD63F0000:          # BLR Xn
            kind, t = 'indirect', None
        elif (w & 0xFFFFFC1F) == 0xD61F0000 and ((w >> 5) & 31) not in (30,):   # BR Xn (not a return)
            kind, t = 'branch-reg', None
        if not kind:
            continue
        if t is None:
            what = f'x{(w >> 5) & 31}'
        else:
            what = mod.label(t)
            if what.startswith(mod.name + '+'):
                g = mod.function(t)
                if g and g[0] == t:
                    what += f' ({(g[1] - g[0]) // 4} instructions)'
        here = p + 4 == a or p == a   # a frame's return address is the instruction after the call
        if here:
            mark = len(rows)
        rows.append(f'  +{p - lo:#07x}  {kind:10} {what}{"  <==" if here else ""}')
    if mark is None and a == lo:
        # The start of a function: list all of its calls.
        print('  (function start: all calls)')
        for r in rows:
            print(r)
        return
    if mark is None:
        mark = len(rows)
        print('  (no call at the given address; listing the calls before it)')
        rows = [r for r in rows if int(r.split()[0][1:], 16) + lo < a] + [f'  {"":8} <== {mod.name}+{a - mod.base:#x}']
        mark = len(rows) - 1
    first = max(0, mark - before)
    if first:
        print(f'  ... {first} earlier calls')
    for r in rows[first:mark + after + 1]:
        print(r)
    if mark + after + 1 < len(rows):
        print(f'  ... {len(rows) - mark - after - 1} later calls')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addresses', nargs='+', help='module+offset as karpur.log prints them')
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--before', type=int, default=40)
    ap.add_argument('--after', type=int, default=15)
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    want = {s.split('+', 1)[0] for s in a.addresses}
    mods = {}
    for name, base, _ in layout(a.exefs):
        if name in want:
            mods[name] = Mod(name, base, a.exefs / name)
    print('=' * 72)
    for s in a.addresses:
        mod, addr = parse(s, mods)
        report(mod, addr, a.before, a.after)
        print()
    print('=' * 72)


if __name__ == '__main__':
    main()
