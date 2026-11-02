"""Report what the game's packed file system (arc/data.trpfs with its table arc/data.trpfd) holds, by names and numbers.

Purpose: find the data files that set LOD and view distances (docs/progress.md 2026-11-02). The game loads its data
from one big archive; files in it are known only by a 64-bit FNV-1a hash of their path. This tool

  1. reads the table (data.trpfd): file hashes, the pack each one is in, pack names and sizes;
  2. reads the archive header (data.trpfs): where each pack starts;
  3. collects path-like strings from the ExeFS modules (main, subsdk*), hashes them and reports which name a file of
     the archive (the names the game's code uses itself);
  4. for packs and named files whose name has a keyword (lod, dist, cull, ...), reads the pack's table of contents and
     reports per file: compression (none or the Oodle codec), packed and unpacked size.

It prints names, counts and sizes only, never file contents, and writes the full report to
game/report/trpfs-report.txt.

--watch REGEX: for the packs whose name (or "index:name", so "^(12|34):" picks packs 12 and 34) matches, writes the stored (not packed) files to game/dump and lists the Oodle
ones in game/data_watch.txt; a run with TOTK_DATA_WATCH=1 (runtime/data_watch.c) dumps those when the game unpacks
them. Everything stays under game/ on the player's PC.
--view: writes game/data_patch.txt, the romfs.bin offsets of the far fade distances (ModelDitherFadeDefaultParam) and
the create/destroy distances (ObjectGenerationRange), which runtime/data_patch.c scales by TOTK_VIEW_DIST /
TOTK_SPAWN_DIST as the game reads them.
--report: field names and numbers of the dumped files, read with a schema (.bfbs) from the same pack, to
game/report/data-dump-report.txt.

Formats (Game Freak "Trinity" engine, all FlatBuffers; field order as community tools describe it):
  data.trpfd  root { file_hashes: [u64], pack_names: [string], file_info: [{pack_index: u64}], pack_info:
              [{size: u64, file_count: u64}] }
  data.trpfs  "ONEPACK\\0", u64 offset of root { pack_hashes: [u64], pack_offsets: [u64] }; pack_hashes are the
              FNV-1a hashes of pack_names
  pack        root { file_hashes: [u64], files: [{?, compression: i8 (-1 = none), level: u8, size: u64 (unpacked),
              data: [u8]}] }

    .venv\\Scripts\\python tools\\trpfs.py
    .venv\\Scripts\\python tools\\trpfs.py --keywords lod,dist,cull,far
    .venv\\Scripts\\python tools\\trpfs.py --watch "dither_fade|generation_range"
    .venv\\Scripts\\python tools\\trpfs.py --report
    .venv\\Scripts\\python tools\\trpfs.py --view

Standard library only.
"""
import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

from nso import parse  # noqa: E402

ROMFS = ROOT / 'game' / 'romfs'
EXEFS = ROOT / 'game' / 'extracted' / 'update' / 'exefs'
KEYWORDS = ('lod,dist,cull,draw,view,far,near,range,visib,level_of,detail,render,camera,fog,stream,quality,param,'
            'config,fade,dither,spawn,generat')
# Oodle codec ids from the second byte of a compressed block header (byte 0 is 0x8C or 0xCC)
OODLE = {0x06: 'oodle-kraken', 0x0A: 'oodle-mermaid', 0x0B: 'oodle-selkie', 0x0C: 'oodle-hydra', 0x0D: 'oodle-leviathan'}


