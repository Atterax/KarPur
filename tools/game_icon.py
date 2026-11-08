"""The game's own icon from the player's base game XCI, as build/game/icon.ico for karpur.exe (tools/build_game.py).

The Control NCA of the base game holds the title's icons (icon_<language>.dat, 256x256 JPEG). hactool (prebuilt,
toolchain/hactool) unpacks that NCA's RomFS into a temporary folder; Windows' own image classes (System.Drawing through
Windows PowerShell) scale the JPEG to 256, 64, 48, 32 and 16 pixels as PNG, and this script packs them into an .ico.
The icon is the player's own game data: it stays in build/, which git ignores, and is never part of the repository.

  python tools/game_icon.py [--config config.local.toml] [--out build/game/icon.ico]
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from extract_romfs import HACTOOL, copy_range, private_file, write_hactool_keys  # noqa: E402
from switchfs import load_keys, open_container  # noqa: E402

SIZES = (256, 64, 48, 32, 16)
LANGUAGES = ('German', 'AmericanEnglish', 'BritishEnglish', 'Japanese')
SCALE = r'''
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($args[0])
foreach ($n in $args[2..($args.Count - 1)]) {
  $bmp = New-Object System.Drawing.Bitmap ([int]$n), ([int]$n)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.DrawImage($src, 0, 0, [int]$n, [int]$n)
  $bmp.Save((Join-Path $args[1] "$n.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
$src.Dispose()
'''


def pack_ico(pngs, target):
    """An .ico whose images are PNG (Windows Vista and later read those)."""
    head = struct.pack('<HHH', 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    entries, blobs = b'', b''
    for size, data in pngs:
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
        blobs += data
    target.write_bytes(head + entries + blobs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'game' / 'icon.ico')
    a = ap.parse_args()
    if os.name != 'nt':
        sys.exit('game_icon: Windows only (the icon is a Windows resource)')
    config = tomllib.loads(a.config.read_text(encoding='utf-8'))
    base, keys_path = Path(config['game']['base']), Path(config['keys']['prod'])
    keys = load_keys(keys_path)
    _, tickets, ncas = open_container(base, keys)
    controls = [n for n in ncas if n.content_type == 2]
    if not controls:
        sys.exit(f'game_icon: {base.name} has no Control NCA')
    control = min(controls, key=lambda n: n.title_id)
    tmp = ROOT / 'game' / 'tmp-icon'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    try:
        with private_file(tmp / 'title.keys') as f:
            for rights, key in tickets.items():
                f.write(f'{rights.hex()} = {key.hex()}\n')
        write_hactool_keys(keys_path, tmp / 'hactool.keys')
        copy_range(base, control.offset, control.size, tmp / 'control.nca', 'control')
        r = subprocess.run([str(HACTOOL), '-x', '--disablekeywarns', '--suppresskeys', '-k', str(tmp / 'hactool.keys'),
                            f'--romfsdir={tmp / "romfs"}', str(tmp / 'control.nca')],
                           env=dict(os.environ, MNG_TITLE_KEYS=str(tmp / 'title.keys')), capture_output=True)
        romfs, icon = tmp / 'romfs', None
        if romfs.is_dir():   # the player's language first, else any icon the title has
            icon = next((romfs / f'icon_{lang}.dat' for lang in LANGUAGES if (romfs / f'icon_{lang}.dat').is_file()),
                        None) or next(iter(sorted(romfs.glob('icon_*.dat'))), None)
        if not icon:
            sys.exit(f'game_icon: no icon in the Control NCA (hactool exit code {r.returncode})')
        jpg = tmp / 'icon.jpg'
        shutil.copy2(icon, jpg)
        script = tmp / 'scale.ps1'   # -File: the arguments reach the script as $args
        script.write_text(SCALE, encoding='utf-8')
        ps = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
                             str(script), str(jpg), str(tmp), *map(str, SIZES)], capture_output=True)
        missing = [n for n in SIZES if not (tmp / f'{n}.png').is_file()]
        if ps.returncode or missing:
            sys.exit(f'game_icon: scaling the icon failed (PowerShell exit code {ps.returncode})')
        a.out.parent.mkdir(parents=True, exist_ok=True)
        pack_ico([(n, (tmp / f'{n}.png').read_bytes()) for n in SIZES], a.out)
        print(f'game_icon: {a.out} ({len(SIZES)} sizes, from {icon.name})')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)   # also removes the key files


if __name__ == '__main__':
    main()
