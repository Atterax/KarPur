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
  .venv\\Scripts\\python tools\\uilayout.py --hide N_dlc_00 game\\dump\\applidataxmenustate_xmenu.truis__e683c072feaedbbf

--hide PANE writes game/ui_patch.txt lines that clear the pane's visible flag when the game unpacks that file
(runtime/data_patch.c, TOTK_UI_PATCH=1): packed and unpacked size, fingerprint, byte offset and new flags only.

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


def sarc_files(b, with_offsets=False):
    """[(name, data)] of a SARC archive; with_offsets: [(name, data, offset of data in b)]."""
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
        data = b[data_off + start:data_off + end]
        out.append((name, data, data_off + start) if with_offsets else (name, data))
    return out


def find_panes(b, pane, base=0, where=''):
    """[(layout path, offset of the pane's flags byte in the outermost file, flags)] for every pane named pane in the
    layouts of b (a SARC, nested SARCs, or a layout)."""
    hits = []
    if b[:4] == b'SARC':
        for name, data, off in sarc_files(b, True):
            hits += find_panes(data, pane, base + off, f'{where}/{name}' if where else name)
    elif b[:4] == b'FLYT':
        _, secs = sections(b, b'FLYT')
        for kind, off, _size in secs:
            if kind in PANE_TYPES and cstr(b, off + 0x0C, 24) == pane:
                hits.append((where, base + off + 8, b[off + 8]))
    return hits


PANE_FIELDS = {'x': 0x2C, 'y': 0x30, 'sx': 0x44, 'sy': 0x48, 'w': 0x4C, 'h': 0x50}


def find_pane_sections(b, pane, base=0, where=''):
    """[(layout path, offset of the pane section in the outermost file, section bytes)] for every pane named pane."""
    hits = []
    if b[:4] == b'SARC':
        for name, data, off in sarc_files(b, True):
            hits += find_pane_sections(data, pane, base + off, f'{where}/{name}' if where else name)
    elif b[:4] == b'FLYT':
        _, secs = sections(b, b'FLYT')
        for kind, off, size in secs:
            if kind in PANE_TYPES and cstr(b, off + 0x0C, 24) == pane:
                hits.append((where, base + off, b[off:off + size]))
    return hits


def watch_key(dump):
    watch = ROOT / 'game' / 'data_watch.txt'
    for line in watch.read_text().splitlines() if watch.is_file() else []:
        t = line.split()
        if len(t) == 4 and not line.startswith('#') and t[3] == dump.name:
            return t[:3]
    sys.exit(f'error: {watch} has no line for {dump.name} (run tools\\trpfs.py --watch for its pack again)')


