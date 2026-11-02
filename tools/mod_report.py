"""Report what a RomFS mod for Scarlet/Violet changes, compared with the player's own game: names and numbers only.

Community mods (Trinity Mod Loader layout: a folder with romfs/<path> loose files) replace data files of the packed
archive arc/data.trpfs. To do the same in KarPur at run time (runtime/data_patch.c) we need to know which files and
fields a mod changes; this tool reads the mod's files only to compare them, on the player's PC, and prints:

  - every file of the mod: its path, size, whether the game's archive has it (FNV-1a 64 of the path), its pack and
    how the game stores it (unpacked, or Oodle-packed);
  - for a file whose original can be read (unpacked in the archive, or dumped to game/dump by a DATA_WATCH=1 run):
    with a FlatBuffers schema (.bfbs from the mod, the same pack or game/dump) the fields that differ, original ->
    mod; without one, the 32-bit words that differ, as float and integer, with their byte offset;
  - Oodle-packed originals that have no dump yet are added to game/data_watch.txt, so one game run with
    TOTK_DATA_WATCH=1 dumps them; run this tool again afterwards.

Nothing of the mod or the game is written outside game/; the report goes to game/report/mods-<folder>.txt.

    .venv\\Scripts\\python tools\\mod_report.py C:\\KarPur\\mods\\improved_lod
    .venv\\Scripts\\python tools\\mod_report.py C:\\KarPur\\mods          (every mod folder in it, compared)

Identical files are only counted; files that differ get their field or word changes.
"""
import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import trpfs  # noqa: E402
from trpfs import (HEAD, SCALAR, TYPE_NAMES, Schema, Table, codec, fmt, fnv1a64, fnv1a64_bytes,  # noqa: E402
                   romfs_file, walk_paths)

MAX_WORD_DIFFS = 24


def mod_files(folder):
    """[(relative game path, file)] of a mod folder: the part after a 'romfs' folder, else after the folder itself."""
    out = []
    for p in sorted(folder.rglob('*')):
        if not p.is_file():
            continue
        parts = p.relative_to(folder).parts
        lower = [x.lower() for x in parts]
        rel = parts[lower.index('romfs') + 1:] if 'romfs' in lower else parts
        if rel:
            out.append(('/'.join(rel), p))
    return out


def schemas_in(datas):
    out = []
    for d in datas:
        if d[4:8] == b'BFBS':
            try:
                out.append(Schema(d))
            except Exception:  # noqa: BLE001
                pass
    return out


def fields_of(sc, data):
    """[(obj.field, base type, value)] of a FlatBuffer read with schema sc, or None when it does not fit."""
    if sc.root is None:
        return None
    try:
        return [(p, b, struct.unpack_from('<' + SCALAR[b], data, at)[0], at)
                for p, b, at in walk_paths(sc, sc.root, data, struct.unpack_from('<I', data, 0)[0])]
    except Exception:  # noqa: BLE001
        return None


def patch_line(loc, at, typ, value, name):
    """A game/mod_patch.txt line (runtime/data_patch.c): set this field of the original file to the mod's value."""
    v = repr(float(value)) if typ == 'f32' else str(int(value))
    if loc[0] == 'romfs':
        return f'romfs {loc[1] + at:x} {typ} mod={v} {name}'
    return f'oodle {loc[1]} {loc[2]} {loc[3]:016x} {at:x} {typ} mod={v} {name}'


