"""Find where the game code uses frame-time constants (1/30 s and relatives), as a report of addresses only.

The value scan of writable memory (runtime/value_scan.c) found no stored 1/30 that is the game's per-frame step: the
step is a constant of the code. This tool looks for it in the modules on disk:

- immediates: MOVZ/MOVN (+ MOVK) sequences that build one of the values in a register (the usual way clang
  materialises a float constant: mov w8, #lo; movk w8, #hi, lsl #16; fmov s0, w8), and FMOV (scalar immediate) for
  values that encoding can hold;
- literals: words in .rodata/.data that hold one of the values, and the loads that read them (LDR literal, ADRP + LDR
  with an immediate offset, ADRP + ADD), including 16-byte vector loads that cover the word.

For each value it prints the count and every site as module+offset, with the function that holds it (the lifter's
function discovery) and the kind of site. A value that a frame-step patch can replace shows up as a few sites in game
code; the report never contains instruction or data bytes, only addresses, counts and value names.

  .venv\\Scripts\\python tools\\consts.py
  .venv\\Scripts\\python tools\\consts.py --module main --module subsdk0 --show 60

--patch-file FILE writes a patch table (recomp/patches.py) with one set per immediate site of float 1/30 (to 1/60),
float 30.0 built by MOVZ/MOVK (to 60.0; FMOV #30.0 cannot hold 60.0) and 33333/33334 us (to 16667), each off by
default; tools/lift.py --patches FILE --gen-only lifts with it, and TOTK_PATCH_BISECT=1 halves the sets by key in the
running game (runtime/value_scan.c).

  .venv\\Scripts\\python tools\\consts.py --patch-file build\\fps_candidates.txt
"""

import argparse
import struct
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

EXEFS = ROOT / 'game' / 'extracted' / 'update' / 'exefs'


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def f64(x):
    return struct.unpack('<Q', struct.pack('<d', x))[0]


# (name, width in bits, bit pattern). Frame-time values at 30 fps and their 60 fps counterparts (a step built from
# 1/60 times the vsync count shows up as 1/60), in seconds, milliseconds, microseconds and nanoseconds.
VALUES = [
    ('float 1/30', 32, f32(1 / 30)), ('float 30.0', 32, f32(30.0)), ('float 1/60', 32, f32(1 / 60)),
    ('float 60.0', 32, f32(60.0)), ('float 33.333 ms', 32, f32(1000 / 30)), ('float 16.667 ms', 32, f32(1000 / 60)),
    ('double 1/30', 64, f64(1 / 30)), ('double 30.0', 64, f64(30.0)), ('double 1/60', 64, f64(1 / 60)),
    ('double 60.0', 64, f64(60.0)), ('double 33.333 ms', 64, f64(1000 / 30)), ('double 16.667 ms', 64, f64(1000 / 60)),
    ('int 33333 us', 32, 33333), ('int 33334 us', 32, 33334), ('int 16666 us', 32, 16666), ('int 16667 us', 32, 16667),
    ('int 33333333 ns', 32, 33333333), ('int 16666667 ns', 32, 16666667),
]


def sext_np(v, bits):
    v = v.astype(np.int64)
    return np.where(v & (1 << (bits - 1)), v - (1 << bits), v)


def fmov_imm(imm8, double):
    """Value of an FMOV (scalar, immediate) imm8 as a bit pattern (VFPExpandImm)."""
    sign, b6, rest = imm8 >> 7, (imm8 >> 6) & 1, imm8 & 0x3F
    if double:
        exp = ((b6 ^ 1) << 10) | ((0xFF if b6 else 0) << 2) | ((rest >> 4) & 3)
        return sign << 63 | exp << 52 | (rest & 0xF) << 48
    exp = ((b6 ^ 1) << 7) | ((0x1F if b6 else 0) << 2) | ((rest >> 4) & 3)
    return sign << 31 | exp << 23 | (rest & 0xF) << 19


class Mod:
    def __init__(self, name, base, path):
        self.name, self.base = name, base
        self.m = Module(name, path, base, 'c')
        starts, _ = discover(self.m)
        self.starts = (self.m.lo + starts * 4).tolist()

    def where(self, a):
        i = bisect_right(self.starts, a) - 1
        f = self.starts[i] if i >= 0 else None
        s = f'{self.name}+{a - self.base:#x}'
        return s + (f' in {self.name}+{f - self.base:#x} (+{a - f:#x})' if f is not None else '')


