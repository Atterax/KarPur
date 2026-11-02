"""List the calls a guest function makes, as a report: for crash analysis without sharing game code.

For each address (as karpur.log prints them, e.g. main+0x14fe804) the tool finds the function that contains it, using
the same function discovery as the lifter (recomp/gen.py discover), and lists the calls the function makes in order:
offset in the function, target (SDK symbol name for imports and exports, else module+offset) and the size of a
directly called game function. The line of the given address is marked with <==. Indirect calls (BLR) and tail
branches out of the function are listed too. Output: addresses, sizes and symbol names only, never instruction bytes.

  .venv\\Scripts\\python tools\\calls.py main+0x11bcec8 main+0x14fe804

--before / --after limit how many calls around the marked address are printed (default 40 / 15). For the start
address of a function every call is listed.

--refs also lists where an address is referenced, for "not a function start" crashes (the lifter did not see the
address as a function): direct branches to it, ADR and ADRP+ADD that compute it (and whether discovery skipped them as
jump-table anchors), RELATIVE relocations and .eh_frame entries for it, and whether it directly follows a call (a
return address). For an indirect call or branch at or right before the address it names where the register was last
set in the function (load base and offset, address computation, or copy), as offsets and register numbers only.

  .venv\\Scripts\\python tools\\calls.py --refs main+0x2a1fc24 main+0xfc3968

--origin MOD+OFF:xN names where register xN was last set before that instruction (constant, load base and offset,
address computation, copy), e.g. the argument of a call at its BL/BLR.

  .venv\\Scripts\\python tools\\calls.py main+0xbcc044 --origin main+0xbcc040:x1

--insn MOD+OFF prints the class of each instruction around MOD+OFF (--around N, default 4): move-wide immediates,
FMOV immediates with their value, loads and stores with base register and offset, integer/float conversions, FP
arithmetic, calls and branches. Registers and immediates only, never instruction words.

  .venv\\Scripts\\python tools\\calls.py --insn main+0xd058dc

--string TEXT lists the NUL-terminated strings of the module that contain TEXT (case-insensitive, at most 40) with
their offset and the functions that compute their address (ADRP+ADD, ADR): to find library code such as a decoder by
its error messages.

  .venv\\Scripts\\python tools\\calls.py --string oodle --string kraken
"""

import argparse
import re
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
from nso import R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT, R_AARCH64_RELATIVE  # noqa: E402

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


def word_kind(w):
    """Short class of an instruction word, for the reports (no operands beyond branch kinds)."""
    if (w & 0xFC000000) == 0x94000000:
        return 'BL'
    if (w & 0xFFFFFC1F) == 0xD63F0000:
        return f'BLR x{(w >> 5) & 31}'
    if (w & 0xFFFFFC1F) == 0xD61F0000:
        return f'BR x{(w >> 5) & 31}'
    if (w & 0xFFFFFC1F) == 0xD65F0000:
        return 'RET'
    if (w & 0xFC000000) == 0x14000000:
        return 'B'
    return 'other'


