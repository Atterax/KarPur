"""List a RomFS mod's files for replacing whole files at run time (game/mod_files.txt, TOTK_MOD_FILES=1).

Some mods change a file's size (Improved Dynamic LODs: common_resident_{0,1}.trscn grow by 616 bytes), so changing fields
in place (tools/mod_report.py --apply) cannot apply them. For each Oodle-packed file of the mod this tool writes two lines
for runtime/data_patch.c:

  romfs <offset> u32 mod=<size> modsize:<name>      the unpacked size in the pack's table of contents (field 3 of the
                                                    file's entry), so the game reserves room for the mod's file
  modfile <packed> <unpacked> <fnv1a64> <path>      when the game unpacks the file with these packed bytes, the runtime
                                                    gives it the file at <path> instead

The mod stays where it is, nothing of it is copied. Files stored unpacked are not supported and are reported. Prints
paths and sizes only. Only the game's files are read, on the player's PC.

Several mods: a file two of them change comes from the later one (the launcher's Mods tab passes the enabled mods in
its order). --out writes the list elsewhere; the last line of the output is "SUMMARY {json}" with, per mod, the files
it replaces, the files it adds (not in the game: not supported yet), the game's unpacked files it changes (not
supported) and whether it carries code (exefs: never usable in a recompiled game). The launcher reads that line.

  .venv\\Scripts\\python tools\\mod_files.py "C:\\KarPur\\mods\\Improved Dynamic LODs"
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import mod_report  # noqa: E402
import trpfs  # noqa: E402

HEAD = 4096   # packed bytes the fingerprint covers (runtime/data_watch.c)


def game_path(game, path, fpath):
    """The file's path in the game's archive: a mod without a romfs folder gives the longest known tail of its path."""
    if trpfs.fnv1a64(path) in game.where:
        return path
    parts = list(fpath.resolve().parts[1:])
    for k in range(len(parts)):
        tail = '/'.join(parts[k:])
        for candidate in (tail, tail.lower()):
            if trpfs.fnv1a64(candidate) in game.where:
                return candidate
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('mod', type=Path, nargs='+', help='mod folders')
    ap.add_argument('--romfs', type=Path, default=trpfs.ROMFS)
    ap.add_argument('--out', type=Path, default=ROOT / 'game' / 'mod_files.txt')
    a = ap.parse_args()
    game = mod_report.Game(a.romfs)
    lines = ['# made by tools/mod_files.py; runtime/data_patch.c, TOTK_MOD_FILES=1']
    skipped = 0
    by_file = {}   # the game's path -> its two lines, the later mod's
    summary = []
    for folder in a.mod:
        mod = {'mod': str(folder), 'replaces': 0, 'adds': 0, 'unpacked': 0,
               'code': (folder / 'exefs').is_dir() and any((folder / 'exefs').iterdir())}
        summary.append(mod)
        for path, fpath in mod_report.mod_files(folder):
            found = game_path(game, path, fpath)
            pi = game.file_pack[game.where[trpfs.fnv1a64(found)]] if found else None
            entry = (game.toc(pi) or {}).get(trpfs.fnv1a64(found)) if found else None
            if entry is None:
                print(f'{path}: not in this game: left out')
                skipped += 1
                mod['adds'] += 1
                continue
            if trpfs.codec(entry) == 'none':
                print(f'{found}: stored unpacked in the game: not supported, left out')
                skipped += 1
                mod['unpacked'] += 1
                continue
            start, packed = entry._vec(len(entry.slots) - 1)
            base = game.fs_off + game.offset_of[trpfs.fnv1a64(game.pack_names[pi])]
            size = fpath.stat().st_size
            unpacked = entry.scalar(3, 'Q')
            print_ = trpfs.fnv1a64_bytes(entry.buf[start:start + min(packed, HEAD)])
            name = Path(found).name
            by_file[found] = [f'romfs {base + entry.pos + entry.slots[3]:x} u32 mod={size} modsize:{name}',
                              f'modfile {packed} {unpacked} {print_:016x} {fpath.resolve()}']
            mod['replaces'] += 1
            print(f'{found}: game {unpacked} B, mod {size} B')
    for pair in by_file.values():
        lines += pair
    out = a.out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{(len(lines) - 1) // 2} files listed in {out}' + (f', {skipped} left out' if skipped else ''))
    print('SUMMARY ' + json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
