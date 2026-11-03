"""Name the functions of each phase of a scene coverage run (TOTK_COVERAGE), as a report of names, counts and addresses.

runtime/coverage.c writes game/coverage/functions.txt (main offset, phase mask) and phases.txt. This tool reads both
together with main from the extracted ExeFS and gives each phase the hints the module itself carries:

- classes: the C++ class whose vtable holds the function (RTTI type names in .rodata, vtables in .data.rel.ro);
- SDK calls: the imported functions (nn::hid, nn::fs, nvn, ...) first called in the phase through the PLT, grouped
  by namespace, which shows the runtime services a phase brings in;
- strings: the NUL-terminated ASCII strings (log messages, asset paths, names) whose address a function computes,
  listed for the phase they are first used in (assertion and reference-count messages are used from the start).

A phase is reported by its new functions (run in no earlier phase); those run in this phase only are counted apart.
The full per-function list goes to game/coverage/phase-functions.txt on the player's PC (ignored by git).

hostcalls.txt (TOTK_COVERAGE=1 or, without the frame-rate cost, TOTK_HOSTCALLS=1) adds what the game asks of the
runtime in each phase: the NVN procs and replaced SDK functions first called in it and the busiest NVN procs per
present. The game calls NVN through function pointers, so these calls are not in the SDK lists above.

  .venv\\Scripts\\python tools\\phases.py
  .venv\\Scripts\\python tools\\phases.py --host-only
  .venv\\Scripts\\python tools\\phases.py --names "Boot,Sprache,Preset,Editor,Ausweis,Video,Wohnung" --show 40
  .venv\\Scripts\\python tools\\phases.py --phase 3 --only

Output: class and symbol names, strings of the code, counts and main offsets; never instruction or data bytes.
"""

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'recomp'))

COVERAGE = ROOT / 'game' / 'coverage'
R_ABS64, R_GLOB_DAT, R_RELATIVE = 257, 1025, 1027
MAX_STRING = 120


# ---- Itanium C++ names, as far as the hints need them --------------------------------------------------------------