def compare(orig, mod, schemas, say, loc=None, sink=None):
    for sc in schemas:
        a, b = fields_of(sc, orig), fields_of(sc, mod)
        if a is None or b is None:
            continue
        say(f'    schema {sc.objects[sc.root]["name"]}: {len(a)} fields in the game, {len(b)} in the mod')
        # fields matched by their path (vector elements by index); a field the game's file leaves at its default is
        # absent there and cannot be set in place
        ka = {p: (v, at, bt) for p, bt, v, at in a}
        kb = {p for p, _, _, _ in b}
        changed = absent = 0
        for p, bt, v, _ in b:
            old = ka.get(p)
            if old is None:
                say(f'      {p}: (default in the game, not settable in place) -> {fmt(v)}')
                changed += 1
                absent += 1
            elif old[0] != v:
                say(f'      {p}: {fmt(old[0])} -> {fmt(v)}')
                changed += 1
                if sink is not None and loc and old[2] in TYPE_NAMES:
                    sink.append(patch_line(loc, old[1], TYPE_NAMES[old[2]], v, p[-60:]))
        for p in sorted(set(ka) - kb):
            say(f'      {p}: {fmt(ka[p][0])} -> (default in the mod, left as it is)')
            changed += 1
        if absent:
            say(f'    {absent} changes need fields the game file does not have: the mod applies only in part')
        say(f'    {changed} fields differ')
        return
    if len(orig) != len(mod):
        say(f'    no schema fits; sizes differ: game {len(orig)} B, mod {len(mod)} B')
        return
    n = len(orig) // 4
    wa = struct.unpack_from(f'<{n}I', orig)
    wb = struct.unpack_from(f'<{n}I', mod)
    diffs = [i for i in range(n) if wa[i] != wb[i]]
    say(f'    no schema; same size, {len(diffs)} of {n} 32-bit words differ')
    if sink is not None and loc:
        sink.extend(patch_line(loc, 4 * i, 'u32', wb[i], f'word+{4 * i:x}') for i in diffs)
    for i in diffs[:MAX_WORD_DIFFS]:
        fa, fb = struct.unpack('<f', struct.pack('<I', wa[i]))[0], struct.unpack('<f', struct.pack('<I', wb[i]))[0]
        say(f'      +{4 * i:#x}: float {fa:.6g} -> {fb:.6g}   int {wa[i]} -> {wb[i]}')


class Game:
    """The player's archive: file hashes, packs and their tables of contents."""

    def __init__(self, romfs):
        index = json.loads((romfs / 'index.json').read_text(encoding='utf-8'))
        self.img = (romfs / 'romfs.bin').open('rb')
        fd_off, fd_size = romfs_file(index, 'arc/data.trpfd')
        fd = Table.root(self.read(fd_off, fd_size))
        self.file_hashes, self.pack_names = fd.u64s(0), fd.strings(1)
        self.file_pack = [t.scalar(0, 'Q') for t in fd.tables(2)]
        self.pack_size = [t.scalar(0, 'Q') for t in fd.tables(3)]
        self.fs_off, fs_size = romfs_file(index, 'arc/data.trpfs')
        _, root_at = struct.unpack('<8sQ', self.read(self.fs_off, 16))
        fs = Table.root(self.read(self.fs_off + root_at, fs_size - root_at))
        self.offset_of = dict(zip(fs.u64s(0), fs.u64s(1)))
        self.where = {h: i for i, h in enumerate(self.file_hashes)}
        self.tocs = {}

    def read(self, off, n):
        self.img.seek(off)
        return self.img.read(n)

    def toc(self, pi):
        if pi not in self.tocs:
            off = self.offset_of.get(fnv1a64(self.pack_names[pi]))
            if off is None or self.pack_size[pi] > 256 << 20:
                self.tocs[pi] = None
            else:
                pk = Table.root(self.read(self.fs_off + off, self.pack_size[pi]))
                self.tocs[pi] = dict(zip(pk.u64s(0), pk.tables(1)))
        return self.tocs[pi]


