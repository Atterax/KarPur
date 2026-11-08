"""Maintainer tool: pins the Python the launcher downloads (launcher/pins.json).

The launcher (launcher/) runs tools/setup_build.py with Python's official embeddable package and the wheels of
tools/requirements.txt in the versions of this repository's .venv. This tool looks up their download URLs (python.org,
PyPI), downloads each once and writes URL, size and SHA-256 to launcher/pins.json, which the launcher embeds and checks
every download against. Run it with the .venv whose versions the release should use.

  .venv\\Scripts\\python tools\\launcher_pins.py
"""

import hashlib
import json
import sys
import urllib.request
from importlib import metadata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'launcher' / 'pins.json'
WHEELS = ['cryptography', 'cffi', 'pycparser', 'lz4', 'numpy']


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def pinned(url):
    data = fetch(url)
    return {'url': url, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def wheel(name, version, tags):
    info = json.loads(fetch(f'https://pypi.org/pypi/{name}/{version}/json'))
    files = [f for f in info['urls'] if f['packagetype'] == 'bdist_wheel']
    for tag in tags:   # the first tag that has a Windows x64 wheel (or a pure-Python one)
        for f in files:
            if tag in f['filename']:
                return pinned(f['url'])
    sys.exit(f'{name} {version}: no wheel for {tags}')


def main():
    v = sys.version_info
    py = f'{v.major}.{v.minor}.{v.micro}'
    cp = f'cp{v.major}{v.minor}'
    pins = {'python': pinned(f'https://www.python.org/ftp/python/{py}/python-{py}-embed-amd64.zip'),
            'python_version': py, 'wheels': []}
    for name in WHEELS:
        version = metadata.version(name)
        pins['wheels'].append(dict(name=name, version=version,
                                   **wheel(name, version, [f'{cp}-{cp}-win_amd64', 'abi3-win_amd64', 'py3-none-any'])))
        print(f'{name} {version}: {pins["wheels"][-1]["url"].rsplit("/", 1)[1]}')
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(pins, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'{OUT}: Python {py} and {len(pins["wheels"])} wheels')


if __name__ == '__main__':
    main()