def _skip_args(s, i):
    """Index after the template argument list that starts at s[i] == 'I' (balanced by its openers and E)."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c.isdigit():   # a source name may hold the letters that open and close
            n = 0
            while i < len(s) and s[i].isdigit():
                n = n * 10 + int(s[i])
                i += 1
            i += n
            continue
        if c in 'INLX':
            depth += 1
        elif c == 'E':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def _names(s, i):
    """Components of an Itanium nested or unscoped name starting at s[i]; stops at the first part it cannot read."""
    out = []
    while i < len(s):
        c = s[i]
        if c.isdigit():
            n = 0
            while i < len(s) and s[i].isdigit():
                n = n * 10 + int(s[i])
                i += 1
            out.append(s[i:i + n])
            i += n
        elif c == 'I':
            if out:
                out[-1] += '<...>'
            i = _skip_args(s, i)
        elif s.startswith('St', i):
            out.append('std')
            i += 2
        elif c == 'S':   # substitution of an earlier component: not resolved
            j = s.find('_', i)
            if j < 0:
                break
            out.append('?')
            i = j + 1
        elif c in 'CD' and i + 1 < len(s) and s[i + 1] in '0123':
            base = out[-1].split('<')[0] if out else '?'
            out.append(base if c == 'C' else '~' + base)
            i += 2
        else:
            break
    return out


def demangle(sym):
    """'nn::hid::GetNpadState' for '_ZN2nn3hid12GetNpadStateEPNS0_...'; C names come back unchanged."""
    if sym.startswith('_ZN'):
        i = 3
        while i < len(sym) and sym[i] in 'KVrRO':
            i += 1
        parts = _names(sym, i)
    elif sym.startswith('_Z') and len(sym) > 2 and (sym[2].isdigit() or sym.startswith('_ZSt')):
        parts = _names(sym, 2)
    else:
        return sym
    return '::'.join(parts) if parts else sym


def type_name(s):
    """Class name of an RTTI type name string ('N3gfl4core6ObjectE' -> 'gfl::core::Object'), else None."""
    if not s or not (s[0] == 'N' or s[0].isdigit()):
        return None
    parts = _names(s, 1 if s[0] == 'N' else 0)
    if not parts or any(p in ('', '?') for p in parts):
        return None
    if not all(p.replace('<...>', '').replace('_', 'a').isalnum() for p in parts):
        return None
    return '::'.join(parts)


def group(name):
    """Namespace group of a demangled import: two parts for nn::*, one otherwise; C functions by their own name."""
    parts = name.split('::')
    if len(parts) == 1:
        return 'C ' + ('nvn' if name.startswith('nvn') else name)
    return '::'.join(parts[:2] if parts[0] == 'nn' and len(parts) > 2 else parts[:1])


# ---- scanning the module ---------------------------------------------------------------------------------------------

def sext_np(v, bits):
    v = v.astype(np.int64)
    return np.where(v & (1 << (bits - 1)), v - (1 << bits), v)


class View:
    """What the scans need of a module: base, .text start (lo, absolute) and words, the image (indexed by module
    offset), RELATIVE relocations (offset -> target offset), offsets of symbolic relocations, sorted function starts
    (absolute) and a resolver for PLT stubs (absolute address -> import name or None)."""

    def __init__(self, base, lo, words, image, relative, symbolic, starts, plt):
        self.base, self.lo, self.words, self.image = base, lo, words, image
        self.relative, self.symbolic = relative, symbolic
        self.starts = np.asarray(starts, dtype=np.int64)
        self.plt = plt

    def owner(self, idx):
        """Function start (absolute) holding each .text word index in idx."""
        a = self.lo + idx.astype(np.int64) * 4
        k = np.searchsorted(self.starts, a, side='right') - 1
        return np.where(k >= 0, self.starts[np.maximum(k, 0)], -1)

    def string_at(self, off):
        if not 0 <= off < len(self.image):
            return None
        raw = self.image[off:off + MAX_STRING + 1]
        end = raw.find(b'\0')
        if end < 4:
            return None
        s = raw[:end]
        if any(b < 0x20 or b > 0x7e for b in s) or not any(chr(b).isalpha() for b in s):
            return None
        return s.decode('ascii')


def address_refs(v, wanted):
    """(function start, target absolute) for each ADR and ADRP+ADD in a function of the set wanted."""
    w = v.words
    out = []
    adr = np.flatnonzero((w & 0x9F000000) == 0x10000000)
    adrp = np.flatnonzero((w & 0x9F000000) == 0x90000000)
    for sel, page in ((adr, False), (adrp, True)):
        if not len(sel):
            continue
        fn = v.owner(sel)
        keep = np.isin(fn, wanted)
        sel, fn = sel[keep], fn[keep]
        imm = sext_np(((w[sel] >> 5) & 0x7FFFF) << 2 | ((w[sel] >> 29) & 3), 21)
        if not page:
            out += zip(fn.tolist(), (v.lo + sel * 4 + imm).tolist())
            continue
        base = ((v.lo + sel.astype(np.int64) * 4) & ~0xFFF) + (imm << 12)
        rd = w[sel] & 31
        done = np.zeros(len(sel), bool)
        for k in range(1, 5):   # the ADD usually follows within a few instructions
            j = sel + k
            ok = (j < len(w)) & ~done
            x = w[np.minimum(j, len(w) - 1)]
            add = ok & ((x & 0xFFC00000) == 0x91000000) & (((x >> 5) & 31) == rd)
            out += zip(fn[add].tolist(), (base[add] + ((x[add] >> 10) & 0xFFF)).tolist())
            done |= add
    return out


def import_calls(v, wanted):
    """(function start, import name) for each BL or B from a function of the set wanted to a PLT stub."""
    w = v.words
    sel = np.flatnonzero((w & 0x7C000000) == 0x14000000)
    fn = v.owner(sel)
    keep = np.isin(fn, wanted)
    sel, fn = sel[keep], fn[keep]
    tgt = v.lo + (sel + sext_np(w[sel] & 0x03FFFFFF, 26)) * 4
    names = {t: v.plt(t) for t in np.unique(tgt).tolist()}
    return [(f, names[t]) for f, t in zip(fn.tolist(), tgt.tolist()) if names[t]]


def vtable_classes(v):
    """{function start (absolute): [class names]} from the vtables: an RTTI object T has a symbolic (or data) pointer
    at T and its name string at T+8; a vtable has its offset-to-top at V (no relocation, 0 or small negative), T at
    V+8 and function pointers from V+16."""
    rel, sym, img = v.relative, v.symbolic, v.image
    tinfo = {}
    for off, tgt in rel.items():
        t = off - 8
        if t in sym or (t in rel and not v.lo - v.base <= rel[t] < v.lo - v.base + len(v.words) * 4):
            name = type_name(v.string_at(tgt))
            if name:
                tinfo[t] = name
    text_lo, text_hi = v.lo - v.base, v.lo - v.base + len(v.words) * 4
    out = defaultdict(list)
    for off, tgt in rel.items():
        if tgt not in tinfo:
            continue
        top = off - 8
        if top in rel or top in sym or top < 0:
            continue
        ott = int.from_bytes(img[top:top + 8], 'little', signed=True)
        if not -0x100000 < ott <= 0:
            continue
        cls = tinfo[tgt]
        s = off + 8
        for _ in range(4096):
            if s in rel and text_lo <= rel[s] < text_hi:
                lst = out[v.base + rel[s]]
                if cls not in lst:
                    lst.append(cls)
            elif s not in sym:   # a symbolic slot (__cxa_pure_virtual) belongs to the table; anything else ends it
                break
            s += 8
    return out


# ---- coverage files and the report -----------------------------------------------------------------------------------

def read_coverage(folder):
    funcs = {}
    for line in (folder / 'functions.txt').read_text().splitlines():
        t = line.split()
        if len(t) == 2 and not line.startswith('#'):
            funcs[int(t[0], 16)] = int(t[1], 16)
    present = {}
    p = folder / 'phases.txt'
    if p.is_file():
        for line in p.read_text().splitlines():
            t = line.split()
            if len(t) >= 2 and not line.startswith('#'):
                present[int(t[0])] = int(t[1])
    return funcs, present


def first_phase(m):
    return (m & -m).bit_length() - 1


def analyse(v, funcs):
    """Hints per covered function (absolute start): classes, import names, strings."""
    wanted = np.array(sorted(v.base + o for o in funcs), dtype=np.int64)
    classes = vtable_classes(v)
    imports, strings = defaultdict(set), defaultdict(set)
    for f, name in import_calls(v, wanted):
        imports[f].add(demangle(name))
    for f, t in address_refs(v, wanted):
        s = v.string_at(t - v.base)
        if s:
            strings[f].add(s)
    return classes, imports, strings


def top(counter, n):
    return sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))[:n]


def first_use(table, phase_of):
    """{name: earliest phase any user ran in} for a {function: names} table; general helpers (assertions, reference
    counting, locks) are used from the start, so a later first use marks what a phase brings in."""
    out = {}
    for f, names in table.items():
        p = phase_of.get(f)
        if p is None:
            continue
        for s in names:
            if p < out.get(s, 99):
                out[s] = p
    return out


def phase_report(p, label, present, sel, only, classes, imports, strings, first_imp, first_str, show):
    head = f'phase {p}' + (f' ({label})' if label else '')
    print(f'{head}: begins at present {present.get(p, "?")}, {len(sel)} new functions, {len(only)} of them in this '
          f'phase only')
    if not sel:
        return
    cls = Counter(c for f in sel for c in classes.get(f, [])[:1])
    named = sum(1 for f in sel if f in classes)
    print(f'  classes (from vtables, {named} of {len(sel)} functions named):')
    for name, n in top(cls, show):
        print(f'    {n:5}  {name}')
    groups, syms = Counter(), defaultdict(Counter)
    for f in sel:
        for s in imports.get(f, ()):
            if first_imp.get(s) == p:
                groups[group(s)] += 1
                syms[group(s)][s] += 1
    print(f'  SDK functions first called in this phase ({sum(len(x) for x in syms.values())}, by namespace and '
          f'number of calling functions):')
    for g, n in top(groups, show):
        names = ', '.join(s.split('::')[-1] for s, _ in top(syms[g], 6))
        more = len(syms[g]) - 6
        print(f'    {n:5}  {g}: {names}' + (f' (+{more} more)' if more > 0 else ''))
    st = Counter(s for f in sel for s in strings.get(f, ()) if first_str.get(s) == p)
    print(f'  strings first used in this phase ({len(st)}, by functions using them, first {min(show, len(st))}):')
    for s, n in top(st, show):
        print(f'    {n:5}  "{s}"')
    print()


def read_hostcalls(folder):
    """hostcalls.txt: the presents each phase began at (and the last one), then per host function its calls per phase."""
    presents, rows = [], []
    for line in (folder / 'hostcalls.txt').read_text(errors='replace').splitlines():
        if line.startswith('# presents:'):
            presents = [int(x) for x in line.split(':', 1)[1].split()]
        elif line and not line.startswith('#'):
            t = line.split(' ')
            n = len(presents) - 1 if presents else 0
            rows.append(([int(x) for x in t[:n]], ' '.join(t[n:])))
    return presents, rows


def host_name(name):
    return name[4:] if name.startswith('nvn:') else demangle(name)


def is_counter(name):
    """Replay counters of the Vulkan back end (vk_exec.c coverage_gpu_counters), written after the host functions."""
    return name.startswith(('exec:', 'shader:', 'time:', 'bytes:'))


def host_report(presents, rows, labels, phases_wanted, show):
    """Per phase: the replay counters, how many host functions ran, those first called in it, and the busiest NVN procs
    per present."""
    counters = [r for r in rows if is_counter(r[1])]
    rows = [r for r in rows if not is_counter(r[1])]
    n = len(presents) - 1
    first = [next((k for k, c in enumerate(counts) if c), None) for counts, _ in rows]
    nvn = sum(1 for _, name in rows if name.startswith('nvn:'))
    print(f'host functions called: {len(rows)} ({nvn} NVN procs, {len(rows) - nvn} SDK replacements and runtime entries)')
    print()
    for k in range(n):
        if phases_wanted and k not in phases_wanted:
            continue
        frames = max(1, presents[k + 1] - presents[k])
        called = [(counts[k], name) for counts, name in rows if counts[k]]
        fresh = sorted(((counts[k], name) for (counts, name), f in zip(rows, first) if f == k), reverse=True)
        label = labels[k] if k < len(labels) else ''
        print(f'phase {k}' + (f' ({label})' if label else '') + f': presents {presents[k]}..{presents[k + 1]}, '
              f'{len(called)} host functions called, {sum(c for c, _ in called)} calls, {len(fresh)} first called here')
        if counters:
            print('  replayed on the GPU, per present: ' + ', '.join(
                f'{name.split(":", 1)[1]} {counts[k] / frames:.1f}' for counts, name in counters
                if name.startswith('exec:')))
            cost = [f'{name.split(":", 1)[1]} {counts[k] / frames / 1e6:.2f} ms' for counts, name in counters
                    if name.startswith('time:')]
            cost += [f'{name.split(":", 1)[1]} {counts[k] / frames / 1024:.0f} KiB' for counts, name in counters
                     if name.startswith('bytes:')]
            if cost:
                print('  CPU time and bytes per present: ' + ', '.join(cost))
            sh = {name: counts[k] for counts, name in counters if name.startswith('shader:')}
            if sh:
                print(f'  shaders translated in this phase: {sh.get("shader:translated", 0)}')
            rec = sum(c[k] for c, name in rows if name.startswith('nvn:nvnCommandBufferDraw'))
            seen = next((c[k] for c, name in counters if name == 'exec:draws seen'), 0)
            if seen:
                print(f'  draws recorded {rec}, replayed {seen}: each recorded draw ran {seen / max(1, rec):.1f} times')
        for title, sel in (('NVN procs first called here', [x for x in fresh if x[1].startswith('nvn:')]),
                           ('SDK and runtime functions first called here',
                            [x for x in fresh if not x[1].startswith('nvn:')])):
            if sel:
                print(f'  {title} ({len(sel)}, by calls):')
                for c, name in sel[:show]:
                    print(f'    {c:9}  {host_name(name)}')
                if len(sel) > show:
                    print(f'    ... {len(sel) - show} more')
        busy = sorted(((c, name) for c, name in called if name.startswith('nvn:')), reverse=True)[:show]
        if busy:
            print('  busiest NVN procs (calls per present):')
            for c, name in busy:
                print(f'    {c / frames:9.1f}  {host_name(name)}')
        print()


def write_functions(path, funcs, v, classes, imports, strings):
    with open(path, 'w', encoding='ascii', errors='replace') as f:
        f.write('# main offset, phase mask, size in instructions, class, SDK calls, strings; KarPur tools/phases.py\n')
        for off in sorted(funcs):
            a = v.base + off
            k = int(np.searchsorted(v.starts, a, side='right'))
            hi = int(v.starts[k]) if k < len(v.starts) else v.lo + len(v.words) * 4
            cls = '|'.join(classes.get(a, [])[:2]) or '-'
            imp = '|'.join(sorted(imports.get(a, ()))[:6]) or '-'
            st = '|'.join(sorted(strings.get(a, ()))[:4]) or '-'
            f.write(f'{off:x} {funcs[off]:x} {(hi - a) // 4} {cls} {imp} "{st}"\n')


def load_main(exefs):
    from calls import Mod   # recomp's function discovery and the PLT decoding of the crash tools
    from layout import layout
    for name, base, _ in layout(exefs):
        if name == 'main':
            mod = Mod('main', base, exefs / 'main')
            break
    else:
        sys.exit(f'error: no main in {exefs}')
    m = mod.m
    relative, symbolic = {}, set()
    for off, typ, sym, add in m.nso.relocs:
        if typ == R_RELATIVE:
            relative[off] = add
        elif typ in (R_ABS64, R_GLOB_DAT):
            symbolic.add(off)
    return View(base, m.lo, m.words, m.image, relative, symbolic, mod.starts, mod.plt_target)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--coverage', type=Path, default=COVERAGE, help='folder with functions.txt and phases.txt')
    ap.add_argument('--exefs', type=Path, default=ROOT / 'game' / 'extracted' / 'update' / 'exefs')
    ap.add_argument('--names', default='', help='comma-separated names of the phases, e.g. "Boot,Sprache,Editor"')
    ap.add_argument('--phase', type=int, action='append', default=[], help='report only this phase (repeatable)')
    ap.add_argument('--only', action='store_true', help='report the functions of a phase only, not all new ones')
    ap.add_argument('--show', type=int, default=20, help='lines per list (default 20)')
    ap.add_argument('--host-only', action='store_true', help='report hostcalls.txt only, without reading main')
    a = ap.parse_args()
    labels = [s.strip() for s in a.names.split(',')] if a.names else []
    has_host, has_funcs = (a.coverage / 'hostcalls.txt').is_file(), (a.coverage / 'functions.txt').is_file()
    if not has_host and not has_funcs:
        sys.exit(f'error: {a.coverage} has no hostcalls.txt or functions.txt: run the game with TOTK_HOSTCALLS=1 or '
                 f'TOTK_COVERAGE=1 and press F4')
    if has_host:
        print('=' * 72)
        print('KarPur host call report (NVN procs and SDK functions the runtime answers, per phase)')
        presents, rows = read_hostcalls(a.coverage)
        host_report(presents, rows, labels, a.phase, a.show)
    if a.host_only or not has_funcs:
        print('=' * 72)
        return
    if not (a.exefs / 'main').is_file():
        sys.exit(f'error: {a.exefs} has no main: run tools\\extract.py first')
    funcs, present = read_coverage(a.coverage)
    print(f'reading main and {len(funcs)} covered functions ...', flush=True)
    v = load_main(a.exefs)
    v.starts = np.union1d(v.starts, np.array([v.base + o for o in funcs], dtype=np.int64))
    classes, imports, strings = analyse(v, funcs)
    print('=' * 72)
    print(f'KarPur phase report: {len(funcs)} functions of main ran, {sum(1 for f in funcs if v.base + f in classes)} '
          f'with a class from a vtable, {len({c for l in classes.values() for c in l})} classes in all vtables')
    print()
    phase_of = {v.base + o: first_phase(m) for o, m in funcs.items()}
    first_imp, first_str = first_use(imports, phase_of), first_use(strings, phase_of)
    phases = sorted({first_phase(m) for m in funcs.values()} | set(present))
    for p in phases:
        if a.phase and p not in a.phase:
            continue
        new = [v.base + o for o, m in funcs.items() if first_phase(m) == p]
        only = [f for f in new if funcs[f - v.base] == 1 << p]
        phase_report(p, labels[p] if p < len(labels) else '', present, only if a.only else new, only, classes,
                     imports, strings, first_imp, first_str, a.show)
    out = a.coverage / 'phase-functions.txt'
    write_functions(out, funcs, v, classes, imports, strings)
    print(f'full list per function: {out}')
    print('=' * 72)


if __name__ == '__main__':
    main()