def analyse(game, folder, say, watch_lines, sink=None):
    """Report of one mod folder; returns its summary {path: sha256 of the content}."""
    import hashlib
    files = mod_files(folder)
    say('=' * 72)
    say(f'mod {folder.name}: {len(files)} files')
    types = Counter(Path(p).suffix or '(none)' for p, _ in files)
    say('file types: ' + ', '.join(f'{t} {c}' for t, c in types.most_common()))
    mod_data = {p: f.read_bytes() for p, f in files}
    digest = {p: hashlib.sha256(d).hexdigest() for p, d in mod_data.items()}
    mod_schemas_by_dir = {}
    for p, d in mod_data.items():
        if d[4:8] == b'BFBS':
            mod_schemas_by_dir.setdefault(p.rsplit('/', 1)[0], []).append(d)
    unknown = same = 0
    for path, fpath in files:
        data = mod_data[path]
        if path.endswith(('.trpfd', '.trpfs')):
            say(f'{path}: archive table of the mod, {len(data)} B (a loader such as Trinity Mod Loader uses it to '
                'switch the listed files to loose files)')
            continue
        # a mod without a romfs folder: the game path is the longest tail of the file's full path (folder names
        # above the mod folder included, e.g. world/...) that the archive knows
        if fnv1a64(path) not in game.where:
            parts = [x for x in fpath.resolve().parts[1:]]
            for k in range(len(parts)):
                tail = '/'.join(parts[k:])
                if fnv1a64(tail) in game.where or fnv1a64(tail.lower()) in game.where:
                    path = tail if fnv1a64(tail) in game.where else tail.lower()
                    break
        h = fnv1a64(path)
        if h not in game.where:
            unknown += 1
            say(f'{path}: {len(data)} B, not a file of this game archive (new file, another path, or the other '
                'game version)')
            continue
        pi = game.file_pack[game.where[h]]
        t = game.toc(pi)
        e = t.get(h) if t else None
        stem = game.pack_names[pi].removeprefix('arc/').removesuffix('.trpak')[:200]
        if e is None:
            say(f'{path}: {len(data)} B, pack {pi} not readable')
            continue
        start, n = e._vec(len(e.slots) - 1)
        kind = codec(e)
        size = e.scalar(3, 'Q') if kind != 'none' else n
        if kind == 'none':
            orig = e.buf[start:start + n]
            loc = ('romfs', game.fs_off + game.offset_of[fnv1a64(game.pack_names[pi])] + start)
        else:
            loc = ('oodle', n, size, fnv1a64_bytes(e.buf[start:start + min(n, HEAD)]))
            dump = trpfs.DUMP / f'{stem}__{h:016x}'
            if not dump.is_file():
                watch_lines.append(f'{n} {size} {fnv1a64_bytes(e.buf[start:start + min(n, HEAD)]):016x} '
                                   f'{stem}__{h:016x}')
                say(f'{path}: mod {len(data)} B, game {size} B ({kind}), pack {pi}; original not dumped yet: '
                    'listed for DATA_WATCH')
                continue
            orig = dump.read_bytes()
        if orig == data:
            same += 1
            continue
        say(f'{path}: mod {len(data)} B, game {size} B ({kind}), pack {pi}')
        cands = list(mod_schemas_by_dir.get(path.rsplit('/', 1)[0], []))
        for hh, ee in t.items():
            s0, n0 = ee._vec(len(ee.slots) - 1)
            if codec(ee) == 'none':
                cands.append(ee.buf[s0:s0 + n0])
            else:
                d = trpfs.DUMP / f'{stem}__{hh:016x}'
                if d.is_file():
                    cands.append(d.read_bytes())
        compare(orig, data, schemas_in(cands), say, loc, sink)
    say(f'summary {folder.name}: {len(files)} files, {unknown} not in this game, {same} identical to the game')
    return digest


def is_mod(folder):
    """A folder with a romfs folder, or with data files of the game's kinds (a mod packed without romfs/)."""
    if any(x.name.lower() == 'romfs' for x in folder.rglob('*') if x.is_dir()):
        return True
    return any(x.is_file() and x.suffix.lower() not in ('.npdm', '', '.txt', '.md', '.toml', '.png', '.jpg')
               for x in folder.rglob('*'))


