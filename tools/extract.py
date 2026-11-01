"""Milestone M1: extract the code of the game named in config.local.toml and print a report about it.

    .venv\\Scripts\\python tools\\extract.py

What it does:

1. Opens the base game, the update and the DLC files (XCI or NSP) with the console's prod.keys.
2. Extracts the ExeFS (the executables: main, sdk, subsdk*, rtld) of the base game and of the update into
   game/extracted/{base,update}/exefs, checking every hash.
3. Parses the executables and prints a report: game and versions, module sizes and build IDs, the SDK version,
   middleware, and which SDK functions `main` imports.

The report holds only metadata (IDs, sizes, hashes, counts, names) and is safe to paste. It is also saved in
game/report/m1-report.txt; the full import list of main goes to game/report/main-imports.txt.
"""

import argparse
import hashlib
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import nso as nsolib
    from switchfs import GameFileError, load_keys, open_container, read_exefs
except ModuleNotFoundError as e:   # cryptography or lz4 missing
    sys.exit(f'error: Python package missing ({e.name}). Run: .venv\\Scripts\\python -m pip install -r tools\\requirements.txt')

GAMES = {0x0100A3D008C5C000: 'Pokemon Scarlet', 0x01008F6008C5E000: 'Pokemon Violet'}
MODULE_ORDER = ('rtld', 'main', 'sdk')


class Report:
    def __init__(self):
        self.lines = []

    def __call__(self, text=''):
        print(text, flush=True)
        self.lines.append(text)


def tid(value):
    return f'{value:016X}'


def short_hash(data):
    return hashlib.sha256(data).hexdigest()[:16]


def base_id(title_id):
    """The application ID an update (…800) or DLC (…1000 + n) belongs to."""
    return title_id & ~0xFFF if title_id & 0x1000 == 0 else (title_id - 0x1000) & ~0xFFF


def load_config(path):
    if not path.is_file():
        raise GameFileError(f'{path.name} not found: copy config.example.toml to config.local.toml and fill in your paths')
    try:
        config = tomllib.loads(path.read_text(encoding='utf-8'))
    except tomllib.TOMLDecodeError as e:
        raise GameFileError(f'{path.name} has a syntax error: {e}. Check the quotes and commas.') from None
    game, keys = config.get('game', {}), config.get('keys', {})
    for name in ('base', 'update'):
        if not game.get(name):
            raise GameFileError(f'{path.name}: [game] {name} is missing')
    if not keys.get('prod'):
        raise GameFileError(f'{path.name}: [keys] prod is missing')
    return Path(game['base']), Path(game['update']), [Path(p) for p in game.get('dlc', [])], Path(keys['prod'])


def inventory(report, label, path, kind, ncas):
    report(f'{label}: {path.name} ({kind.upper()}, {path.stat().st_size / 2**30:.2f} GiB)')
    for n in sorted(ncas, key=lambda n: (n.title_id, n.content_type)):
        bktr = ', BKTR patch' if 4 in n.section_types() else ''
        report(f'  {n.type_name:<10} {tid(n.title_id)}  {n.size / 2**20:10.1f} MiB  keygen {n.key_generation}{bktr}')


def extract_program(report, label, path, keys, out_dir, *, update):
    kind, tickets, ncas = open_container(path, keys)
    inventory(report, label, path, kind, ncas)
    programs = [n for n in ncas if n.content_type == 0 and (4 in n.section_types()) == update]
    if len(programs) != 1:
        raise GameFileError(f'{path.name}: expected one {"update" if update else "base"} Program NCA, found {len(programs)}'
                            f' (is this really the {"update" if update else "base game"} file?)')
    program = programs[0]
    try:
        with path.open('rb') as f:
            files = read_exefs(f, program, keys, tickets)
    except GameFileError as e:
        raise GameFileError(f'{path.name}: {e}') from None
    exefs = out_dir / 'exefs'
    exefs.mkdir(parents=True, exist_ok=True)
    for old in exefs.iterdir():
        old.unlink()
    for name, data in files.items():
        (exefs / name).write_bytes(data)
    report(f'  ExeFS: {", ".join(sorted(files))} (all hashes OK)')
    return program, files


