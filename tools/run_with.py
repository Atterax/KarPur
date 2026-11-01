"""Start build/game/karpur.exe with runtime switches set for this one run (for A/B tests of rendering problems).

Each argument is NAME=VALUE; the TOTK_ prefix of the runtime's switches (runtime/cfg.c) is added when missing. The
switches apply to this run only: nothing is written to karpur.ini and the PowerShell session keeps no variables. The
log names the switches at the end, so a pasted log says which test it was.

  .venv\\Scripts\\python tools\\run_with.py MEMORY=import
  .venv\\Scripts\\python tools\\run_with.py BIND_SKIP=0 SHADER_MEMO=0
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / 'build' / 'game' / 'karpur.exe'


def main():
    env = dict(os.environ)
    names = []
    for arg in sys.argv[1:]:
        if '=' not in arg:
            sys.exit(f'error: {arg}: expected NAME=VALUE, e.g. MEMORY=import')
        name, value = arg.split('=', 1)
        name = name.strip().upper()
        if not name.startswith('TOTK_'):
            name = 'TOTK_' + name
        env[name] = value
        names.append(f'{name}={value}')
    if not EXE.is_file():
        sys.exit(f'error: {EXE} not found: run tools\\build_game.py first')
    print(f'run_with: starting karpur.exe with {", ".join(names) or "no switches"}', flush=True)
    code = subprocess.run([str(EXE)], cwd=ROOT, env=env).returncode
    print(f'run_with: karpur.exe ended with exit code {code} ({", ".join(names) or "no switches"})')


if __name__ == '__main__':
    main()