def set_fields(dump, spec, out_path):
    """--set PANE:field=value,...: f32 lines that set a pane's position (x, y), scale (sx, sy) or size (w, h) when the
    game unpacks the file (game/ui_patch.txt, TOTK_UI_PATCH=1). In layout units: 1920x1080, origin at the centre,
    y up, relative to the parent pane."""
    pane, _, rest = spec.partition(':')
    changes = [kv.split('=', 1) for kv in rest.split(',') if '=' in kv]
    if not changes or any(k not in PANE_FIELDS for k, _ in changes):
        sys.exit(f'error: --set {spec}: expected PANE:field=value,... with fields {", ".join(PANE_FIELDS)}')
    hits = find_pane_sections(dump.read_bytes(), pane)
    if not hits:
        sys.exit(f'error: no pane {pane} in {dump.name}')
    key = watch_key(dump)
    lines = []
    for where, at, sec in hits:
        for k, v in changes:
            old = struct.unpack_from('<f', sec, PANE_FIELDS[k])[0]
            lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at + PANE_FIELDS[k]:x} f32 mod={float(v)} set:{pane}.{k}')
            print(f'{pane} in {where}: {k} {old:g} -> {float(v):g}')
    old = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                         '(TOTK_UI_PATCH=1); tools/uilayout.py --hide']
    tags = {l.rsplit(' ', 1)[-1] for l in lines}
    old = [l for l in old if l.rsplit(' ', 1)[-1] not in tags]
    out_path.write_text('\n'.join(old + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines for {pane} in {out_path}')


def swap_part(dump, spec, out_path):
    """--part PANE=LAYOUT: u8 lines that make the part pane PANE use another layout of the archive (its name ends the
    prt1 section, padded with zeros). The new name must not be longer than the old one."""
    pane, _, new = spec.partition('=')
    if not pane or not new:
        sys.exit(f'error: --part {spec}: expected PANE=LAYOUT')
    hits = [h for h in find_pane_sections(dump.read_bytes(), pane) if h[2][:4] == b'prt1']
    if not hits:
        sys.exit(f'error: no part pane {pane} in {dump.name}')
    key = watch_key(dump)
    lines = []
    for where, at, sec in hits:
        # prt1: the pane (0x54 with the header), u32 property count, f32 magnify x/y, 40 bytes per property, the name
        start = 0x54 + 12 + 40 * struct.unpack_from('<I', sec, 0x54)[0]
        body = sec.rstrip(b'\0')
        old = sec[start:].split(b'\0', 1)[0]
        room = len(sec) - start
        if len(new) + 1 > room:
            sys.exit(f'error: {pane} in {where}: "{new}" does not fit where "{old.decode()}" is ({room} bytes)')
        raw = new.encode() + b'\0' * (room - len(new))
        for j, c in enumerate(raw):
            if j >= len(old) and c == 0 and start + j >= len(body):
                break   # already zero
            lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at + start + j:x} u8 mod={c} part:{pane}')
        print(f'{pane} in {where}: part "{old.decode()}" -> "{new}" ({len(lines)} bytes)')
    old_lines = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                               '(TOTK_UI_PATCH=1)']
    old_lines = [l for l in old_lines if not l.endswith(f' part:{pane}')]
    out_path.write_text('\n'.join(old_lines + lines) + '\n', encoding='utf-8')


