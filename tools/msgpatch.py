"""Replace one line of a game message file in place, as game/ui_patch.txt lines for runtime/data_patch.c.

Purpose: the "PC settings" entry of the main menu (docs/progress.md 2026-11-04) takes the place of the
downloadable-content button, whose text the menu fetches as ("xmenu", "xmenu_dlc_button"). The text files come in
pairs per language, message/dat/<language>/common/<name>.tbl (AHTB: the labels, in line order) and the .dat with the
texts, both Oodle-packed in arc/data.trpfs; runtime/data_watch.c dumps them to game/dump (tools/trpfs.py --watch,
TOTK_DATA_WATCH=1).

Message file (.dat), Game Freak's format as the community tools describe it, little endian:
  header   u16 languages (1), u16 lines, u32 section length, u32 initial key (0), u32 section offset (16)
  section  u32 length; per line u32 offset (from the section start), u16 length in UTF-16 units with the terminator,
           u16 flags; then the texts
  text     UTF-16, each unit XOR a key that starts at 0x7C89 + 0x2983 * line and turns left by 3 bits after each unit

The new text must not be longer than the old one: it is written over the start of the old line, with its terminator,
and the line's length field is lowered, so the file keeps its size and nothing moves. The lines go to game/ui_patch.txt
(TOTK_UI_PATCH=1) keyed by the packed and unpacked size and fingerprint from game/data_watch.txt. The game's own texts
are only decoded to check the line (that it decodes cleanly and ends where its length says); they are never printed.

  .venv\\Scripts\\python tools\\msgpatch.py --tbl game\\dump\\messagedatGermancommonxmenu.tbl__bd12a0c67156b59f ^
      --dat game\\dump\\messagedatGermancommonxmenu.tbl__45ed36c6be5c8b1c --label xmenu_dlc_button --text PC-Einstellungen

--relocate writes a "msgtext" line instead: the runtime puts the text, of any length, in its own memory and points the
line there (runtime/data_patch.c). It needs only the line's index from the .tbl (the same in every language) and the
.dat's key in game/data_watch.txt, so --dat may name a file that was never dumped (its name as in data_watch.txt).
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

from uilayout import ahtb_report  # noqa: E402

KEY_BASE, KEY_ADVANCE = 0x7C89, 0x2983


def labels_of(tbl):
    out = []
    ahtb_report(tbl, out)
    text = out[0].split(' labels: ', 1)[1] if out and ' labels: ' in out[0] else ''
    return [s for s in text.split(', ') if s]


def line_table(dat):
    langs, lines, sec_len, key0, sec = struct.unpack_from('<HHIII', dat, 0)
    if langs != 1 or key0 != 0 or sec + 4 > len(dat):
        raise ValueError(f'unexpected header: {langs} languages, key {key0}, section at {sec}')
    entries = [struct.unpack_from('<IHH', dat, sec + 4 + 8 * i) for i in range(lines)]
    return sec, entries


def crypt(units, line):
    """XOR the UTF-16 units with the line's key stream (the same both ways)."""
    key = (KEY_BASE + KEY_ADVANCE * line) & 0xFFFF
    out = []
    for u in units:
        out.append(u ^ key)
        key = ((key << 3) | (key >> 13)) & 0xFFFF
    return out


def watch_key(name):
    watch = ROOT / 'game' / 'data_watch.txt'
    for line in watch.read_text().splitlines() if watch.is_file() else []:
        t = line.split()
        if len(t) == 4 and not line.startswith('#') and t[3] == name:
            return t[:3]
    sys.exit(f'error: {watch} has no line for {name} (run tools\\trpfs.py --watch for its pack)')


def relocate(a, idx):
    if '\r' in a.text or '\n' in a.text:
        sys.exit('error: write a line break as \\n')
    key = watch_key(a.dat.name)
    tag = f'msgtext:{a.label}:{a.dat.name.split("__")[0]}'
    line = f'msgtext {key[0]} {key[1]} {key[2]} {idx} {tag} {a.text}'
    old = a.out.read_text(encoding='utf-8').splitlines() if a.out.is_file() else []
    old = [l for l in old if l.split(' ', 6)[5:6] != [tag]]
    a.out.write_text('\n'.join(old + [line]) + '\n', encoding='utf-8')
    print(f'{a.label}: line {idx}, {len(a.text)} characters, as a msgtext line in {a.out}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tbl', type=Path, required=True, help='the dumped label table (.tbl, AHTB)')
    ap.add_argument('--dat', type=Path, required=True, help='the dumped message file of the same name')
    ap.add_argument('--label', required=True)
    ap.add_argument('--text', required=True)
    ap.add_argument('--out', type=Path, default=ROOT / 'game' / 'ui_patch.txt')
    ap.add_argument('--relocate', action='store_true', help='a msgtext line: any length, in the runtime\'s memory')
    a = ap.parse_args()
    labels = labels_of(a.tbl.read_bytes())
    if a.label not in labels:
        sys.exit(f'error: no label {a.label} in {a.tbl.name} ({len(labels)} labels)')
    idx = labels.index(a.label)
    if a.relocate:
        return relocate(a, idx)
    dat = a.dat.read_bytes()
    sec, entries = line_table(dat)
    if idx >= len(entries):
        sys.exit(f'error: label {a.label} is line {idx}, but {a.dat.name} has {len(entries)} lines')
    off, length, flags = entries[idx]
    at = sec + off
    old = crypt(struct.unpack_from(f'<{length}H', dat, at), idx)
    clean = old[-1] == 0 and all(u >= 0x20 or u == 0x0A for u in old[:-1])
    print(f'{a.label}: line {idx} of {len(entries)}, {length} units at +{at:#x}, flags {flags:#x}, '
          f'decodes cleanly: {"yes" if clean else "NO"}')
    if not clean:
        sys.exit('error: the line does not decode as plain text (another format or a text with variables): nothing written')
    units = list(a.text.encode('utf-16-le'))
    new = [units[i] | units[i + 1] << 8 for i in range(0, len(units), 2)] + [0]
    if len(new) > length:
        sys.exit(f'error: "{a.text}" needs {len(new)} units with its terminator, the line has {length}')
    key = None
    watch = ROOT / 'game' / 'data_watch.txt'
    for line in watch.read_text().splitlines() if watch.is_file() else []:
        t = line.split()
        if len(t) == 4 and not line.startswith('#') and t[3] == a.dat.name:
            key = t[:3]
    if not key:
        sys.exit(f'error: {watch} has no line for {a.dat.name} (run tools\\trpfs.py --watch for its pack)')
    tag = f'msg:{a.label}:{a.dat.name.split("__")[0]}'
    lines = [f'oodle {key[0]} {key[1]} {key[2]} {sec + 4 + 8 * idx + 4:x} u16 mod={len(new)} {tag}']
    for j, u in enumerate(crypt(new, idx)):
        lines.append(f'oodle {key[0]} {key[1]} {key[2]} {at + 2 * j:x} u16 mod={u} {tag}')
    old_lines = a.out.read_text(encoding='utf-8').splitlines() if a.out.is_file() else [
        '# UI bytes set by runtime/data_patch.c (TOTK_UI_PATCH=1)']
    old_lines = [l for l in old_lines if not l.endswith(' ' + tag)]
    a.out.write_text('\n'.join(old_lines + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} lines ({len(new)} units and the length) for {a.label} in {a.out}')


if __name__ == '__main__':
    main()