def fnv1a64(text):
    h = 0xCBF29CE484222645
    for b in text.encode('utf-8'):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Table:
    """One FlatBuffers table in buf at pos: fields by index, read lazily."""

    def __init__(self, buf, pos):
        self.buf, self.pos = buf, pos
        vt = pos - struct.unpack_from('<i', buf, pos)[0]
        vt_size = struct.unpack_from('<H', buf, vt)[0]
        self.slots = [struct.unpack_from('<H', buf, vt + 4 + 2 * i)[0] for i in range((vt_size - 4) // 2)]

    @classmethod
    def root(cls, buf, at=0):
        return cls(buf, at + struct.unpack_from('<I', buf, at)[0])

    def has(self, i):
        return i < len(self.slots) and self.slots[i] != 0

    def scalar(self, i, fmt, default=0):
        return struct.unpack_from('<' + fmt, self.buf, self.pos + self.slots[i])[0] if self.has(i) else default

    def _vec(self, i):
        if not self.has(i):
            return 0, 0
        at = self.pos + self.slots[i]
        v = at + struct.unpack_from('<I', self.buf, at)[0]
        return v + 4, struct.unpack_from('<I', self.buf, v)[0]

    def vec_len(self, i):
        return self._vec(i)[1]

    def u64s(self, i):
        start, n = self._vec(i)
        return list(struct.unpack_from(f'<{n}Q', self.buf, start)) if n else []

    def tables(self, i):
        start, n = self._vec(i)
        return [Table(self.buf, start + 4 * k + struct.unpack_from('<I', self.buf, start + 4 * k)[0]) for k in range(n)]

    def strings(self, i):
        start, n = self._vec(i)
        out = []
        for k in range(n):
            s = start + 4 * k + struct.unpack_from('<I', self.buf, start + 4 * k)[0]
            ln = struct.unpack_from('<I', self.buf, s)[0]
            out.append(self.buf[s + 4:s + 4 + ln].decode('utf-8', 'replace'))
        return out

    def strings_at(self, i):
        """String field i."""
        if not self.has(i):
            return ''
        at = self.pos + self.slots[i]
        s = at + struct.unpack_from('<I', self.buf, at)[0]
        return self.buf[s + 4:s + 4 + struct.unpack_from('<I', self.buf, s)[0]].decode('utf-8', 'replace')

    def bytes_at(self, i, n):
        """The first n bytes of byte vector field i (only for a codec check, never printed)."""
        start, ln = self._vec(i)
        return self.buf[start:start + min(n, ln)], ln


ASSET_TYPES = {'.bntx', '.trmdl', '.trmbf', '.trmsh', '.trskl', '.tracm', '.trcma', '.truiv', '.truis', '.ptcl',
               '.tracp', '.trtbf', '.trins', '.trmtr', '.trmmt', '.tranm', '.trtml', '.bk2', '.wem', '.bnk'}


def inner_type(pack_name):
    """Type of the pack's main file: the extension before .trpak."""
    return Path(pack_name.removesuffix('.trpak')).suffix.lower() or '(none)'


def codec(entry):
    """Compression of one packed file: the byte vector is the last field; field 1 is the type (i8, -1 = none)."""
    comp = entry.scalar(1, 'b', -1)
    if comp == -1:
        return 'none'
    head, _ = entry.bytes_at(len(entry.slots) - 1, 2)
    if len(head) == 2 and head[0] in (0x8C, 0xCC):
        return OODLE.get(head[1], f'oodle id {head[1]}')
    return f'type {comp}'


def romfs_file(index, path):
    for e in index['files']:
        if e['path'] == path:
            return e['offset'], e['size']
    sys.exit(f'error: {path} is not in the RomFS index (run tools\\extract_romfs.py first)')


def code_strings(exefs):
    """Path-like ASCII strings from every ExeFS module (with and without a leading slash or drive prefix)."""
    pat = re.compile(rb'([A-Za-z0-9_\-./:%]{5,200})\x00')
    names = set()
    for p in sorted(exefs.iterdir()):
        if p.name != 'main' and not p.name.startswith('subsdk'):
            continue
        try:
            nso = parse(p.name, p.read_bytes())
        except Exception as e:  # noqa: BLE001
            print(f'note: {p.name} not read ({e})')
            continue
        for s in nso.strings(pat):
            if '/' not in s and '.' not in s:
                continue
            for v in (s, s.lstrip('/'), s.split(':', 1)[-1].lstrip('/')):
                names.add(v)
                names.add(v.lower())
                if v.endswith('/'):
                    names |= expand_dir(v)
    return names


# A code string ending in '/' is a folder the code appends a file name to; Game Freak's data folders hold files named
# after the folder (object_generation_range/object_generation_range_array.bin), so these guesses are hashed too.
DIR_SUFFIXES = ('', '_array', '_param', '_default_param', '_table', '_data', '_list', '_setting', '_config', 'debug',
                '_debug', '_default')


def expand_dir(d):
    stem = d.rstrip('/').rsplit('/', 1)[-1]
    out = {d.rstrip('/') + ext for ext in ('.bin', '.bfbs')}
    for suf in DIR_SUFFIXES:
        for ext in ('.bin', '.bfbs'):
            out.add(f'{d}{stem}{suf}{ext}')
    return out


DUMP = ROOT / 'game' / 'dump'
WATCH_LIST = ROOT / 'game' / 'data_watch.txt'
HEAD = 4096   # packed bytes the runtime's fingerprint covers (runtime/data_watch.c)


def watch(regex, pack_names, pack_info, toc, read, fs_off, offset_of):
    """Files of the matching packs: stored ones are written to game/dump now, Oodle ones are listed for the runtime."""
    pat = re.compile(regex, re.I)
    packs = [i for i, n in enumerate(pack_names) if pat.search(f'{i}:{n}')]   # name or "index:" (e.g. "^1234:")
    print(f'packs matching {regex}: {len(packs)}')
    DUMP.mkdir(parents=True, exist_ok=True)
    lines, stored, listed = ['# packed unpacked fnv1a64-of-first-4KiB name (tools/trpfs.py --watch; runtime/data_watch.c)'], 0, 0
    for i in packs[:2000]:
        t = toc(i)
        if not t:
            print(f'  pack {i}: too big, left out')
            continue
        stem = pack_names[i].removeprefix('arc/').removesuffix('.trpak')[:200]
        for h, e in t.items():
            start, packed = e._vec(len(e.slots) - 1)
            name = f'{stem}__{h:016x}'
            if codec(e) == 'none':
                (DUMP / name).write_bytes(e.buf[start:start + packed])
                stored += 1
            else:
                head = e.buf[start:start + min(packed, HEAD)]
                lines.append(f'{packed} {e.scalar(3, "Q")} {fnv1a64_bytes(head):016x} {name}')
                listed += 1
        print(f'  pack {i:6}  {len(t):4} files  {pack_names[i]}')
    WATCH_LIST.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{stored} stored files written to {DUMP}; {listed} Oodle files listed in {WATCH_LIST} for the runtime')


VIEW_PACKS = 'worlddata.*(model_dither_fade|object_generation_range)'
PATCH_LIST = ROOT / 'game' / 'data_patch.txt'
# (group, object name pattern, field name pattern): the fields runtime/data_patch.c scales
VIEW_FIELDS = [('fade', r'DitherFade', r'^Far(Begin|End)Distance$'),
               ('spawn', r'ObjectGenerationRange$', r'(Create|Destroy)Distance$')]
TYPE_NAMES = {11: 'f32', 7: 'i32', 8: 'u32', 5: 'i16', 6: 'u16'}


def walk_fields(sc, oi, buf, pos):
    """(object name, field name, base type, byte position) of every scalar field present in a table or struct."""
    o = sc.objects[oi]
    t = None if o['struct'] else Table(buf, pos)
    for f in o['fields']:
        b = f['base']
        if o['struct']:
            at = pos + f['offset']
        else:
            slot = (f['offset'] - 4) // 2
            if not t.has(slot):
                continue
            at = t.pos + t.slots[slot]
        if b in SCALAR:
            yield o['name'], f['name'], b, at
        elif b == OBJ:
            sub = sc.objects[f['index']]
            yield from walk_fields(sc, f['index'], buf, at if sub['struct'] else at + struct.unpack_from('<I', buf, at)[0])
        elif b == VECTOR and f['elem'] == OBJ:
            sub = sc.objects[f['index']]
            v = at + struct.unpack_from('<I', buf, at)[0]
            for k in range(struct.unpack_from('<I', buf, v)[0]):
                el = v + 4 + (k * sub['size'] if sub['struct'] else 4 * k)
                yield from walk_fields(sc, f['index'], buf,
                                       el if sub['struct'] else el + struct.unpack_from('<I', buf, el)[0])


def view(regex, pack_names, toc, fs_off, offset_of):
    """game/data_patch.txt: romfs.bin offset, type, group and name of every distance field of the matching packs."""
    pat = re.compile(regex, re.I)
    lines = ['# romfs <offset> <type> <group> <name> | oodle <packed> <unpacked> <fnv> <offset in file> <type> <group> '
             '<name> (tools/trpfs.py --view; runtime/data_patch.c)']
    for i, name in enumerate(pack_names):
        if not pat.search(name):
            continue
        t = toc(i)
        if not t:
            continue
        base = fs_off + offset_of[fnv1a64(name)]
        stem = name.removeprefix('arc/').removesuffix('.trpak')[:200]
        # (where, data): where is ('romfs', offset of the file in romfs.bin) for a stored file, or ('oodle', packed
        # size, unpacked size, fingerprint) for a packed one, read from the dump a DATA_WATCH=1 run wrote
        files, schemas, missing = [], [], 0
        for h, e in t.items():
            start, n = e._vec(len(e.slots) - 1)
            if codec(e) == 'none':
                data, where = e.buf[start:start + n], ('romfs', base + start)
            else:
                dump = DUMP / f'{stem}__{h:016x}'
                if not dump.is_file():
                    missing += 1
                    continue
                data = dump.read_bytes()
                where = ('oodle', n, e.scalar(3, 'Q'), fnv1a64_bytes(e.buf[start:start + min(n, HEAD)]))
            if data[4:8] == b'BFBS':
                schemas.append(Schema(data))
            else:
                files.append((where, data))
        print(f'pack {stem}: {len(t)} files, {len(schemas)} schemas, {len(files)} data files'
              + (f', {missing} packed files without a dump' if missing else ''))
        if missing:
            print('    a packed file has no dump in game/dump: run trpfs.py --watch for this pack, then the game once '
                  'with DATA_WATCH=1')
        for where, data in files:
            for sc in schemas:
                if sc.root is None:
                    print('    schema without a root table')
                    continue
                try:
                    found = list(walk_fields(sc, sc.root, data, struct.unpack_from('<I', data, 0)[0]))
                except Exception as e:  # noqa: BLE001
                    print(f'    schema {sc.objects[sc.root]["name"]} does not fit a {len(data)}-byte file: '
                          f'{type(e).__name__} {e}')
                    continue
                print(f'    {len(data)} bytes ({where[0]}), schema {sc.objects[sc.root]["name"]}: {len(found)} fields')
                for obj, field, b, at in found:
                    for group, opat, fpat in VIEW_FIELDS:
                        if re.search(opat, obj) and re.search(fpat, field) and b in TYPE_NAMES:
                            value = struct.unpack_from('<' + SCALAR[b], data, at)[0]
                            if where[0] == 'romfs':
                                lines.append(f'romfs {where[1] + at:x} {TYPE_NAMES[b]} {group} {obj}.{field}')
                            else:
                                lines.append(f'oodle {where[1]} {where[2]} {where[3]:016x} {at:x} {TYPE_NAMES[b]} '
                                             f'{group} {obj}.{field}')
                            print(f'      {group:5} {obj}.{field} = {fmt(value)}')
                break
    PATCH_LIST.parent.mkdir(parents=True, exist_ok=True)
    PATCH_LIST.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{len(lines) - 1} fields written to {PATCH_LIST}')


def fnv1a64_bytes(data):
    h = 0xCBF29CE484222645
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# FlatBuffers reflection (reflection.fbs) base types
SCALAR = {2: '?', 3: 'b', 4: 'B', 5: 'h', 6: 'H', 7: 'i', 8: 'I', 9: 'q', 10: 'Q', 11: 'f', 12: 'd', 1: 'B'}
STRING, VECTOR, OBJ, UNION, ARRAY = 13, 14, 15, 16, 17


class Schema:
    """A .bfbs file: objects with their fields (name, base type, element type, object index, vtable slot or struct
    offset), and the root table."""

    def __init__(self, buf):
        root = Table.root(buf)
        self.objects = []
        for o in root.tables(0):
            fields = []
            for f in o.tables(1):
                t = Table(f.buf, f.pos + f.slots[1] + struct.unpack_from('<I', f.buf, f.pos + f.slots[1])[0])
                fields.append(dict(name=f.strings_at(0), base=t.scalar(0, 'b'), elem=t.scalar(1, 'b'),
                                   index=t.scalar(2, 'i', -1), offset=f.scalar(3, 'H'),
                                   fixed=t.scalar(3, 'H')))
            self.objects.append(dict(name=o.strings_at(0), fields=sorted(fields, key=lambda x: x['offset']),
                                     struct=bool(o.scalar(2, '?')), size=o.scalar(4, 'i')))
        r = root.slots[4] if root.has(4) else 0
        at = root.pos + r
        self.root = None
        if r:
            pos = at + struct.unpack_from('<I', buf, at)[0]
            name = Table(buf, pos).strings_at(0)
            self.root = next((k for k, o in enumerate(self.objects) if o['name'] == name), None)


def fmt(v):
    return f'{v:.6g}' if isinstance(v, float) else str(v)


def show_table(sc, oi, buf, pos, depth, rows, say):
    """Fields of a table (pos) or struct (pos = its first byte) of object oi."""
    o = sc.objects[oi]
    pad = '  ' * depth
    t = None if o['struct'] else Table(buf, pos)
    for f in o['fields']:
        b, name = f['base'], f['name']
        if o['struct']:
            at = pos + f['offset']
        else:
            slot = (f['offset'] - 4) // 2
            if not t.has(slot):
                continue   # default value
            at = t.pos + t.slots[slot]
        if b in SCALAR:
            say(f'{pad}{name} = {fmt(struct.unpack_from("<" + SCALAR[b], buf, at)[0])}')
        elif b == STRING:
            s = at + struct.unpack_from('<I', buf, at)[0]
            n = struct.unpack_from('<I', buf, s)[0]
            say(f'{pad}{name} = "{buf[s + 4:s + 4 + min(n, 60)].decode("utf-8", "replace")}"')
        elif b == OBJ:
            sub = sc.objects[f['index']]
            say(f'{pad}{name} ({sub["name"]}):')
            show_table(sc, f['index'], buf, at if sub['struct'] else at + struct.unpack_from('<I', buf, at)[0],
                       depth + 1, rows, say)
        elif b == VECTOR:
            v = at + struct.unpack_from('<I', buf, at)[0]
            n = struct.unpack_from('<I', buf, v)[0]
            e = f['elem']
            if e in SCALAR:
                fm = SCALAR[e]
                vals = struct.unpack_from(f'<{n}{fm}', buf, v + 4)
                more = f' ... ({n})' if n > 8 * rows else ''
                say(f'{pad}{name} [{n}] = ' + ', '.join(fmt(x) for x in vals[:8 * rows]) + more)
            elif e == OBJ:
                sub = sc.objects[f['index']]
                say(f'{pad}{name} [{n} x {sub["name"]}]')
                for k in range(min(n, rows)):
                    say(f'{pad}  [{k}]')
                    if sub['struct']:
                        show_table(sc, f['index'], buf, v + 4 + k * sub['size'], depth + 2, rows, say)
                    else:
                        el = v + 4 + 4 * k
                        show_table(sc, f['index'], buf, el + struct.unpack_from('<I', buf, el)[0], depth + 2, rows, say)
            else:
                say(f'{pad}{name} [{n}] (element type {e})')
        else:
            say(f'{pad}{name}: type {b}')


def dump_report(rows):
    """Field names and numbers of the dumped files, read with a schema (.bfbs) of the same pack."""
    if not DUMP.is_dir():
        sys.exit(f'error: {DUMP} is empty: run tools\\trpfs.py --watch and then the game with DATA_WATCH=1 first')
    files = sorted(p for p in DUMP.iterdir() if p.is_file())
    schemas, data = {}, []
    for p in files:
        buf = p.read_bytes()
        if buf[4:8] == b'BFBS':
            try:
                schemas.setdefault(p.name.rsplit('__', 1)[0], []).append(Schema(buf))
            except Exception as e:  # noqa: BLE001
                print(f'{p.name}: schema not read ({e})')
        else:
            data.append((p, buf))
    out = ['=' * 25 + ' KarPur data dump report ' + '=' * 22,
           f'{len(files)} files in {DUMP}: {sum(len(v) for v in schemas.values())} schemas, {len(data)} data files']

    def say(line):
        out.append(line)

    for p, buf in data:
        stem = p.name.rsplit('__', 1)[0]
        say('')
        say(f'{p.name} ({len(buf)} bytes)')
        done = False
        for sc in schemas.get(stem, []):
            if sc.root is None:
                continue
            try:
                lines = []
                show_table(sc, sc.root, buf, struct.unpack_from('<I', buf, 0)[0], 1, rows, lines.append)
                say(f'  schema root {sc.objects[sc.root]["name"]}:')
                out.extend(lines)
                done = True
                break
            except Exception as e:  # noqa: BLE001
                say(f'  schema {sc.objects[sc.root]["name"]} does not fit ({type(e).__name__})')
        if not done:
            try:
                t = Table.root(buf)
                say(f'  no schema; root table with {len(t.slots)} fields')
            except Exception:  # noqa: BLE001
                say('  no schema; not a FlatBuffer')
    out.append('=' * 72)
    text = '\n'.join(out) + '\n'
    print(text)
    report = ROOT / 'game' / 'report' / 'data-dump-report.txt'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(text, encoding='utf-8')
    print(f'full report: {report}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--romfs', type=Path, default=ROMFS)
    ap.add_argument('--exefs', type=Path, default=EXEFS)
    ap.add_argument('--keywords', default=KEYWORDS, help='comma-separated, matched case-insensitively in names')
    ap.add_argument('--names', type=Path, help='extra candidate paths, one per line (e.g. from a modding wiki)')
    ap.add_argument('--grep', help='list the packs whose name matches this regular expression instead of keywords')
    ap.add_argument('--all-types', action='store_true', help='keyword list includes models, textures, animations, UI')
    ap.add_argument('--watch', metavar='REGEX', help='packs whose name matches: unpacked files to game/dump, Oodle '
                    'ones to the list game/data_watch.txt for the runtime (TOTK_DATA_WATCH=1)')
    ap.add_argument('--view', nargs='?', const=VIEW_PACKS, metavar='REGEX',
                    help='write game/data_patch.txt, the distance fields for TOTK_VIEW_DIST / TOTK_SPAWN_DIST')
    ap.add_argument('--report', action='store_true', help='field names and numbers of the files in game/dump')
    ap.add_argument('--rows', type=int, default=4, help='--report: elements shown per vector (default 4)')
    ap.add_argument('--max-pack-mb', type=int, default=256, help='largest pack whose table of contents is read')
    a = ap.parse_args()
    if a.report:
        return dump_report(a.rows)
    index_path = a.romfs / 'index.json'
    image_path = a.romfs / 'romfs.bin'
    if not index_path.is_file() or not image_path.is_file():
        sys.exit(f'error: {a.romfs} has no index.json/romfs.bin: run tools\\extract_romfs.py first')
    index = json.loads(index_path.read_text(encoding='utf-8'))
    keys = [k.strip().lower() for k in a.keywords.split(',') if k.strip()]
    hit = lambda name: any(k in name.lower() for k in keys)  # noqa: E731
    out = []

    def say(line=''):
        print(line)
        out.append(line)

    def note(line):   # report file only
        out.append(line)

    img = image_path.open('rb')

    def read(off, n):
        img.seek(off)
        return img.read(n)

    # 1. the table
    fd_off, fd_size = romfs_file(index, 'arc/data.trpfd')
    fd = Table.root(read(fd_off, fd_size))
    file_hashes, pack_names = fd.u64s(0), fd.strings(1)
    file_pack = [t.scalar(0, 'Q') for t in fd.tables(2)]
    pack_info = [(t.scalar(0, 'Q'), t.scalar(1, 'Q')) for t in fd.tables(3)]
    say('=' * 26 + ' KarPur trpfs report ' + '=' * 25)
    say(f'data.trpfd: {len(file_hashes)} files, {len(pack_names)} packs, {len(file_pack)} file entries, '
        f'{len(pack_info)} pack entries, root fields {len(fd.slots)}')
    if not (len(file_hashes) == len(file_pack) and len(pack_names) == len(pack_info)):
        say('WARNING: counts do not pair up; the field order differs from what this tool expects')

    # 2. the archive header
    fs_off, fs_size = romfs_file(index, 'arc/data.trpfs')
    magic, root_at = struct.unpack('<8sQ', read(fs_off, 16))
    say(f'data.trpfs: {fs_size / 2**30:.2f} GiB, magic {"ok" if magic.startswith(b"ONEPACK") else "UNEXPECTED"}, '
        f'table at {root_at:#x}')
    fs = Table.root(read(fs_off + root_at, fs_size - root_at))
    pack_hashes, pack_offsets = fs.u64s(0), fs.u64s(1)
    offset_of = dict(zip(pack_hashes, pack_offsets))
    named = sum(1 for n in pack_names if fnv1a64(n) in offset_of)
    say(f'archive: {len(pack_hashes)} packs; {named} of {len(pack_names)} pack names hash to a pack '
        f'({"FNV-1a confirmed" if named else "hash function differs"})')
    # A pack is named after its main file: "arc/" + that file's path without slashes + ".trpak", so the ending
    # before .trpak is the main file's type.
    kinds = Counter(inner_type(n) for n in pack_names)
    kind_size = Counter()
    for i, n in enumerate(pack_names):
        kind_size[inner_type(n)] += pack_info[i][0] if i < len(pack_info) else 0
    say(f'main file types of the packs ({len(kinds)}; packs, MiB):')
    for k, c in sorted(kinds.items(), key=lambda kc: -kc[1]):
        say(f'  {c:6}  {kind_size[k] / 2**20:9.1f}  {k}')

    toc_cache = {}

    def toc(pi):
        if pi not in toc_cache:
            off, size = offset_of.get(fnv1a64(pack_names[pi])), pack_info[pi][0]
            if off is None or size > a.max_pack_mb << 20:
                toc_cache[pi] = None
            else:
                pk = Table.root(read(fs_off + off, size))
                toc_cache[pi] = dict(zip(pk.u64s(0), pk.tables(1)))
        return toc_cache[pi]

    if a.view:
        return view(a.view, pack_names, toc, fs_off, offset_of)
    if a.watch:
        return watch(a.watch, pack_names, pack_info, toc, read, fs_off, offset_of)

    # 3. names from the code
    hashes = {h: i for i, h in enumerate(file_hashes)}
    names = code_strings(a.exefs) if a.exefs.is_dir() else set()
    if a.names:
        names |= {ln.strip() for ln in a.names.read_text(encoding='utf-8').splitlines() if ln.strip()}
    found = {}
    for s in names:
        h = fnv1a64(s)
        if h in hashes and h not in found:
            found[h] = s
    say(f'code strings tried: {len(names)}; names of archive files found: {len(found)}')
    folders = Counter(s.rsplit('/', 1)[0] if '/' in s else '(none)' for s in found.values())
    say('folders of found names: ' + ', '.join(f'{f} {c}' for f, c in folders.most_common(20)))

    # 4. keyword matches with their compression
    say()
    say(f'keywords: {",".join(keys)}')
    grep = re.compile(a.grep, re.I) if a.grep else None
    kw_packs = [i for i, n in enumerate(pack_names)
                if (grep.search(n) if grep else hit(n) and (a.all_types or inner_type(n) not in ASSET_TYPES))]
    say(f'packs whose name {"matches " + a.grep if grep else "has a keyword (asset types left out)"}: '
        f'{len(kw_packs)} (all in the report file, the first 60 here)')
    for k, i in enumerate(kw_packs):
        size, count = pack_info[i] if i < len(pack_info) else (0, 0)
        (say if k < 60 else note)(f'  pack {i:6}  {count:5} files  {size / 2**20:9.2f} MiB  {pack_names[i]}')

    # compression of the files in every 50th pack (the file headers only)
    sample = Counter()
    for i in range(0, len(pack_names), 50):
        t = toc(i) if pack_info[i][0] <= 4 << 20 else None   # small packs only, to keep the run short
        for e in (t or {}).values():
            sample[codec(e)] += 1
    say('compression in a sample (every 50th pack up to 4 MiB): ' + ', '.join(f'{k} {c}' for k, c in sample.most_common()))
    kw_files = sorted((s, h) for h, s in found.items() if hit(s))
    say(f'found file names with a keyword: {len(kw_files)}')
    codecs = Counter()
    for s, h in kw_files[:300]:
        pi = file_pack[hashes[h]]
        t = toc(pi)
        entry = t.get(h) if t else None
        if entry is None:
            say(f'  {s}  (pack {pi}: {"too big or missing" if t is None else "hash not in pack"})')
            continue
        kind, packed, size = codec(entry), entry.bytes_at(len(entry.slots) - 1, 0)[1], entry.scalar(3, 'Q')
        codecs[kind] += 1
        say(f'  {s}  pack {pi} ({pack_names[pi].removeprefix("arc/")}), {kind}, packed {packed} B, unpacked {size} B')
    if codecs:
        say('compression of these files: ' + ', '.join(f'{k} {c}' for k, c in codecs.most_common()))
    say('=' * 72)
    report = ROOT / 'game' / 'report' / 'trpfs-report.txt'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print(f'full report: {report}')


if __name__ == '__main__':
    main()