def reg_origin(mod, lo, p, r, depth=0):
    """Where register x<r> was last written before p inside [lo, p), as text (scans back up to 64 instructions)."""
    for q in range(p - 4, max(lo, p - 256) - 4, -4):
        w = mod.m.word(q)
        rel = f'+{q - lo:#x}'
        if (w & 0x9F000000) == 0x90000000 and (w & 31) == r:
            imm = sext(((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3), 21)
            return f'{rel}: ADRP page {mod.name}+{((q & ~0xFFF) + (imm << 12)) - mod.base:#x}'
        if (w & 0x9F000000) == 0x10000000 and (w & 31) == r:
            imm = sext(((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3), 21)
            return f'{rel}: ADR {mod.name}+{q + imm - mod.base:#x}'
        if (w & 0x7FE00800) == 0x1A800000 and (w & 31) == r:          # CSEL / CSINC Wd, Wn, Wm, cond
            n, m2, inc = (w >> 5) & 31, (w >> 16) & 31, (w >> 10) & 1
            cond = ['eq', 'ne', 'hs', 'lo', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al', 'nv'][(w >> 12) & 15]
            if depth > 1:
                return f'{rel}: {"csinc" if inc else "csel"} x{n}, x{m2}, {cond}'
            a_ = 'zero' if n == 31 else reg_origin(mod, lo, q, n, depth + 1)
            b_ = ('one (zero + 1)' if inc else 'zero') if m2 == 31 else reg_origin(mod, lo, q, m2, depth + 1) + (' + 1' if inc else '')
            return f'{rel}: if {cond} then [{a_}] else [{b_}]'
        if (w & 0x7F800000) == 0x52800000 and (w & 31) == r:          # MOVZ Wd/Xd, #imm16, LSL #hw
            return f'{rel}: constant {((w >> 5) & 0xFFFF) << (16 * ((w >> 21) & 3)):#x}'
        if (w & 0x7F800000) == 0x12800000 and (w & 31) == r:          # MOVN
            v = ~(((w >> 5) & 0xFFFF) << (16 * ((w >> 21) & 3))) & (0xFFFFFFFFFFFFFFFF if w >> 31 else 0xFFFFFFFF)
            return f'{rel}: constant {v:#x}'
        if (w & 0x7F8003E0) == 0x320003E0 and (w & 31) == r:          # ORR Wd/Xd, WZR, #bitmask (MOV alias)
            return f'{rel}: constant (bitmask immediate)'
        if (w & 0xFFC00000) == 0xB9400000 and (w & 31) == r:          # LDR Wt, [Xn, #imm*4]
            n, off = (w >> 5) & 31, ((w >> 10) & 0xFFF) * 4
            base = 'sp' if n == 31 else f'x{n}'
            more = '' if depth or n == 31 else f'; x{n} from {reg_origin(mod, lo, q, n, depth + 1)}'
            return f'{rel}: load 4 bytes from [{base} + {off:#x}]{more}'
        if (w & 0xFFC00000) == 0xF9400000 and (w & 31) == r:          # LDR Xt, [Xn, #imm*8]
            n, off = (w >> 5) & 31, ((w >> 10) & 0xFFF) * 8
            base = 'sp' if n == 31 else f'x{n}'
            more = '' if depth or n == 31 else f'; x{n} from {reg_origin(mod, lo, q, n, depth + 1)}'
            return f'{rel}: load 8 bytes from [{base} + {off:#x}]{more}'
        if (w & 0xFFE00C00) == 0xF8600800 and (w & 31) == r:          # LDR Xt, [Xn, Xm{, ext}]
            return f'{rel}: load 8 bytes from [x{(w >> 5) & 31} + x{(w >> 16) & 31} (scaled/extended)]'
        if (w & 0xFFC00000) == 0x91000000 and (w & 31) == r:          # ADD Xd, Xn, #imm
            n = (w >> 5) & 31
            imm = ((w >> 10) & 0xFFF) << (12 if (w >> 22) & 1 else 0)
            if n == r and not depth:
                prev = reg_origin(mod, lo, q, n, depth + 1)
                return f'{rel}: add {imm:#x} to x{n} ({prev})'
            return f'{rel}: x{n} + {imm:#x}'
        if (w & 0xFF200000) == 0x8B000000 and (w & 31) == r:          # ADD Xd, Xn, Xm{, shift}
            return f'{rel}: x{(w >> 5) & 31} + x{(w >> 16) & 31} (shifted register)'
        if (w & 0xFFE00000) == 0x8B200000 and (w & 31) == r:          # ADD Xd, Xn, Wm, ext (relative tables)
            return f'{rel}: x{(w >> 5) & 31} + x{(w >> 16) & 31} (extended register: relative table?)'
        if (w & 0xFFE0FFE0) == 0xAA0003E0 and (w & 31) == r:          # MOV Xd, Xm
            m = (w >> 16) & 31
            if not depth:
                return f'{rel}: copy of x{m} ({reg_origin(mod, lo, q, m, depth + 1)})'
            return f'{rel}: copy of x{m}'
        if (w & 0x3B000000) == 0x39000000 and (w >> 22) & 1 and (w & 31) == r:
            return f'{rel}: other load'
        if (w & 0x3FC00000) == 0x29400000 and ((w & 31) == r or ((w >> 10) & 31) == r):   # LDP
            return f'{rel}: load pair from [x{(w >> 5) & 31}]'
        if (w & 0xFC000000) == 0x94000000 or (w & 0xFFFFFC1F) == 0xD63F0000:
            if r <= 18 or r == 30:
                return f'{rel}: result or clobber of a call'
    return 'not set in the 64 instructions before it'


FP2 = {0: 'fmul', 1: 'fdiv', 2: 'fadd', 3: 'fsub', 4: 'fmax', 5: 'fmin', 6: 'fmaxnm', 7: 'fminnm', 8: 'fnmul'}


def insn_kind(mod, a):
    """Coarse class of the instruction at a with its registers and immediates, for --insn: enough to tell which
    register a value goes to and how it is used, without printing instruction words."""
    w = mod.m.word(a)
    rd, rn, rm = w & 31, (w >> 5) & 31, (w >> 16) & 31
    x = 'x' if w >> 31 else 'w'
    if (w & 0x1F800000) == 0x12800000:   # move wide
        opc, hw, imm = (w >> 29) & 3, (w >> 21) & 3, (w >> 5) & 0xFFFF
        name = {0: 'movn', 2: 'movz', 3: 'movk'}.get(opc, 'move-wide')
        return f'{name} {x}{rd}, #{imm:#x} lsl {16 * hw}'
    if (w & 0xFF201FE0) == 0x1E201000 and ((w >> 22) & 3) in (0, 1):
        from consts import fmov_imm
        dbl = (w >> 22) & 1
        v = fmov_imm((w >> 13) & 0xFF, dbl)
        val = struct.unpack('<d' if dbl else '<f', v.to_bytes(8 if dbl else 4, 'little'))[0]
        return f'fmov {"d" if dbl else "s"}{rd}, #{val}'
    if (w & 0x7F3FFC00) == 0x1E270000:
        return f'fmov s/d{rd}, {x}{rn} (bits from a general register)'
    if (w & 0x7F3FFC00) == 0x1E260000:
        return f'fmov {x}{rd}, s/d{rn} (bits to a general register)'
    if (w & 0xFF200C00) == 0x1E200800 and ((w >> 12) & 15) in FP2:
        t = 'd' if (w >> 22) & 1 else 's'
        return f'{FP2[(w >> 12) & 15]} {t}{rd}, {t}{rn}, {t}{rm}'
    if (w & 0x7F3F0000) == 0x1E220000 or (w & 0x7F3F0000) == 0x1E230000:
        return f'{"scvtf" if not (w >> 16) & 1 else "ucvtf"} s/d{rd}, {x}{rn} (integer to float)'
    if (w & 0x7F3F0000) == 0x1E380000 or (w & 0x7F3F0000) == 0x1E390000:
        return f'{"fcvtzs" if not (w >> 16) & 1 else "fcvtzu"} {x}{rd}, s/d{rn} (float to integer)'
    if (w & 0x3B000000) == 0x39000000:   # load/store unsigned offset
        size, v, opc = (w >> 30) & 3, (w >> 26) & 1, (w >> 22) & 3
        scale = (4 if opc & 2 else size) if v else size
        off = ((w >> 10) & 0xFFF) << scale
        reg = (['b', 'h', 's', 'd'][size] if not (v and opc & 2) else 'q') if v else ('x' if size == 3 else 'w')
        base = 'sp' if rn == 31 else f'x{rn}'
        return f'{"ldr" if opc & 1 or (v and opc == 3) else "str"} {reg}{rd}, [{base}, #{off:#x}]'
    if (w & 0x7F000000) == 0x11000000 or (w & 0x7F000000) == 0x51000000:
        imm = ((w >> 10) & 0xFFF) << (12 if (w >> 22) & 1 else 0)
        sp = lambda r: 'sp' if r == 31 else f'{x}{r}'
        return f'{"sub" if (w >> 30) & 1 else "add"} {sp(rd)}, {sp(rn)}, #{imm:#x}'
    k = word_kind(w)
    if k == 'BL':
        t = a + (sext(w & 0x3FFFFFF, 26) << 2)
        return f'bl {mod.label(t)}'
    if k == 'B':
        t = a + (sext(w & 0x3FFFFFF, 26) << 2)
        return f'b {mod.name}+{t - mod.base:#x}'
    if k != 'other':
        return k.lower()
    if (w & 0xFF000010) == 0x54000000:
        return 'b.cond (conditional branch)'
    if (w & 0x7E000000) == 0x34000000:
        return f'cbz/cbnz {x}{rd}'
    if (w & 0x1F000000) == 0x1E000000:
        return 'other floating-point instruction'
    if (w & 0x0A000000) == 0x08000000:
        return 'other load/store'
    return 'other'


def insn_report(mod, a, n):
    f = mod.function(a)
    print(f'{mod.name}+{a - mod.base:#x}' + (f' in {mod.name}+{f[0] - mod.base:#x}' if f else ''))
    for q in range(a - 4 * n, a + 4 * (n + 1), 4):
        if not mod.m.in_text(q):
            continue
        mark = '  <==' if q == a else ''
        print(f'  {mod.name}+{q - mod.base:#x}: {insn_kind(mod, q)}{mark}')


def fde_cover(mod, a):
    """The .eh_frame FDE whose range holds a, from .eh_frame_hdr (FDE pc_begin pcrel sdata4, pc_range udata4)."""
    m = mod.m
    img, hdr = m.image, m.nso.eh_start
    if not hdr or hdr >= len(img):
        return 'no .eh_frame_hdr'
    _, ptr_enc, cnt_enc, tbl_enc = img[hdr:hdr + 4]
    from module import _read_encoded
    off = hdr + 4
    _, sz = _read_encoded(img, off, ptr_enc, hdr)
    off += sz
    count, sz = _read_encoded(img, off, cnt_enc, hdr)
    off += sz
    best = None
    for _ in range(count):
        loc, sz = _read_encoded(img, off, tbl_enc, hdr)
        off += sz
        fde, sz = _read_encoded(img, off, tbl_enc, hdr)
        off += sz
        if mod.base + loc <= a and (best is None or loc > best[0]):
            best = (loc, fde)
    if not best:
        return 'no FDE at or before it'
    loc, fde = best
    begin = fde + 8 + int.from_bytes(img[fde + 8:fde + 12], 'little', signed=True)
    rng = int.from_bytes(img[fde + 12:fde + 16], 'little')
    if begin != loc:
        return f'nearest FDE starts at {mod.name}+{loc:#x} (range not decoded: other pointer encoding)'
    inside = loc <= a - mod.base < loc + rng
    return (f'nearest FDE {mod.name}+{loc:#x}..{mod.name}+{loc + rng:#x}: '
            + ('the address lies inside it' if inside else 'the address lies after its end (code without unwind info)'))


def abs_refs(mod, a):
    """ABS64 / GLOB_DAT relocations with a symbol of this module, whose symbol value + addend is a; and how many such
    relocations into .text are not function starts (a gap in the discovery seeds, which take RELATIVE ones only)."""
    m = mod.m
    syms = m.nso.syms
    starts = set(mod.starts)
    hits, into_text, not_start, sample = [], 0, 0, []
    for off, typ, sym, add in m.nso.relocs:
        if typ not in (257, 1025) or not sym or sym >= len(syms) or not syms[sym][2]:
            continue
        t = mod.base + syms[sym][3] + add
        if t == a:
            hits.append((off, typ, syms[sym][0], add))
        if m.in_text(t):
            into_text += 1
            if t not in starts:
                not_start += 1
                if len(sample) < 5:
                    sample.append(f'{mod.name}+{t - mod.base:#x}')
    for off, typ, name, add in hits:
        print(f'  {"ABS64" if typ == 257 else "GLOB_DAT"} relocation to it at {mod.name}+{off:#x}'
              f' (symbol {name or "unnamed"} + {add:#x})')
    print(f'  ABS64/GLOB_DAT relocations into .text with a local symbol: {into_text}, not function starts: {not_start}'
          + (f' (e.g. {", ".join(sample)})' if sample else ''))


def ref_hits(mod, a):
    """[(kind, word index of the source)] of direct branches, ADR and ADRP+ADD that name address a."""
    m = mod.m
    w = m.words
    lo = m.lo
    i = (a - lo) >> 2
    # direct branches
    hits = []
    sel = np.flatnonzero((w & 0x7C000000) == 0x14000000)
    tgt = sel + sext_np(w[sel] & 0x03FFFFFF, 26)
    for s in sel[tgt == i]:
        hits.append(('BL' if w[s] >> 31 else 'B', int(s)))
    for mask, val, nb, k in ((0xFE000010, 0x54000000, 19, 'B.cond'), (0x7E000000, 0x34000000, 19, 'CBZ/CBNZ'),
                             (0x7E000000, 0x36000000, 14, 'TBZ/TBNZ')):
        sel = np.flatnonzero((w & mask) == val)
        tgt = sel + sext_np((w[sel] >> 5) & ((1 << nb) - 1), nb)
        for s in sel[tgt == i]:
            hits.append((k, int(s)))
    # ADR
    sel = np.flatnonzero((w & 0x9F000000) == 0x10000000)
    imm = sext_np(((w[sel] >> 5) & 0x7FFFF) << 2 | ((w[sel] >> 29) & 3), 21)
    br = np.flatnonzero((w & 0xFFFFFC1F) == 0xD61F0000)
    for s in sel[(lo + sel * 4 + imm) == a]:
        s = int(s)
        nb = br[np.searchsorted(br, s):][:1]
        anchor = len(nb) and nb[0] - s <= 16
        hits.append(('ADR' + (' (skipped by discovery: a BR follows within 16 instructions)' if anchor else ''), s))
    # ADRP + ADD
    adrp = np.flatnonzero((w & 0x9F000000) == 0x90000000)
    page = ((lo + adrp * 4) & ~0xFFF) + (sext_np(((w[adrp] >> 5) & 0x7FFFF) << 2 | ((w[adrp] >> 29) & 3), 21) << 12)
    for s in adrp[page == (a & ~0xFFF)]:
        s = int(s)
        rd = int(w[s]) & 31
        for k in range(1, 9):
            if s + k >= len(w):
                break
            x = int(w[s + k])
            if (x & 0xFFC00000) == 0x91000000 and ((x >> 5) & 31) == rd and ((x >> 10) & 0xFFF) == (a & 0xFFF):
                hits.append(('ADRP+ADD', s))
                break
    return hits


def pieces(mod, a):
    """For the .eh_frame function holding a: every discovered function start inside it with the reasons it is a
    start, and every BR (not a return) in it with where its register was set. Explains a function the discovery split."""
    m = mod.m
    rng = [(b, e) for b, e in m.eh_frame_ranges() if b <= a < e]
    name = lambda x: f'{mod.name}+{x - mod.base:#x}'
    if not rng:
        print(f'{name(a)}: no .eh_frame function holds it')
        return
    b, e = rng[0]
    print(f'{name(a)}: .eh_frame function {name(b)}..{name(e)} ({(e - b) // 4} instructions)')
    eh = set(m.eh_frame_functions())
    exports = set(mod.exports)
    rel = {mod.base + add for _, typ, _, add in m.nso.relocs if typ == R_AARCH64_RELATIVE and b <= mod.base + add < e}
    inner = [x for x in mod.starts if b <= x < e]
    for x in inner:
        why = []
        if x in eh:
            why.append('eh_frame')
        if x in exports:
            why.append('export')
        if x in rel:
            why.append('RELATIVE')
        for k, src in ref_hits(mod, x):
            sa = m.lo + src * 4
            why.append(f'{k} from {name(sa)}' + (' (same .eh_frame function)' if b <= sa < e else ''))
        nxt = next((y for y in mod.starts if y > x), m.hi)
        print(f'  start {name(x)} (+{x - b:#x}, {(min(nxt, e) - x) // 4} instructions): '
              + ('; '.join(why[:6]) if why else 'no reason found (branch fixpoint?)')
              + (f'; ... {len(why) - 6} more' if len(why) > 6 else ''))
    for q in range(b, e, 4):
        wq = m.word(q)
        if (wq & 0xFFFFFC1F) == 0xD61F0000 and ((wq >> 5) & 31) != 30:
            g = mod.function(q)
            r = (wq >> 5) & 31
            print(f'  BR x{r} at {name(q)} (in {name(g[0])}): x{r} from {reg_origin(mod, g[0], q, r)}')


def refs(mod, a):
    """Report where address a is referenced and how discovery saw it."""
    m = mod.m
    w = m.words
    lo = m.lo
    name = f'{mod.name}+{a - mod.base:#x}'
    print(f'{name}: references')
    if not m.in_text(a):
        print('  not in .text')
        return
    i = (a - lo) >> 2
    f = mod.function(a)
    if f:
        print(f'  discovery: {"function start" if f[0] == a else f"inside {mod.name}+{f[0] - mod.base:#x} at +{a - f[0]:#x}"}'
              f' (function {(f[1] - f[0]) // 4} instructions)')
    if i > 0:
        k = word_kind(int(w[i - 1]))
        print(f'  instruction before it: {k}' + ('  -> it is a return address' if k == 'BL' or k.startswith('BLR') else ''))
    print(f'  instruction at it: {word_kind(int(w[i]))}')
    hits = ref_hits(mod, a)
    for k, s in hits:
        src = lo + s * 4
        g = mod.function(src)
        where = f'{mod.name}+{src - mod.base:#x}' + (f' (in {mod.name}+{g[0] - mod.base:#x})' if g else '')
        print(f'  {k:8} from {where}')
    rel = [off for off, typ, _, add in m.nso.relocs if typ == R_AARCH64_RELATIVE and mod.base + add == a]
    print(f'  RELATIVE relocations to it: {len(rel)}' + (f' (first at {mod.name}+{rel[0]:#x})' if rel else ''))
    print(f'  .eh_frame entry starts at it: {"yes" if a in set(m.eh_frame_functions()) else "no"}')
    try:
        print(f'  .eh_frame: {fde_cover(mod, a)}')
    except (ValueError, IndexError) as e:
        print(f'  .eh_frame: not decoded ({e})')
    abs_refs(mod, a)
    # 32-bit PC-relative words (`.word target - .`, relative vtables and offset tables) anywhere in the image
    img32 = np.frombuffer(m.image[:len(m.image) // 4 * 4], dtype='<i4').astype(np.int64)
    pos = np.arange(len(img32), dtype=np.int64) * 4
    pcrel = np.flatnonzero(pos + img32 == a - mod.base)
    print(f'  32-bit words holding (target - their own address): {len(pcrel)}'
          + (f' (first at {mod.name}+{int(pcrel[0]) * 4:#x})' if len(pcrel) else ''))
    # 32-bit words anywhere in the image holding the offset from some anchor are too common to search; report the
    # 32-bit and 64-bit image words equal to the module offset (tables of offsets from the module base).
    off = a - mod.base
    img = np.frombuffer(m.image[:len(m.image) // 4 * 4], dtype='<u4')
    n32 = int(np.count_nonzero(img == off))
    print(f'  image words equal to the module offset {off:#x}: {n32}')
    if not hits and not rel:
        print('  no direct reference: the address is computed at run time (a table of relative offsets, pointer'
              ' arithmetic, or a return address)')
    # indirect branch at a-4 or a (the caller side of an lr)
    for q in (a - 4, a):
        k = word_kind(m.word(q)) if m.in_text(q) else 'other'
        if k.startswith('BLR') or k.startswith('BR'):
            g = mod.function(q)
            r = (m.word(q) >> 5) & 31
            print(f'  {k} at {mod.name}+{q - mod.base:#x}: x{r} from {reg_origin(mod, g[0], q, r)}')


def sext_np(v, bits):
    v = v.astype(np.int64)
    return np.where(v & (1 << (bits - 1)), v - (1 << bits), v)


def returns_report(mod, addr, depth, seen):
    """--returns: where x0 comes from at each return of the function holding addr, its compares and tail calls; a
    return of a game function's result is followed into that function, up to 4 levels."""
    f = mod.function(addr)
    ind = '  ' * depth
    if not f:
        print(f'{ind}{mod.name}+{addr - mod.base:#x}: not inside a function')
        return
    lo_, hi_ = f
    if lo_ in seen:
        return
    seen.add(lo_)
    print(f'{ind}{mod.name}+{lo_ - mod.base:#x} ({(hi_ - lo_) // 4} instructions):')
    follow = []
    for q in range(lo_, hi_, 4):
        w = mod.m.word(q)
        if (w & 0x7F20001F) == 0x6B00001F:   # CMP register (SUBS to zr)
            print(f'{ind}  +{q - lo_:#x}: compare x{(w >> 5) & 31} ({reg_origin(mod, lo_, q, (w >> 5) & 31)}) with '
                  f'x{(w >> 16) & 31} ({reg_origin(mod, lo_, q, (w >> 16) & 31)})')
        elif (w & 0x7F80001F) == 0x7100001F:   # CMP immediate
            imm = ((w >> 10) & 0xFFF) << (12 if (w >> 22) & 1 else 0)
            print(f'{ind}  +{q - lo_:#x}: compare x{(w >> 5) & 31} ({reg_origin(mod, lo_, q, (w >> 5) & 31)}) with {imm:#x}')
        elif w == 0xD65F03C0:   # RET
            org = reg_origin(mod, lo_, q, 0)
            print(f'{ind}  +{q - lo_:#x}: return x0 = {org}')
            if 'result or clobber of a call' in org:
                for p_ in range(q - 4, lo_ - 4, -4):   # the call that set x0
                    wp = mod.m.word(p_)
                    if (wp & 0xFC000000) == 0x94000000:
                        follow.append(p_ + sext(wp & 0x3FFFFFF, 26) * 4)
                        break
                    if (wp & 0xFFFFFC1F) == 0xD63F0000:
                        break
        elif (w & 0xFC000000) == 0x14000000:
            t = q + sext(w & 0x3FFFFFF, 26) * 4
            if not lo_ <= t < hi_:
                print(f'{ind}  +{q - lo_:#x}: tail call {mod.label(t)} with x0 = {reg_origin(mod, lo_, q, 0)}')
                follow.append(t)
    if depth < 4:
        for t in follow:
            if mod.m.in_text(t) and not mod.label(t).startswith('_'):
                print(f'{ind}  follows the result of {mod.label(t)}:')
                returns_report(mod, t, depth + 1, seen)


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


def string_report(mod, text, limit=40):
    """Strings of the module image containing text, and the functions that reference each one."""
    img = mod.m.image
    pat = re.compile(rb'[\x20-\x7e]*' + re.escape(text.encode()) + rb'[\x20-\x7e]*\x00', re.I)
    hits = list(pat.finditer(img))
    print(f'strings in {mod.name} containing "{text}": {len(hits)}' + (f' (first {limit})' if len(hits) > limit else ''))
    for m in hits[:limit]:
        a = mod.base + m.start()
        s = m.group()[:-1].decode('ascii')
        users = []
        for kind, idx in ref_hits(mod, a):
            src = mod.m.lo + idx * 4
            f = mod.function(src)
            users.append(f'{kind.split(" ")[0]} at {mod.name}+{src - mod.base:#x} in function '
                         f'{mod.name}+{f[0] - mod.base:#x}' if f else f'{kind} at {mod.name}+{src - mod.base:#x}')
        print(f'  {mod.name}+{a - mod.base:#x}  "{s[:100]}"')
        for u in users[:8] or ['(no ADRP+ADD/ADR to it; maybe through a pointer table)']:
            print(f'      {u}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addresses', nargs='*', help='module+offset as karpur.log prints them')
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--before', type=int, default=40)
    ap.add_argument('--after', type=int, default=15)
    ap.add_argument('--refs', action='store_true', help='also list where each address is referenced')
    ap.add_argument('--returns', action='append', default=[], metavar='MOD+OFF',
                    help='for the function at MOD+OFF: where x0 comes from at each return, and its compares')
    ap.add_argument('--origin', action='append', default=[], metavar='MOD+OFF:xN',
                    help='where register xN was last set before the instruction at MOD+OFF (repeatable)')
    ap.add_argument('--insn', action='append', default=[], metavar='MOD+OFF',
                    help='the instruction classes (register, immediate, load offset, call target) around MOD+OFF')
    ap.add_argument('--string', action='append', default=[], metavar='TEXT',
                    help='strings of main containing TEXT and the functions that use them')
    ap.add_argument('--around', type=int, default=4, help='instructions before and after for --insn (default 4)')
    a = ap.parse_args()
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    want = ({s.split('+', 1)[0] for s in a.addresses} | {o.split('+', 1)[0] for o in a.origin} |
            {o.split('+', 1)[0] for o in a.returns} | {o.split('+', 1)[0] for o in a.insn} |
            ({'main'} if a.string else set()))
    mods = {}
    for name, base, _ in layout(a.exefs):
        if name in want:
            mods[name] = Mod(name, base, a.exefs / name)
    print('=' * 72)
    for s in a.addresses:
        mod, addr = parse(s, mods)
        report(mod, addr, a.before, a.after)
        if a.refs:
            refs(mod, addr)
            pieces(mod, addr)
        print()
    for o in a.origin:
        at, _, reg = o.partition(':')
        mod, addr = parse(at, mods)
        f = mod.function(addr)
        r = int(reg.lstrip('xXwW') or 0)
        print(f'{at} {reg}: {reg_origin(mod, f[0], addr, r) if f else "not inside a function"}')
    for o in a.returns:
        mod, addr = parse(o, mods)
        returns_report(mod, addr, 0, set())
    for o in a.insn:
        mod, addr = parse(o, mods)
        insn_report(mod, addr, a.around)
    for t in a.string:
        string_report(mods['main'], t)
    if a.origin or a.returns or a.insn or a.string:
        print('=' * 72)


if __name__ == '__main__':
    main()
