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

Nothing of the mod or the game is written outside game/; the report goes to game/report/mod-<name>.txt.

    .venv\\Scripts\\python tools\\mod_report.py C:\\KarPur\\mods\\improved_lod
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
from trpfs import (HEAD, SCALAR, Schema, Table, codec, fmt, fnv1a64, fnv1a64_bytes, romfs_file,  # noqa: E402
                   walk_fields)

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
        return [(f'{o}.{f}', b, struct.unpack_from('<' + SCALAR[b], data, at)[0])
                for o, f, b, at in walk_fields(sc, sc.root, data, struct.unpack_from('<I', data, 0)[0])]
    except Exception:  # noqa: BLE001
        return None


def compare(orig, mod, schemas, say):
    for sc in schemas:
        a, b = fields_of(sc, orig), fields_of(sc, mod)
        if a is None or b is None:
            continue
        say(f'    schema {sc.objects[sc.root]["name"]}: {len(a)} fields in the game, {len(b)} in the mod')
        # fields in walk order, matched by name and occurrence
        seen_a, seen_b = Counter(), Counter()
        ka = {}
        for name, _, v in a:
            ka[(name, seen_a[name])] = v
            seen_a[name] += 1
        changed = 0
        for name, _, v in b:
            k = (name, seen_b[name])
            seen_b[name] += 1
            old = ka.get(k)
            if old is None:
                say(f'      {name}[{k[1]}]: (not in the game) -> {fmt(v)}')
                changed += 1
            elif old != v:
                say(f'      {name}[{k[1]}]: {fmt(old)} -> {fmt(v)}')
                changed += 1
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
    for i in diffs[:MAX_WORD_DIFFS]:
        fa, fb = struct.unpack('<f', struct.pack('<I', wa[i]))[0], struct.unpack('<f', struct.pack('<I', wb[i]))[0]
        say(f'      +{4 * i:#x}: float {fa:.6g} -> {fb:.6g}   int {wa[i]} -> {wb[i]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('mod', type=Path, help='the unpacked mod folder')
    ap.add_argument('--romfs', type=Path, default=trpfs.ROMFS)
    a = ap.parse_args()
    if not a.mod.is_dir():
        sys.exit(f'error: {a.mod} is not a folder (unpack the mod first)')
    index = json.loads((a.romfs / 'index.json').read_text(encoding='utf-8'))
    img = (a.romfs / 'romfs.bin').open('rb')

    def read(off, n):
        img.seek(off)
        return img.read(n)

    fd_off, fd_size = romfs_file(index, 'arc/data.trpfd')
    fd = Table.root(read(fd_off, fd_size))
    file_hashes, pack_names = fd.u64s(0), fd.strings(1)
    file_pack = [t.scalar(0, 'Q') for t in fd.tables(2)]
    pack_size = [t.scalar(0, 'Q') for t in fd.tables(3)]
    fs_off, fs_size = romfs_file(index, 'arc/data.trpfs')
    _, root_at = struct.unpack('<8sQ', read(fs_off, 16))
    fs = Table.root(read(fs_off + root_at, fs_size - root_at))
    offset_of = dict(zip(fs.u64s(0), fs.u64s(1)))
    where = {h: i for i, h in enumerate(file_hashes)}
    tocs = {}

    def toc(pi):
        if pi not in tocs:
            off = offset_of.get(fnv1a64(pack_names[pi]))
            tocs[pi] = None if off is None or pack_size[pi] > 256 << 20 else dict(
                zip(*(lambda t: (t.u64s(0), t.tables(1)))(Table.root(read(fs_off + off, pack_size[pi])))))
        return tocs[pi]

    out = []

    def say(line=''):
        print(line)
        out.append(line)

    files = mod_files(a.mod)
    say('=' * 25 + ' KarPur mod report ' + '=' * 28)
    say(f'mod {a.mod.name}: {len(files)} files')
    types = Counter(Path(p).suffix or '(none)' for p, _ in files)
    say('file types: ' + ', '.join(f'{t} {c}' for t, c in types.most_common()))
    watch_lines, unknown = [], 0
    mod_data = {p: f.read_bytes() for p, f in files}
    mod_schemas_by_dir = {}
    for p, d in mod_data.items():
        if d[4:8] == b'BFBS':
            mod_schemas_by_dir.setdefault(p.rsplit('/', 1)[0], []).append(d)
    for path, _ in files:
        data = mod_data[path]
        if path.endswith(('.trpfd', '.trpfs')):
            say(f'{path}: archive table of the mod (skipped)')
            continue
        h = fnv1a64(path)
        if h not in where:
            unknown += 1
            say(f'{path}: {len(data)} B, not a file of the game archive (new file or another path)')
            continue
        pi = file_pack[where[h]]
        t = toc(pi)
        e = t.get(h) if t else None
        stem = pack_names[pi].removeprefix('arc/').removesuffix('.trpak')[:200]
        if e is None:
            say(f'{path}: {len(data)} B, pack {pi} not readable')
            continue
        start, n = e._vec(len(e.slots) - 1)
        kind = codec(e)
        say(f'{path}: mod {len(data)} B, game {e.scalar(3, "Q") if kind != "none" else n} B ({kind}), pack {pi}')
        if kind == 'none':
            orig = e.buf[start:start + n]
        else:
            dump = trpfs.DUMP / f'{stem}__{h:016x}'
            if not dump.is_file():
                watch_lines.append(f'{n} {e.scalar(3, "Q")} {fnv1a64_bytes(e.buf[start:start + min(n, HEAD)]):016x} '
                                   f'{stem}__{h:016x}')
                say('    original is Oodle-packed and not dumped yet: listed for DATA_WATCH')
                continue
            orig = dump.read_bytes()
        # schemas: the mod's own beside the file, then the pack's (unpacked or dumped)
        cands = list(mod_schemas_by_dir.get(path.rsplit('/', 1)[0], []))
        for hh, ee in t.items():
            s0, n0 = ee._vec(len(ee.slots) - 1)
            if codec(ee) == 'none':
                cands.append(ee.buf[s0:s0 + n0])
            else:
                d = trpfs.DUMP / f'{stem}__{hh:016x}'
                if d.is_file():
                    cands.append(d.read_bytes())
        compare(orig, data, schemas_in(cands), say)
    if watch_lines:
        trpfs.WATCH_LIST.parent.mkdir(parents=True, exist_ok=True)
        old = trpfs.WATCH_LIST.read_text(encoding='utf-8').splitlines() if trpfs.WATCH_LIST.is_file() else []
        keep = [ln for ln in old if ln.strip() and not ln.startswith('#')]
        lines = ['# packed unpacked fnv1a64-of-first-4KiB name (tools/trpfs.py --watch, tools/mod_report.py)']
        lines += sorted(set(keep + watch_lines))
        trpfs.WATCH_LIST.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        say()
        say(f'{len(watch_lines)} packed originals added to {trpfs.WATCH_LIST}: start the game once with DATA_WATCH=1, '
            'walk around, then run this tool again')
    say(f'{unknown} mod files are not in the game archive')
    say('=' * 72)
    rep = ROOT / 'game' / 'report' / f'mod-{a.mod.name}.txt'
    rep.parent.mkdir(parents=True, exist_ok=True)
    rep.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print(f'full report: {rep}')


if __name__ == '__main__':
    main()
