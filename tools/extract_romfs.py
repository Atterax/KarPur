"""Milestone M3, step 1: build the game's RomFS (its data files) from the base game and the update.

    .venv\\Scripts\\python tools\\extract_romfs.py

The update stores its RomFS as a BKTR patch on top of the base game's. hactool (built by tools/build_hactool.py)
merges the two: this script copies both Program NCAs out of their containers into game/tmp, hands hactool the
update's title key and a cleaned copy of prod.keys through private files, and writes the merged image to
game/romfs/romfs.bin. hactool verifies all six IVFC hash levels on the way. The script then indexes the image
(game/romfs/index.json) and prints a report of sizes and counts only.

Needs about 2.5 times the size of both Program NCAs free while it runs (about 30 GB for Scarlet/Violet), and keeps
the merged image (about the size of the base Program NCA) afterwards.

The merge and the RomFS index follow MacNativeGames `games/totk/scripts/extract_update_romfs.py` and
`extract_romfs.py` (MIT, Copyright (c) 2026 MacNativeGames contributors), as vendored by TotK Recomp at upstream
b75be8cfa704df3701e86130c8ced268e79d6b11.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    from extract import load_config
    from switchfs import GameFileError, load_keys, open_container, require
except ModuleNotFoundError as e:
    sys.exit(f'error: Python package missing ({e.name}). Run: .venv\\Scripts\\python -m pip install -r tools\\requirements.txt')

HACTOOL = ROOT / 'toolchain' / 'hactool' / ('hactool.exe' if os.name == 'nt' else 'hactool')
# hactool checks two signatures with Nintendo public keys it does not ship: those two lines always read (FAIL) and say
# nothing about the data. The six IVFC levels decide.
UNSIGNED = re.compile(r'\s*(?:Fixed-Key Signature|NPDM Signature) \(FAIL\)')
HACTOOL_ERROR = re.compile(r'\(FAIL\)|\(INVALID\)|Failed to|Error:')
LONG_HEX = re.compile(r'[0-9A-Fa-f]{32,}')
# Lockpick_RCM writes these 17 bytes long (a trailing NUL); hactool exits unless they are exactly 16.
LOCKPICK_KEK_SOURCE = re.compile(r'(?:mariko_)?master_kek_source_[0-9a-f]{2}\Z')


def say(text=''):
    print(text, flush=True)


def private_file(path):
    """A new file only this user can read (as far as the OS allows), for key material."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    return os.fdopen(fd, 'w', encoding='utf-8')


def write_hactool_keys(source, target):
    """prod.keys reduced to the `name = value` lines hactool can parse (it knows no comments)."""
    lines = []
    for number, line in enumerate(Path(source).read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        body = line.split('#', 1)[0]
        if '=' not in body:
            continue
        name, value = (part.strip() for part in body.split('=', 1))
        if LOCKPICK_KEK_SOURCE.fullmatch(name.lower()):
            if len(value) == 34 and value[-2:] == '00':
                value = value[:32]
            require(len(value) == 32, f'prod.keys line {number}: {name} must be 16 bytes')
        lines.append(f'{name} = {value}')
    with private_file(target) as f:
        f.write('\n'.join(lines) + '\n')


def copy_range(source, offset, size, target, label):
    t, done = time.perf_counter(), 0
    with open(source, 'rb') as src, open(target, 'wb') as dst:
        src.seek(offset)
        while done < size:
            data = src.read(min(size - done, 16 << 20))
            require(bool(data), f'{Path(source).name} ends early (damaged or not copied completely)')
            dst.write(data)
            done += len(data)
            if done % (1 << 30) < (16 << 20):
                say(f'  {label}: {done / 2**30:.0f} of {size / 2**30:.1f} GiB, {time.perf_counter() - t:.0f}s')


def program_nca(path, keys, *, update):
    kind, tickets, ncas = open_container(path, keys)
    programs = [n for n in ncas if n.content_type == 0 and (4 in n.section_types()) == update]
    require(len(programs) == 1, f'{path.name}: expected one {"update" if update else "base"} Program NCA')
    return programs[0], tickets


def index_romfs(path):
    """[(path, offset, size)] of every file in a raw RomFS image (header of ten u64 at offset 0)."""
    length = path.stat().st_size
    with path.open('rb') as f:
        h = struct.unpack('<10Q', f.read(80))
        require(h[0] == 80, 'merged image is not a RomFS (bad header)')
        for off, size in ((h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])):
            require(off + size <= length, 'RomFS table out of bounds')
        f.seek(h[3])
        dirs = f.read(h[4])
        f.seek(h[7])
        entries = f.read(h[8])
    files, seen_dirs, seen_files = [], set(), set()
    todo = [(0, '')]
    while todo:
        off, parent = todo.pop()
        require(off not in seen_dirs and off + 24 <= len(dirs), 'invalid or cyclic RomFS directory')
        seen_dirs.add(off)
        _, sibling, child, file, _, n = struct.unpack_from('<6I', dirs, off)
        part = dirs[off + 24:off + 24 + n].decode('utf-8')
        prefix = parent + part + '/' if part else parent
        if sibling != 0xFFFFFFFF:
            todo.append((sibling, parent))
        if child != 0xFFFFFFFF:
            todo.append((child, prefix))
        while file != 0xFFFFFFFF:
            require(file not in seen_files and file + 32 <= len(entries), 'invalid or cyclic RomFS file')
            seen_files.add(file)
            _, next_file, data, size, _, n = struct.unpack_from('<IIQQII', entries, file)
            name = entries[file + 32:file + 32 + n].decode('utf-8')
            require(h[9] + data + size <= length, 'RomFS file out of bounds')
            files.append((prefix + name, h[9] + data, size))
            file = next_file
    return sorted(files)


