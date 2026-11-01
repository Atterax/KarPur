"""Extract the Switch system shared fonts (nn::pl) into game/firmware/shared-fonts.

The game asks nn::pl for all six shared fonts at start (Standard, Chinese Simplified and its extension, Chinese
Traditional, Korean, Nintendo extension). They are not in the game's files but in system data NCAs
0100000000000810..814. A game card carries the system update it needs, so the base game XCI named in
config.local.toml holds them in its update partition; an unpacked firmware NCA directory works too (--firmware).

The work is done by MacNativeGames' extract_shared_fonts.py (tools/vendor/macnativegames, unmodified): it verifies
every NCA FS header, all six IVFC levels and each BFTTF header. The keys file is read by that script; key values are
never printed. The report lists font types and sizes only. runtime/ov_system.c reads the fonts from
<romfs>/../firmware/shared-fonts, i.e. game/firmware/shared-fonts.

  .venv\\Scripts\\python tools\\extract_fonts.py [--firmware <NCA directory>]
"""

import argparse
import json
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'tools' / 'vendor' / 'macnativegames'
OUT = ROOT / 'game' / 'firmware' / 'shared-fonts'
# The vendored scripts import each other by bare module name.
sys.path.insert(0, str(VENDOR))
import extract_shared_fonts  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    ap.add_argument('--firmware', type=Path, help='unpacked firmware NCA directory instead of the base game XCI')
    ap.add_argument('--out', type=Path, default=OUT)
    a = ap.parse_args()
    if not a.config.is_file():
        sys.exit(f'error: {a.config.name} not found: copy config.example.toml to config.local.toml and fill in your paths')
    config = tomllib.loads(a.config.read_text(encoding='utf-8'))
    keys = config.get('keys', {}).get('prod')
    base = config.get('game', {}).get('base')
    if not keys:
        sys.exit(f'error: {a.config.name}: [keys] prod is missing')
    if (a.out / 'manifest.json').is_file():
        print(f'{a.out} already holds verified shared fonts')
        return
    if a.out.exists():
        sys.exit(f'error: {a.out} exists without manifest.json (an interrupted run?): delete the folder and run again')
    if a.firmware:
        source = ['--firmware', str(a.firmware)]
    elif base and Path(base).suffix.lower() == '.xci':
        source = ['--xci', base]
    else:
        sys.exit('error: the base game in config.local.toml is not an XCI; give --firmware <NCA directory>')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    sys.argv = ['extract_shared_fonts.py', '--keys', keys, *source, '--output', str(a.out)]
    print('=' * 72)
    try:
        extract_shared_fonts.main()
    except ValueError as e:
        sys.exit(f'error: {e}')
    manifest = json.loads((a.out / 'manifest.json').read_text(encoding='utf-8'))
    for f in manifest['fonts']:
        print(f'font {f["type"]} {f["name"]}: {f["font_bytes"]} bytes, title {f["title_id"]}')
    print(f'written to {a.out}')
    print('=' * 72)


if __name__ == '__main__':
    main()