def immediates(mod):
    """{value name: [(address, kind)]} of MOVZ/MOVN/MOVK builds and FMOV immediates."""
    m = mod.m
    w = m.words.astype(np.uint64)
    n = len(w)
    out = {name: [] for name, _, _ in VALUES}
    wide = (w >> 31) & 1
    rd = w & 31
    imm16 = (w >> 5) & 0xFFFF
    hw = (w >> 21) & 3
    op = (w >> 23) & 0xFF   # sf opc 100101 for move wide; sf excluded below
    movw = ((w >> 23) & 0x3F) == 0x25
    opc = (w >> 29) & 3     # 0 MOVN, 2 MOVZ, 3 MOVK
    del op
    # One pass per value: find the instruction that sets its top non-zero half-word, then rebuild the register from
    # the move-wide instructions before it (MOVZ/MOVN start, MOVK insert) within 8 instructions.
    starts_idx = np.flatnonzero(movw & (opc != 1))
    for name, bitsz, pat in VALUES:
        halves = [(pat >> (16 * k)) & 0xFFFF for k in range(bitsz // 16)]
        top = max((k for k in range(len(halves)) if halves[k]), default=0)
        sel = starts_idx[(imm16[starts_idx] == halves[top]) & (hw[starts_idx] == top)
                         & (wide[starts_idx] == (1 if bitsz == 64 else wide[starts_idx]))]
        hits = set()
        for s in sel.tolist():
            r = int(rd[s])
            # walk back to the MOVZ/MOVN that starts this register's value
            j = s
            while j > max(0, s - 8):
                x = int(w[j])
                if ((x >> 23) & 0x3F) == 0x25 and (x & 31) == r and ((x >> 29) & 3) in (0, 2):
                    break
                j -= 1
            val, ok = None, False
            for k in range(j, min(n, s + 8)):
                x = int(w[k])
                if ((x >> 23) & 0x3F) != 0x25 or (x & 31) != r:
                    if k > s:
                        break
                    continue
                o, h, i16, sf = (x >> 29) & 3, (x >> 21) & 3, (x >> 5) & 0xFFFF, x >> 31
                mask = (1 << (64 if sf else 32)) - 1
                if o == 2:
                    val = (i16 << (16 * h)) & mask
                elif o == 0:
                    val = ~(i16 << (16 * h)) & mask
                elif o == 3 and val is not None:
                    val = (val & ~(0xFFFF << (16 * h)) | i16 << (16 * h)) & mask
                if val == pat:
                    ok = k
            if ok is not False and ok not in hits:
                hits.add(ok)
                out[name].append((m.lo + ok * 4, 'mov/movk', s))
    # FMOV (scalar, immediate): 0x1E201000 | type << 22 | imm8 << 13 | rd
    fm = np.flatnonzero((w & 0xFF201FE0) == 0x1E201000)
    for s in fm.tolist():
        x = int(w[s])
        typ, imm8 = (x >> 22) & 3, (x >> 13) & 0xFF
        if typ not in (0, 1):
            continue
        v = fmov_imm(imm8, typ == 1)
        for name, bitsz, pat in VALUES:
            if name.startswith('float' if typ == 0 else 'double') and v == pat:
                out[name].append((m.lo + s * 4, 'fmov imm', s))
    return out


def literals(mod):
    """{value name: ([data address], [(load address, kind, data address)])}."""
    m = mod.m
    img = np.frombuffer(m.image, dtype=np.uint8)
    w = m.words
    lo = m.lo
    out = {}
    regions = [m.nso.rodata, m.nso.data]
    adrp = np.flatnonzero((w & 0x9F000000) == 0x90000000)
    page = ((lo + adrp * 4) & ~0xFFF) + (sext_np(((w[adrp] >> 5) & 0x7FFFF) << 2 | ((w[adrp] >> 29) & 3), 21) << 12)
    order = np.argsort(page, kind='stable')
    page_s, adrp_s = page[order], adrp[order]
    lit = np.flatnonzero((w & 0x3B000000) == 0x18000000)   # LDR (literal), GPR and SIMD
    lit_t = lo + lit * 4 + sext_np((w[lit] >> 5) & 0x7FFFF, 19) * 4
    for name, bitsz, pat in VALUES:
        step = bitsz // 8
        addrs = []
        for off, size in regions:
            seg = img[off:off + size]
            n = size // step
            arr = np.frombuffer(seg[:n * step].tobytes(), dtype='<u4' if step == 4 else '<u8')
            addrs += (m.base + off + np.flatnonzero(arr == pat) * step).tolist()
        loads = []
        aset = set(addrs)
        for s, t in zip(lit.tolist(), lit_t.tolist()):
            if t in aset:
                loads.append((lo + s * 4, 'ldr literal', t))
        for a in addrs:
            pg = a & ~0xFFF
            i0, i1 = np.searchsorted(page_s, pg), np.searchsorted(page_s, pg, side='right')
            for s in adrp_s[i0:i1].tolist():
                r = int(w[s]) & 31
                for k in range(1, 12):
                    if s + k >= len(w):
                        break
                    x = int(w[s + k])
                    if (x & 0x3B000000) == 0x39000000 and ((x >> 5) & 31) == r and (x >> 22) & 1:   # LDR (unsigned)
                        size_ = (x >> 30) & 3
                        simd = (x >> 26) & 1
                        scale = (4 if ((x >> 23) & 1) else size_) if simd else size_
                        o = ((x >> 10) & 0xFFF) << scale
                        if o == (a & 0xFFF):
                            loads.append((lo + (s + k) * 4, 'adrp+ldr', a))
                        elif simd and scale == 4 and o == (a & 0xFF0):
                            loads.append((lo + (s + k) * 4, 'adrp+ldr q (vector)', a))
                    elif (x & 0xFFC00000) == 0x91000000 and ((x >> 5) & 31) == r and ((x >> 10) & 0xFFF) == (a & 0xFFF):
                        loads.append((lo + (s + k) * 4, 'adrp+add', a))
                    if (x & 31) == r and (x & 0x9F000000) == 0x90000000:
                        break   # the register gets a new page
        out[name] = (addrs, loads)
    return out


# The 60 fps counterpart of each value a patch candidate replaces: (new top half-word or FMOV value, set name prefix).
PATCH_TO = {'float 1/30': (f32(1 / 60), 'fs'), 'float 30.0': (f32(60.0), 'fr'),
            'int 33333 us': (16667, 'us'), 'int 33334 us': (16667, 'us')}


def patch_lines(mod, imm):
    """Patch file lines (recomp/patches.py): one set per site, switch TOTK_<SET>, default off. Each site is the
    instruction that writes the value's top half-word (MOVZ/MOVK; the lower half of 1/30 and 1/60 is the same) or the
    FMOV, rewritten to give the 60 fps value. FMOV immediates stop at 31.0, so FMOV #30.0 sites get no set (60.0 would
    need a second instruction and a scratch register)."""
    m = mod.m
    out, names = [], []
    for vname, (new, prefix) in PATCH_TO.items():
        for _, kind, s in imm[vname]:
            x = int(m.words[s])
            if kind == 'fmov imm':
                imm8 = next((i for i in range(256) if fmov_imm(i, False) == new), None)
                if imm8 is None:
                    continue
                y = (x & ~(0xFF << 13)) | imm8 << 13
            else:
                h = (x >> 21) & 3
                if new >> (16 * (h + 1)) or (vname.startswith('int') and h):
                    continue   # the new value does not fit the same instruction
                y = (x & ~(0xFFFF << 5)) | ((new >> (16 * h)) & 0xFFFF) << 5
            off = m.lo + s * 4 - mod.base
            name = f'{prefix}_{off:x}'
            if name in names:
                continue
            names.append(name)
            out.append(f'set  {name} TOTK_{name.upper()} 0')
            out.append(f'site {name} {mod.name} {off:x} {x:08x} {y:08x}')
    return out, names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--module', action='append', default=[], help='module to search (default: main)')
    ap.add_argument('--show', type=int, default=40, help='sites listed per value and kind (default 40)')
    ap.add_argument('--patch-file', type=Path, help='write a patch table with one switchable set per immediate site of '
                    'float 1/30, float 30.0 and 33333 us (for gen.py --patches; keep it under build/, it holds '
                    'instruction words)')
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    want = set(a.module or ['main'])
    patch = []
    print('=' * 72)
    for name, base, _ in layout(a.exefs):
        if name not in want:
            continue
        mod = Mod(name, base, a.exefs / name)
        imm = immediates(mod)
        lit = literals(mod)
        if a.patch_file:
            lines, names = patch_lines(mod, imm)
            patch += lines
            print(f'module {name}: {len(names)} patch sets for {a.patch_file}')
        print(f'module {name}: {len(mod.starts)} functions')
        for vname, _, _ in VALUES:
            sites = imm[vname]
            addrs, loads = lit[vname]
            print(f'  {vname}: {len(sites)} immediate builds, {len(addrs)} words in .rodata/.data, '
                  f'{len(loads)} loads of them')
            for s, kind, _ in sites[:a.show]:
                print(f'    {kind:10s} {mod.where(s)}')
            if len(sites) > a.show:
                print(f'    ... {len(sites) - a.show} more immediate builds')
            for s, kind, t in loads[:a.show]:
                print(f'    {kind:10s} {mod.where(s)} reads {name}+{t - base:#x}')
            if len(loads) > a.show:
                print(f'    ... {len(loads) - a.show} more loads')
            unread = sorted(set(addrs) - {t for _, _, t in loads})
            if unread:
                print(f'    words without a found load: {len(unread)}'
                      + (f' (first {", ".join(f"{name}+{t - base:#x}" for t in unread[:6])})'))
    if a.patch_file:
        a.patch_file.parent.mkdir(parents=True, exist_ok=True)
        a.patch_file.write_text('# frame-step candidates from tools/consts.py --patch-file (holds instruction words: '
                                'not for the repository)\n' + '\n'.join(patch) + '\n')
        print(f'patch table: {len(patch) // 2} sets written to {a.patch_file}')
    print('=' * 72)


if __name__ == '__main__':
    main()