def cross(mods, say):
    """Pairwise: same path with the same or other content, and paths only one of the two has."""
    say('=' * 72)
    say('comparison of the mods (same file = same path; content compared by SHA-256)')
    names = list(mods)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = mods[names[i]], mods[names[j]]
            both = set(a) & set(b)
            diff = sorted(p for p in both if a[p] != b[p])
            only_a, only_b = sorted(set(a) - set(b)), sorted(set(b) - set(a))
            if not both:
                say(f'{names[i]} / {names[j]}: no common files')
                continue
            say(f'{names[i]} / {names[j]}: {len(both)} common files, {len(both) - len(diff)} identical, '
                f'{len(diff)} different; only in the first {len(only_a)}, only in the second {len(only_b)}')
            for p in diff[:10]:
                say(f'    differs: {p}')
            for p in only_a[:5]:
                say(f'    only in {names[i]}: {p}')
            for p in only_b[:5]:
                say(f'    only in {names[j]}: {p}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('mod', type=Path, nargs='+',
                    help='unpacked mod folders, or a folder of such folders (all are compared)')
    ap.add_argument('--romfs', type=Path, default=trpfs.ROMFS)
    ap.add_argument('--only', metavar='REGEX', help='--apply: take only the fields whose path matches (e.g. '
                    '"values\\[2\\]" or "maxGenerationNum"), to find which change does what')
    ap.add_argument('--value', type=float, help='--apply: use this value for every taken field instead of the mod\'s '
                    '(tests of a moderate change)')
    ap.add_argument('--apply', action='store_true', help='write the changed values to game/mod_patch.txt, which a '
                    'run with MOD_VALUES=1 sets in the game files as they load (files of the same layout only)')
    a = ap.parse_args()
    folders, skipped = [], []
    for m in a.mod:
        if not m.is_dir():
            sys.exit(f'error: {m} is not a folder (unpack the mod first)')
        # one mod: a folder with files or game folders (romfs, world, arc, exefs …) directly in it; else a folder of
        # mods
        direct = list(m.iterdir())
        single = any(x.is_file() for x in direct) or any(x.name.lower() in ('romfs', 'world', 'arc', 'exefs', 'system')
                                                         for x in direct)
        got = [m] if single else [d for d in sorted(direct) if d.is_dir() and is_mod(d)]
        folders += got
        skipped += [d for d in sorted(direct) if d.is_dir() and d not in got] if not single else []
    for d in skipped:   # e.g. a loader's own files: say what is in it, as file types and counts
        kinds = Counter(x.suffix or '(none)' for x in d.rglob('*') if x.is_file())
        print(f'{d.name}: no romfs folder, left out; files: ' + (', '.join(f'{k} {c}' for k, c in kinds.most_common(8))
                                                                 or 'none'))
    if not folders:
        sys.exit(f'error: no folder with a romfs folder in {a.mod}')
    game = Game(a.romfs)
    out, watch_lines = [], []

    def say(line=''):
        out.append(line)

    mods, sink = {}, ([] if a.apply else None)
    for f in folders:
        mods[f.name] = analyse(game, f, say, watch_lines, sink)
        print(f'{f.name}: {len(mods[f.name])} files analysed')
    if len(mods) > 1:
        cross(mods, say)
    if watch_lines:
        trpfs.WATCH_LIST.parent.mkdir(parents=True, exist_ok=True)
        old = trpfs.WATCH_LIST.read_text(encoding='utf-8').splitlines() if trpfs.WATCH_LIST.is_file() else []
        keep = [ln for ln in old if ln.strip() and not ln.startswith('#')]
        lines = ['# packed unpacked fnv1a64-of-first-4KiB name (tools/trpfs.py --watch, tools/mod_report.py)']
        lines += sorted(set(keep + watch_lines))
        trpfs.WATCH_LIST.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        say()
        say(f'{len(set(watch_lines))} packed originals added to {trpfs.WATCH_LIST}: start the game once with '
            'DATA_WATCH=1, walk around, then run this tool again')
    if sink is not None and a.only:
        import re
        pat = re.compile(a.only)
        sink[:] = [ln for ln in sink if pat.search(ln.rsplit(' ', 1)[-1])]
    if sink is not None and a.value is not None:
        out_lines = []
        for ln in sink:
            head, rest = ln.split(' mod=', 1)
            typ = head.rsplit(' ', 1)[-1]
            v = repr(float(a.value)) if typ == 'f32' else str(int(a.value))
            out_lines.append(f'{head} mod={v} {rest.split(" ", 1)[1]}')
        sink[:] = out_lines
    if sink is not None:
        for ln in sink:
            print('  ' + ln.rsplit(' ', 1)[-1] + ' = ' + ln.split(' mod=', 1)[1].split(' ', 1)[0])
        mp = ROOT / 'game' / 'mod_patch.txt'
        mp.parent.mkdir(parents=True, exist_ok=True)
        mp.write_text('# romfs/oodle location, type, mod=<value>, name (tools/mod_report.py --apply; '
                      'runtime/data_patch.c, MOD_VALUES=1)\n' + '\n'.join(sink) + '\n', encoding='utf-8')
        say(f'{len(sink)} values written to {mp}: start the game with MOD_VALUES=1 to use them')
        print(f'{len(sink)} values written to {mp}')
    say('=' * 72)
    rep = ROOT / 'game' / 'report' / f'mods-{"+".join(m.name for m in a.mod)[:80]}.txt'
    rep.parent.mkdir(parents=True, exist_ok=True)
    rep.write_text('\n'.join(out) + '\n', encoding='utf-8')
    for ln in out:   # the summaries and the comparison on screen; every file line is in the report
        if ln.startswith(('summary', '=', 'mod ', 'file types', 'comparison')) or ' / ' in ln[:80] or \
                ln.startswith(('    differs', '    only in')) or 'packed originals added' in ln:
            print(ln)
    print(f'full report: {rep}')


if __name__ == '__main__':
    main()
