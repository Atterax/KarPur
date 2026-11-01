"""Name the functions behind karpur+0x... addresses from the crash reports, using build/game/karpur.map.

The fault reporter (runtime/platform/win32.c) prints return addresses as offsets in karpur.exe. The linker map written by
tools/build_game.py lists every symbol with its address; this tool prints the symbol each offset falls into and the
distance from its start. Output: symbol names and offsets only.

  .venv\\Scripts\\python tools\\symbolize.py karpur+0xc7a4607 karpur+0xc8a6207
"""

import argparse
import re
import sys
from bisect import bisect_right
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP = ROOT / 'build' / 'game' / 'karpur.map'
BASE = 0x140000000   # PE32+ default image base (lld); used only when the map lists absolute addresses

# lld's own map format: "<address> [<load address>] <size> <align> <name>", indented by level (output section, input
# section "file:(section)", symbol); the MSVC-style format has "<section>:<offset> <symbol> <address> ...". Both are
# read: a line of numbers followed by one name is a symbol unless the name is a section or an input section.
MS_LINE = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s')
HEX = re.compile(r'^[0-9a-fA-F]+$')


def load(path):
    syms = []
    for line in path.read_text(errors='replace').splitlines():
        m = MS_LINE.match(line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
            continue
        t = line.split()
        if len(t) < 4 or not all(HEX.match(x) for x in t[:-1]) or len(t[0]) < 8:
            continue
        name = t[-1]
        if name.startswith('.') or ':(' in name or name.endswith(('.o', '.obj', ')')):
            continue
        syms.append((int(t[0], 16), name))
    syms.sort()
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addresses', nargs='+', help='karpur+0x... as the fault report prints them')
    ap.add_argument('--map', type=Path, default=MAP)
    ap.add_argument('--base', type=lambda s: int(s, 16), default=None, help='image base (hex); default: detected')
    a = ap.parse_args()
    if not a.map.is_file():
        sys.exit(f'error: {a.map} not found: run tools\\build_game.py first')
    syms = load(a.map)
    if not syms:
        sys.exit(f'error: no symbols read from {a.map}')
    addrs = [s[0] for s in syms]
    # lld's MinGW map lists image-relative addresses (all below the default image base): offsets are used as they are.
    if a.base is None:
        a.base = BASE if addrs[-1] >= BASE else 0
    print(f'{len(syms)} symbols in {a.map.name}, image base {a.base:#x}')
    for text in a.addresses:
        off = int(text.split('+', 1)[-1], 16)
        va = a.base + off
        i = bisect_right(addrs, va) - 1
        if i < 0:
            print(f'{text}: before the first symbol')
            continue
        print(f'{text}: {syms[i][1]}+{va - syms[i][0]:#x}')


if __name__ == '__main__':
    main()
