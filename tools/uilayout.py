"""Report the structure of the game's UI files that runtime/data_watch.c dumped to game/dump, by names and numbers.

Purpose: find how the main menu (X menu) and the settings page are built, to add a "PC settings" entry and hide the
downloadable-content panel (docs/progress.md 2026-11-03). For each file given (or every SARC / AHTB file in game/dump):

- SARC (Nintendo archive): its files with size and format magic; the layouts and animations inside are reported too;
- FLYT (nn::ui2d layout, .bflyt): the pane tree (type and name per pane, indented by pas1/pae1), the groups with their
  member panes, the parts (embedded layouts) and the text panes' label ids;
- FLAN (nn::ui2d animation, .bflan): the animation's tag name, its groups and the panes it animates;
- AHTB (Game Freak label table, .tbl of a message file): the label names, which identify the texts of a menu.

It prints names, types, counts and sizes, never textures, texts or other content.

  .venv\\Scripts\\python tools\\uilayout.py
  .venv\\Scripts\\python tools\\uilayout.py game\\dump\\applidataresidentresident_xmenu_00.arc__510007e14b0aca3d
  .venv\\Scripts\\python tools\\uilayout.py --flan        (animations too, which is long)

Formats as the community tools describe them (Switch layouts, little endian).
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DUMP = ROOT / 'game' / 'dump'
PANE_TYPES = {b'pan1', b'pic1', b'txt1', b'wnd1', b'bnd1', b'prt1', b'scr1', b'ali1', b'cpt1'}


def cstr(b, off, n):
    """NUL-terminated ASCII at b[off:off+n] (other bytes shown as '?')."""
    raw = b[off:off + n].split(b'\0', 1)[0]
    return ''.join(chr(c) if 32 <= c < 127 else '?' for c in raw)


def sarc_files(b):
    """[(name, data)] of a SARC archive."""
    if b[:4] != b'SARC':
        raise ValueError('not a SARC')
    hdr = struct.unpack_from('<H', b, 4)[0]
    data_off = struct.unpack_from('<I', b, 0x0C)[0]
    sfat = hdr
    if b[sfat:sfat + 4] != b'SFAT':
        raise ValueError('no SFAT')
    sfat_hdr, count = struct.unpack_from('<HH', b, sfat + 4)
    nodes = [struct.unpack_from('<IIII', b, sfat + sfat_hdr + 16 * i) for i in range(count)]
    sfnt = sfat + sfat_hdr + 16 * count
    if b[sfnt:sfnt + 4] != b'SFNT':
        raise ValueError('no SFNT')
    names = sfnt + struct.unpack_from('<H', b, sfnt + 4)[0]
    out = []
    for _, attr, start, end in nodes:
        name = cstr(b, names + (attr & 0xFFFF) * 4, 256) if attr >> 24 else f'<hash {_:08x}>'
        out.append((name, b[data_off + start:data_off + end]))
    return out


def sections(b, magic):
    if b[:4] != magic:
        raise ValueError(f'not {magic!r}')
    hdr = struct.unpack_from('<H', b, 6)[0]
    version = struct.unpack_from('<I', b, 8)[0]
    count = struct.unpack_from('<H', b, 0x10)[0]
    off, out = hdr, []
    for _ in range(count):
        if off + 8 > len(b):
            break
        kind, size = b[off:off + 4], struct.unpack_from('<I', b, off + 4)[0]
        out.append((kind, off, size))
        if size < 8:
            break
        off += size
    return version, out


def flyt_report(b, out):
    version, secs = sections(b, b'FLYT')
    depth, groups, gdepth = 0, [], 0
    out.append(f'    layout version {version >> 24}.{version >> 16 & 255}.{version >> 8 & 255}, {len(secs)} sections')
    counts = {}
    for kind, off, size in secs:
        k = kind.decode('ascii', 'replace')
        counts[k] = counts.get(k, 0) + 1
        if kind == b'lyt1':
            w, h = struct.unpack_from('<ff', b, off + 0x0C)
            out.append(f'    lyt1 {w:.0f}x{h:.0f} "{cstr(b, off + 0x1C, 64)}"')
        elif kind == b'pas1':
            depth += 1
        elif kind == b'pae1':
            depth -= 1
        elif kind in PANE_TYPES:
            name = cstr(b, off + 0x0C, 24)
            w, h = struct.unpack_from('<ff', b, off + 0x44)
            extra = ''
            if kind == b'prt1':   # the part's layout name ends the section
                tail = b[off + size - 64:off + size].rstrip(b'\0')
                extra = f' part "{cstr(tail, tail.rfind(bytes([0])) + 1, 64)}"'
            elif kind == b'txt1':   # label id (text id) of the text pane, an offset from the section start (v8+)
                tid = struct.unpack_from('<I', b, off + 0x54 + 0x3C)[0] if size > 0x54 + 0x40 else 0
                if 0 < tid < size:
                    extra = f' text id "{cstr(b, off + tid, 64)}"'
            out.append(f'    {"  " * depth}{k} {name} ({w:.0f}x{h:.0f}){extra}')
        elif kind == b'grs1':
            gdepth += 1
        elif kind == b'gre1':
            gdepth -= 1
        elif kind == b'grp1':
            name = cstr(b, off + 8, 34)
            n = struct.unpack_from('<H', b, off + 8 + 34)[0]
            members = [cstr(b, off + 8 + 36 + 24 * i, 24) for i in range(min(n, 64))]
            groups.append(f'    {"  " * gdepth}group {name}: {n} panes' + (f' ({", ".join(members[:24])}'
                                                                           f'{", ..." if n > 24 else ""})' if n else ''))
    out.append('    sections: ' + ', '.join(f'{k} {v}' for k, v in sorted(counts.items())))
    out.extend(groups)


def flan_report(b, out):
    _, secs = sections(b, b'FLAN')
    for kind, off, size in secs:
        if kind == b'pat1':
            name = cstr(b, off + 0x1C, 64)
            out.append(f'    animation tag "{name}"')
        elif kind == b'pai1':
            n = struct.unpack_from('<H', b, off + 0x0E)[0]
            out.append(f'    animates {n} targets')


def ahtb_report(b, out):
    if b[:4] != b'AHTB':
        raise ValueError('not AHTB')
    n = struct.unpack_from('<I', b, 4)[0]
    off, labels = 8, []
    for _ in range(n):
        if off + 10 > len(b):
            break
        ln = struct.unpack_from('<H', b, off + 8)[0]
        labels.append(cstr(b, off + 10, ln))
        off += 10 + ln
    out.append(f'    {len(labels)} labels: ' + ', '.join(labels))


def report_file(name, b, out, flan):
    magic = b[:4]
    if magic == b'SARC':
        files = sarc_files(b)
        out.append(f'  SARC: {len(files)} files')
        for fname, data in files:
            out.append(f'   {fname}  {len(data)} B  {data[:4].decode("ascii", "replace")!r}')
        for fname, data in files:
            if data[:4] == b'FLYT':
                out.append(f'  layout {fname}:')
                flyt_report(data, out)
            elif data[:4] == b'FLAN' and flan:
                out.append(f'  animation {fname}:')
                flan_report(data, out)
            elif data[:4] == b'SARC':
                out.append(f'  archive {fname}:')
                report_file(fname, data, out, flan)
    elif magic == b'AHTB':
        out.append('  AHTB label table:')
        ahtb_report(b, out)
    else:
        out.append(f'  {len(b)} B, magic {magic!r}: not reported')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('files', nargs='*', type=Path)
    ap.add_argument('--flan', action='store_true', help='report the animations inside archives too')
    a = ap.parse_args()
    files = a.files or sorted(p for p in DUMP.glob('*') if p.is_file() and p.read_bytes()[:4] in (b'SARC', b'AHTB'))
    if not files:
        sys.exit(f'error: nothing to report in {DUMP}: run tools\\trpfs.py --watch and the game with TOTK_DATA_WATCH=1')
    out = ['=' * 72]
    for p in files:
        out.append(f'{p.name} ({p.stat().st_size} B)')
        try:
            report_file(p.name, p.read_bytes(), out, a.flan)
        except (ValueError, struct.error, IndexError) as e:
            out.append(f'  not read: {e}')
        out.append('')
    out.append('=' * 72)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