def module_report(report, files):
    modules = {}
    names = [n for n in MODULE_ORDER if n in files] + sorted(n for n in files if n.startswith('subsdk'))
    report(f'  {"module":<9} {"text":>9} {"rodata":>9} {"data":>9} {"bss":>9}  imports exports  build ID')
    for name in names:
        m = nsolib.parse(name, files[name])
        modules[name] = m
        sizes = [m.segments[s][1] / 2**20 for s in nsolib.SEGMENTS] + [m.bss_size / 2**20]
        report(f'  {name:<9} ' + ' '.join(f'{s:8.2f}M' for s in sizes)
               + f'  {len(m.imports):7} {len(m.exports):7}  {m.build_id}')
    return modules


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    parser.add_argument('--out', type=Path, default=ROOT / 'game', help='where extracted files and reports go')
    args = parser.parse_args()

    base_path, update_path, dlc_paths, keys_path = load_config(args.config)
    keys = load_keys(keys_path)
    report = Report()
    report('=' * 26 + ' KarPur M1 report ' + '=' * 26)

    base_nca, base_files = extract_program(report, 'Base game', base_path, keys, args.out / 'extracted' / 'base', update=False)
    app = base_nca.title_id
    report(f'  Game: {GAMES.get(app, "UNKNOWN GAME")} ({tid(app)})')
    report(f'  main sha256: {short_hash(base_files["main"])}')
    report()

    update_nca, update_files = extract_program(report, 'Update', update_path, keys, args.out / 'extracted' / 'update', update=True)
    update_ids = {n.title_id for n in open_container(update_path, keys)[2]}
    if not update_ids <= {app, app | 0x800}:
        raise GameFileError(f'{update_path.name} belongs to another game ({", ".join(map(tid, sorted(update_ids)))})')
    report(f'  main sha256: {short_hash(update_files["main"])}')
    report()

    for i, path in enumerate(dlc_paths, 1):
        kind, _, ncas = open_container(path, keys)
        inventory(report, f'DLC {i}', path, kind, ncas)
        foreign = sorted({n.title_id for n in ncas if base_id(n.title_id) != app})
        if foreign:
            report(f'  WARNING: titles of another game: {", ".join(map(tid, foreign))}')
    if dlc_paths:
        report()

    report('Modules of the update (the code we will recompile):')
    modules = module_report(report, update_files)
    report()

    sdk = sorted({v for m in modules.values() for v in m.strings(nsolib.SDK_VERSION)})
    report(f'SDK version: {", ".join(sdk) or "not found"}')
    middleware = sorted({v for m in modules.values() for v in m.strings(nsolib.MIDDLEWARE)})
    report(f'Middleware ({len(middleware)}):')
    for mw in middleware:
        report(f'  {mw}')
    report()

    main_mod = modules['main']
    provided = {s.name for name, m in modules.items() if name != 'main' for s in m.exports}
    imports = main_mod.imports
    unresolved = [s for s in imports if s.name not in provided and s.binding != 2]
    report(f'main imports {len(imports)} symbols; {len(unresolved)} not provided by the other modules')
    report('  by namespace:')
    for ns, count in nsolib.namespace_counts(imports).most_common(30):
        report(f'    {count:6}  {ns}')
    c_names = sorted(s.name for s in imports if not s.name.startswith('_Z'))
    nvn = [n for n in c_names if n.lower().startswith('nvn')]
    report(f'  C functions: {len(c_names)} (NVN: {", ".join(nvn) or "none"})')
    report('=' * 70)

    report_dir = args.out / 'report'
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / 'm1-report.txt').write_text('\n'.join(report.lines) + '\n', encoding='utf-8')
    (report_dir / 'main-imports.txt').write_text(
        ''.join(f'{"provided " if s.name in provided else "MISSING  "}{s.name}\n' for s in sorted(imports, key=lambda s: s.name)),
        encoding='utf-8')
    print(f'\nSaved: {report_dir / "m1-report.txt"}')


if __name__ == '__main__':
    try:
        main()
    except (GameFileError, nsolib.NsoError) as error:
        sys.exit(f'error: {error}')
    except FileNotFoundError as error:
        sys.exit(f'error: file not found: {error.filename}')
