"""Compare the game's SDK symbols with the ones the TotK Recomp runtime already handles.

    .venv\\Scripts\\python tools\\compare_totk.py

Reads the ExeFS that tools/extract.py wrote to game/extracted/update/exefs. TotK Recomp replaces SDK functions by
symbol name (tools/data/totk_sdk_symbols.txt), so a name that exists in our game too can probably reuse that code;
a missing one was renamed in another SDK version or is not used here. Prints only symbol names and counts.
"""

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import nso as nsolib  # noqa: E402

DATA = ROOT / 'tools' / 'data' / 'totk_sdk_symbols.txt'


def load_totk():
    out = {}
    for line in DATA.read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            name, where = line.split()
            out[name] = where
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exefs', type=Path, default=ROOT / 'game' / 'extracted' / 'update' / 'exefs')
    args = parser.parse_args()
    if not (args.exefs / 'main').is_file():
        sys.exit(f'error: {args.exefs} has no main: run tools\\extract.py first')

    modules = {p.name: nsolib.parse(p.name, p.read_bytes()) for p in sorted(args.exefs.iterdir())
               if p.name in ('main', 'sdk', 'rtld') or p.name.startswith('subsdk')}
    ours = {s.name for m in modules.values() for s in m.symbols if s.name}
    imports = modules['main'].imports
    totk = load_totk()

    print('=' * 24 + ' KarPur TotK comparison ' + '=' * 24)
    by_file = defaultdict(lambda: [0, 0])
    missing = defaultdict(list)
    for name, where in totk.items():
        by_file[where][1] += 1
        if name in ours or (name.endswith('_') and any(o.startswith(name) for o in ours)):
            by_file[where][0] += 1
        else:
            missing[where].append(name)
    found = sum(v[0] for v in by_file.values())
    print(f'TotK runtime symbols present in our game: {found} of {len(totk)}')
    for where, (hit, total) in sorted(by_file.items(), key=lambda kv: -kv[1][1]):
        print(f'  {hit:4} / {total:<4} {where}')
    print()

    totk_ns = Counter(nsolib.namespace(n) for n in totk)
    print('Namespaces main imports that TotK never touches (new host work, or lifted as is):')
    for ns, count in nsolib.namespace_counts(imports).most_common():
        if ns.startswith('nn::') and ns not in totk_ns:
            print(f'  {count:5}  {ns}')
    print()
    print('TotK symbols missing in our game (renamed in SDK 13 or unused):')
    for where in sorted(missing):
        for name in sorted(missing[where]):
            print(f'  {where:<15} {name}')
    print('=' * 72)


if __name__ == '__main__':
    try:
        main()
    except nsolib.NsoError as error:
        sys.exit(f'error: {error}')