def merge(base_path, update_path, keys_path, keys, out_dir):
    base_nca, _ = program_nca(base_path, keys, update=False)
    update_nca, tickets = program_nca(update_path, keys, update=True)
    sections = [i for i in range(4) if update_nca.header[0x400 + 512 * i + 4] == 4
                and any(struct.unpack_from('<II', update_nca.header, 0x240 + 16 * i))]
    require(len(sections) == 1, f'{update_path.name}: expected one BKTR section, found {len(sections)}')

    need = int((base_nca.size + update_nca.size) * 2.5)
    free = shutil.disk_usage(out_dir.parent).free
    require(free > need, f'need about {need / 2**30:.0f} GiB free on the drive of {out_dir.parent}, '
                         f'only {free / 2**30:.0f} GiB are free')

    tmp = out_dir.parent / 'tmp'
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    result = out_dir.parent / 'romfs-new'
    if result.exists():
        shutil.rmtree(result)
    result.mkdir()
    try:
        with private_file(tmp / 'title.keys') as f:
            for rights, key in tickets.items():
                f.write(f'{rights.hex()} = {key.hex()}\n')
        write_hactool_keys(keys_path, tmp / 'hactool.keys')
        say('Copying the Program NCAs out of their containers ...')
        copy_range(base_path, base_nca.offset, base_nca.size, tmp / 'base.nca', 'base game')
        copy_range(update_path, update_nca.offset, update_nca.size, tmp / 'update.nca', 'update')

        say('Merging with hactool (verifies every hash level) ...')
        t = time.perf_counter()
        command = [str(HACTOOL), '-x', '-y', '--disablekeywarns', '--suppresskeys', '-k', str(tmp / 'hactool.keys'),
                   f'--basenca={tmp / "base.nca"}', f'--section{sections[0]}={result / "romfs.bin"}',
                   str(tmp / 'update.nca')]
        run = subprocess.run(command, env=dict(os.environ, MNG_TITLE_KEYS=str(tmp / 'title.keys')),
                             capture_output=True, encoding='utf-8', errors='replace')
        log = LONG_HEX.sub('<hex removed>', run.stdout + run.stderr)
        (out_dir.parent / 'report').mkdir(exist_ok=True)
        (out_dir.parent / 'report' / 'hactool-merge.log').write_text(log, encoding='utf-8')
        checked = '\n'.join(line for line in log.splitlines() if not UNSIGNED.match(line))
        problems = [line.strip() for line in checked.splitlines() if HACTOOL_ERROR.search(line)]
        require(run.returncode == 0 and not problems,
                'hactool merge failed (exit code ' + str(run.returncode) + '):\n  ' + '\n  '.join(problems[:10] or
                ['no message; see game/report/hactool-merge.log']))
        levels = [i for i in range(6) if re.search(rf'Level {i} \(GOOD\)', log)]
        require(len(levels) == 6, f'hactool verified only IVFC levels {levels}; see game/report/hactool-merge.log')
        say(f'  done in {time.perf_counter() - t:.0f}s, all six IVFC levels GOOD')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)   # also removes the key files

    if out_dir.exists():
        shutil.rmtree(out_dir)
    result.rename(out_dir)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    ap.add_argument('--out', type=Path, default=ROOT / 'game' / 'romfs')
    ap.add_argument('--index-only', action='store_true', help='only index and report an existing romfs.bin')
    args = ap.parse_args()

    if not args.index_only:
        if not HACTOOL.is_file():
            raise GameFileError(f'{HACTOOL} missing: run .venv\\Scripts\\python tools\\build_hactool.py first')
        base, update, _, keys_path = load_config(args.config)
        keys = load_keys(keys_path)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        merge(base, update, keys_path, keys, args.out)

    image = args.out / 'romfs.bin'
    require(image.is_file(), f'{image} missing')
    say('Indexing and hashing the RomFS ...')
    files = index_romfs(image)
    with image.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    (args.out / 'index.json').write_text(json.dumps(
        {'romfs_size': image.stat().st_size, 'romfs_sha256': digest,
         'files': [{'path': p, 'offset': o, 'size': s} for p, o, s in files]}, indent=1) + '\n', encoding='utf-8')

    lines = ['=' * 26 + ' KarPur RomFS report ' + '=' * 25,
             f'RomFS image: {image.stat().st_size / 2**30:.2f} GiB, {len(files)} files, sha256 {digest[:16]}',
             'Top-level folders (files, GiB):']
    tops = Counter()
    top_size = Counter()
    for p, _, s in files:
        top = p.split('/', 1)[0] if '/' in p else '(root)'
        tops[top] += 1
        top_size[top] += s
    for top, n in top_size.most_common(15):
        lines.append(f'  {tops[top]:7}  {n / 2**30:7.2f}  {top}')
    exts, ext_size = Counter(), Counter()
    for p, _, s in files:
        ext = Path(p).suffix.lower() or '(none)'
        exts[ext] += 1
        ext_size[ext] += s
    lines.append('File types (files, GiB):')
    for ext, n in ext_size.most_common(20):
        lines.append(f'  {exts[ext]:7}  {n / 2**30:7.2f}  {ext}')
    lines.append('=' * 72)
    report = '\n'.join(lines)
    say('\n' + report)
    report_dir = args.out.parent / 'report'
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / 'romfs-report.txt').write_text(report + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except GameFileError as error:
        sys.exit(f'error: {error}')
    except FileNotFoundError as error:
        sys.exit(f'error: file not found: {error.filename}')