def set_keys(dump, specs, out_path):
    """--setkey OFFSET=VALUE: f32 lines that change animation key values (hermite keys; the offsets come from --anim).
    The X menu's list places are keys of xmenu_top_00_pattern_button.bflan, one frame per number of visible buttons."""
    b = dump.read_bytes()
    key = watch_key(dump)
    lines = []
    for spec in specs:
        off, _, val = spec.partition('=')
        at = int(off, 16)
        if not val or at + 4 > len(b):
            sys.exit(f'error: --setkey {spec}: expected OFFSET=VALUE inside the file')
        print(f'key at {at:x}: {struct.unpack_from("<f", b, at)[0]:g} -> {float(val):g}')
        lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at:x} f32 mod={float(val)} key:{at:x}')
    old = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                         '(TOTK_UI_PATCH=1)']
    tags = {l.rsplit(' ', 1)[-1] for l in lines}
    old = [l for l in old if l.rsplit(' ', 1)[-1] not in tags]
    out_path.write_text('\n'.join(old + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines in {out_path}')


def set_grid_rows(dump, specs, out_path):
    """--grid-row BUTTON=ROW: u8 lines that move a button to another row of a UIKit grid panel. The grid is not in the
    layout but in a small FlatBuffers file of the same .truis path (state_xmenu: 1480 bytes, GridPanel_00 of the X
    menu). Each grid cell is a table: soffset to its vtable, offset to the button's name (a u32 length and the
    characters), then u32 column, row, column span, row span. The cursor walks the rows in order, so swapping two
    rows swaps the cursor order without moving anything on screen."""
    b = dump.read_bytes()
    key = watch_key(dump)
    lines = []
    for spec in specs:
        name, _, row = spec.partition('=')
        hits = [at for at in range(0, len(b) - 24, 4)
                if struct.unpack_from('<I', b, at + 4)[0] == 0x14 and b[at + 0x18:at + 0x1C] == struct.pack('<I', len(name))
                and b[at + 0x1C:at + 0x1C + len(name) + 1] == name.encode() + b'\0']
        if len(hits) != 1:
            sys.exit(f'error: {len(hits)} grid cells named {name} in {dump.name} (expected one)')
        at = hits[0] + 12
        print(f'grid cell {name}: row {struct.unpack_from("<I", b, at)[0]} -> {int(row)}')
        lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at:x} u8 mod={int(row)} grid:{name}')
    old = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                         '(TOTK_UI_PATCH=1)']
    tags = {l.rsplit(' ', 1)[-1] for l in lines}
    old = [l for l in old if l.rsplit(' ', 1)[-1] not in tags and not l.endswith(' grid:row')]
    out_path.write_text('\n'.join(old + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines in {out_path}')


def rename_texture(dump, spec, out_path):
    """--texture LAYOUT:OLD=NEW: u8 lines that rename a texture in the texture list (txl1) of the layout LAYOUT, so its
    panes show another texture of the archive by default (the X menu buttons: the default icon; the others pick theirs
    with the patern_icon animation). NEW must have the length of OLD."""
    layout, _, rest = spec.partition(':')
    old, _, new = rest.partition('=')
    if not old or len(old) != len(new):
        sys.exit(f'error: --texture {spec}: expected LAYOUT:OLD=NEW with names of the same length')
    hits = []

    def walk(b, base):
        for name, data, off in sarc_files(b, True):
            if data[:4] == b'SARC':
                walk(data, base + off)
            elif data[:4] == b'FLYT' and name.endswith(f'/{layout}.bflyt'):
                _, secs = sections(data, b'FLYT')
                for kind, s, _ in secs:
                    if kind != b'txl1':
                        continue
                    for i in range(struct.unpack_from('<H', data, s + 8)[0]):
                        at = s + 12 + struct.unpack_from('<I', data, s + 12 + 4 * i)[0]
                        if cstr(data, at, 64) == old:
                            hits.append((name, base + off + at))
    walk(dump.read_bytes(), 0)
    if not hits:
        sys.exit(f'error: no texture {old} in the layout {layout} of {dump.name}')
    key = watch_key(dump)
    tag = f'texture:{layout}:{old}'
    lines = [f'oodle {key[0]} {key[1]} {key[2]} {at + j:x} u8 mod={c} {tag}'
             for _, at in hits for j, (a, c) in enumerate(zip(old.encode(), new.encode())) if a != c]
    for where, _ in hits:
        print(f'{where}: texture "{old}" -> "{new}"')
    old_lines = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                               '(TOTK_UI_PATCH=1)']
    old_lines = [l for l in old_lines if not l.endswith(' ' + tag)]
    out_path.write_text('\n'.join(old_lines + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines in {out_path}')


def hide(dump, pane, out_path):
    """Append to out_path the line that clears the visible bit of pane in the packed file dump (game/ui_patch.txt,
    read by runtime/data_patch.c with TOTK_UI_PATCH=1). The packed size, unpacked size and fingerprint come from the
    line of game/data_watch.txt that names the dump."""
    hits = find_panes(dump.read_bytes(), pane)
    if not hits:
        sys.exit(f'error: no pane {pane} in {dump.name}')
    watch = ROOT / 'game' / 'data_watch.txt'
    key = None
    for line in watch.read_text().splitlines() if watch.is_file() else []:
        t = line.split()
        if len(t) == 4 and not line.startswith('#') and t[3] == dump.name:
            key = t[:3]
    if not key:
        sys.exit(f'error: {watch} has no line for {dump.name} (run tools\\trpfs.py --watch for its pack again)')
    lines = []
    for where, at, flags in hits:
        if not flags & 1:
            print(f'{pane} in {where}: already hidden (flags {flags:#x})')
            continue
        lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at:x} u8 mod={flags & ~1} hide:{pane}')
        print(f'{pane} in {where}: flags {flags:#x} at +{at:#x} -> {flags & ~1:#x}')
    old = out_path.read_text(encoding='utf-8').splitlines() if out_path.is_file() else ['# UI bytes set by runtime/data_patch.c '
                                                                         '(TOTK_UI_PATCH=1); tools/uilayout.py --hide']
    old = [l for l in old if not l.endswith(f' hide:{pane}')]
    out_path.write_text('\n'.join(old + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines for {pane} in {out_path}')


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
            # pane: flags +8, origin +9, alpha +10, part scale +11, name +12 (24), user data +36 (8), translation
            # +44 (x, y, z), rotation +56, scale +68 (x, y), size +76 (w, h)
            name = cstr(b, off + 0x0C, 24)
            x, y = struct.unpack_from('<ff', b, off + 0x2C)
            w, h = struct.unpack_from('<ff', b, off + 0x4C)
            flags, alpha = b[off + 8], b[off + 10]
            extra = ''
            if kind == b'prt1':   # the part's layout name ends the section
                tail = b[off + size - 64:off + size].rstrip(b'\0')
                extra = f' part "{cstr(tail, tail.rfind(bytes([0])) + 1, 64)}"'
            elif kind == b'txt1':   # label id (text id) of the text pane, an offset from the section start (v8+)
                tid = struct.unpack_from('<I', b, off + 0x54 + 0x3C)[0] if size > 0x54 + 0x40 else 0
                if 0 < tid < size:
                    extra = f' text id "{cstr(b, off + tid, 64)}"'
            vis = '' if flags & 1 else ' hidden'
            out.append(f'    {"  " * depth}{k} {name} ({w:.0f}x{h:.0f} at {x:.0f},{y:.0f}, alpha {alpha}{vis}){extra}')
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


def flan_detail(b, out, base=0, where=''):
    """Every animated target of a BFLAN with its keys. pai1: u16 frames, u8 loop, u8 pad, u16 textures, u16 entries,
    u32 offset of the entry table; the texture name offsets follow at +0x14. An entry: name[28], u8 tags, u8 type,
    u16 pad, u32 tag offsets (from the entry). A tag (FLPA pane, FLVI visibility, FLTP texture pattern ...): magic,
    u8 groups, pad, u32 group offsets (from the tag). A group: u8 index, u8 target, u8 curve (1 step: f32 frame,
    u16 value; 2 hermite: f32 frame, value, slope), pad, u16 keys, pad, u32 key offset (from the group). Every key
    value is printed with the file offset of its value (for --setkey lines)."""
    _, secs = sections(b, b'FLAN')
    tag_name = ''
    for kind, off, size in secs:
        if kind == b'pat1':
            tag_name = cstr(b, off + 0x1C, 64)
        if kind != b'pai1':
            continue
        frames, loop = struct.unpack_from('<HB', b, off + 8)
        ntex, nent, eoff = struct.unpack_from('<HHI', b, off + 0x0C)
        tex = [cstr(b, off + 0x14 + struct.unpack_from('<I', b, off + 0x14 + 4 * i)[0], 64) for i in range(ntex)]
        out.append(f'    tag "{tag_name}", {frames} frames, textures {tex}')
        for e in range(nent):
            ent = off + struct.unpack_from('<I', b, off + eoff + 4 * e)[0]
            name = cstr(b, ent, 28)
            ntags, typ = b[ent + 28], b[ent + 29]
            for t in range(ntags):
                tg = ent + struct.unpack_from('<I', b, ent + 32 + 4 * t)[0]
                magic = b[tg:tg + 4].decode('ascii', 'replace')
                for g in range(b[tg + 4]):
                    gr = tg + struct.unpack_from('<I', b, tg + 8 + 4 * g)[0]
                    idx, target, curve = b[gr], b[gr + 1], b[gr + 2]
                    nkeys = struct.unpack_from('<H', b, gr + 4)[0]
                    kat = gr + struct.unpack_from('<I', b, gr + 8)[0]
                    keys = []
                    for k in range(nkeys):
                        if curve == 2:
                            fr, val, _ = struct.unpack_from('<fff', b, kat + 12 * k)
                            keys.append(f'{fr:g}:{val:g}@{base + kat + 12 * k + 4:x}')
                        else:
                            fr, val = struct.unpack_from('<fH', b, kat + 8 * k)
                            keys.append(f'{fr:g}:{val}@{base + kat + 8 * k + 4:x}')
                    out.append(f'      {name} {magic} idx {idx} target {target} curve {curve}: ' + ' '.join(keys))


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
    ap.add_argument('--hide', metavar='PANE', help='write the line that hides PANE of the one file given to '
                    'game/ui_patch.txt (TOTK_UI_PATCH=1)')
    ap.add_argument('--set', metavar='PANE:field=value,...', action='append', default=[],
                    help='write lines that set x, y, sx, sy, w or h of PANE in the one file given (game/ui_patch.txt)')
    ap.add_argument('--part', metavar='PANE=LAYOUT', help='write lines that make the part pane PANE use LAYOUT')
    ap.add_argument('--anim', metavar='TEXT', help='print the keys of every animation whose name contains TEXT')
    ap.add_argument('--texture', metavar='LAYOUT:OLD=NEW', help='write lines that rename a texture of LAYOUT')
    ap.add_argument('--setkey', metavar='OFFSET=VALUE', action='append', default=[],
                    help='write a line that sets the f32 animation key value at OFFSET (as --anim prints it)')
    ap.add_argument('--grid-row', metavar='BUTTON=ROW', action='append', default=[],
                    help='write a line that puts BUTTON in another row of a UIKit grid (the small .truis file)')
    a = ap.parse_args()
    if a.grid_row:
        if len(a.files) != 1:
            sys.exit('error: --grid-row takes exactly one dumped file')
        return set_grid_rows(a.files[0], a.grid_row, ROOT / 'game' / 'ui_patch.txt')
    if a.texture:
        if len(a.files) != 1:
            sys.exit('error: --texture takes exactly one dumped file')
        return rename_texture(a.files[0], a.texture, ROOT / 'game' / 'ui_patch.txt')
    if a.setkey:
        if len(a.files) != 1:
            sys.exit('error: --setkey takes exactly one dumped file')
        return set_keys(a.files[0], a.setkey, ROOT / 'game' / 'ui_patch.txt')
    if a.anim:
        if len(a.files) != 1:
            sys.exit('error: --anim takes exactly one dumped file')
        out = []

        def walk(b, base, where):
            for name, data, off in sarc_files(b, True):
                if data[:4] == b'SARC':
                    walk(data, base + off, f'{where}/{name}')
                elif data[:4] == b'FLAN' and a.anim in name:
                    out.append(f'  animation {where}/{name}:')
                    flan_detail(data, out, base + off)
        walk(a.files[0].read_bytes(), 0, '')
        print('\n'.join(out))
        return
    if a.part:
        if len(a.files) != 1:
            sys.exit('error: --part takes exactly one dumped file')
        return swap_part(a.files[0], a.part, ROOT / 'game' / 'ui_patch.txt')
    if a.set:
        if len(a.files) != 1:
            sys.exit('error: --set takes exactly one dumped file')
        for spec in a.set:
            set_fields(a.files[0], spec, ROOT / 'game' / 'ui_patch.txt')
        return
    if a.hide:
        if len(a.files) != 1:
            sys.exit('error: --hide takes exactly one dumped file')
        return hide(a.files[0], a.hide, ROOT / 'game' / 'ui_patch.txt')
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
