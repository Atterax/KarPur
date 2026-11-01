"""End-to-end test of tools/extract.py on a fake game (tests/make_fake_game.py). No game files or keys needed.

    python tests/test_extract.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(*args):
    return subprocess.run([sys.executable, *map(str, args)], capture_output=True, text=True, cwd=ROOT)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        assert run(ROOT / 'tests' / 'make_fake_game.py', tmp).returncode == 0
        r = run(ROOT / 'tools' / 'extract.py', '--config', tmp / 'config.toml', '--out', tmp / 'game')
        assert r.returncode == 0, r.stdout + r.stderr
        for expected in ('Game: Pokemon Violet', 'BKTR patch', 'SDK version: 17.5.3-Release', 'FakeVendor+FakeEngine-1.0',
                         'main imports 6 symbols; 1 not provided', 'NVN: nvnBootstrapLoader', 'Data       01008F6008C5F002'):
            assert expected in r.stdout, f'missing in report: {expected}\n{r.stdout}'
        keys = [line.split('=')[1].strip() for line in (tmp / 'prod.keys').read_text().splitlines()]
        report = r.stdout + (tmp / 'game' / 'report' / 'main-imports.txt').read_text()
        assert not any(k in report for k in keys), 'a key value leaked into the report'

        r = run(ROOT / 'tools' / 'compare_totk.py', '--exefs', tmp / 'game' / 'extracted' / 'update' / 'exefs')
        assert r.returncode == 0 and 'TotK runtime symbols present in our game: 1 of' in r.stdout, r.stdout + r.stderr

        # Swapped base and update must fail with a hint, not a traceback.
        swapped = (tmp / 'config.toml').read_text().replace('base.xci', 'X').replace('update.nsp', 'base.xci').replace('X', 'update.nsp')
        (tmp / 'swapped.toml').write_text(swapped)
        r = run(ROOT / 'tools' / 'extract.py', '--config', tmp / 'swapped.toml', '--out', tmp / 'game2')
        assert r.returncode != 0 and 'is this really the base game file?' in r.stderr, r.stderr
    print('test_extract: OK')


if __name__ == '__main__':
    main()
