"""The C toolchain of a launcher build: MSYS2 UCRT64 packages, pinned, unpacked into a folder instead of installing MSYS2.

The build (tools/lift.py, tools/build_game.py) needs clang, lld, ninja and the libraries the runtime links (SDL3, Opus,
libvpx, zstd, Vulkan). MSYS2 ships them as .pkg.tar.zst archives whose files sit under ucrt64/; unpacked side by side
they form the same tree as an MSYS2 install, and KARPUR_UCRT points the build at it. Each archive is pinned by file name
and SHA-256 in tools/data/toolchain_msys2.json, so a download that changed is refused.

  python tools/toolchain_msys2.py pin             # maintainer: write the pins from the local MSYS2 package cache
  python tools/toolchain_msys2.py fetch DEST      # download, check and unpack into DEST (DEST/ucrt64/bin/clang.exe)
"""

import argparse
import hashlib
import json
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = ROOT / 'tools' / 'data' / 'toolchain_msys2.json'
MIRRORS = ['https://mirror.msys2.org/mingw/ucrt64/', 'https://repo.msys2.org/mingw/ucrt64/']
PREFIX = 'mingw-w64-ucrt-x86_64-'
# What the build uses and everything those packages depend on (pactree -u on 2026-11-08).
PACKAGES = ['clang', 'clang-libs', 'llvm-libs', 'lld', 'ninja', 'sdl3', 'opus', 'libvpx', 'zstd', 'vulkan-headers',
            'vulkan-loader', 'binutils', 'cc-libs', 'crt', 'headers', 'gcc', 'libgcc', 'libstdc++', 'gettext-runtime',
            'gmp', 'isl', 'libffi', 'libiconv', 'libwinpthread', 'libxml2', 'mpc', 'mpfr', 'tzdata',
            'windows-default-manifest', 'winpthreads', 'zlib']
CACHE = Path(r'C:\msys64\var\cache\pacman\pkg')
PACMAN = Path(r'C:\msys64\usr\bin\pacman.exe')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def pin():
    """The installed version of each package, from the local pacman cache (names, sizes and hashes only)."""
    pins = []
    for short in PACKAGES:
        name = PREFIX + short
        r = subprocess.run([str(PACMAN), '-Q', name], capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'{name} is not installed')
        version = r.stdout.split()[1]
        file = f'{name}-{version}-any.pkg.tar.zst'
        path = CACHE / file
        if not path.is_file():
            sys.exit(f'{file} is not in {CACHE}')
        pins.append({'file': file, 'size': path.stat().st_size, 'sha256': sha256(path)})
    PINS.write_text(json.dumps({'packages': pins}, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'{PINS}: {len(pins)} packages, {sum(p["size"] for p in pins) / 2**20:.0f} MiB to download')


def download(file, size, digest, dest, progress):
    """dest/<file> with the pinned hash, fetched from the first mirror that has it."""
    out = dest / file
    if out.is_file() and out.stat().st_size == size and sha256(out) == digest:
        return out
    part = out.with_suffix(out.suffix + '.part')
    last = None
    for mirror in MIRRORS:
        try:
            with urllib.request.urlopen(mirror + file, timeout=60) as r, open(part, 'wb') as f:
                got = 0
                while block := r.read(1 << 20):
                    f.write(block)
                    got += len(block)
                    progress(got)
        except OSError as e:
            last = e
            continue
        if part.stat().st_size == size and sha256(part) == digest:
            part.replace(out)
            return out
        last = f'{file} from {mirror} does not match the pinned SHA-256'
    part.unlink(missing_ok=True)
    raise RuntimeError(f'{file}: download failed ({last})')


def unpack(archive, dest):
    """The archive's files under dest (its pacman metadata, the names starting with '.', stay out)."""
    with tarfile.open(archive, mode='r:zst') as t:
        members = [m for m in t.getmembers() if not m.name.startswith('.')]
        t.extractall(dest, members=members, filter='data')


def fetch(dest, log=print):
    """Downloads and unpacks every pinned package into dest; returns dest/ucrt64 (KARPUR_UCRT)."""
    dest = Path(dest)
    pins = json.loads(PINS.read_text(encoding='utf-8'))['packages']
    cache = dest / 'downloads'
    cache.mkdir(parents=True, exist_ok=True)
    done_file = dest / 'unpacked.json'
    done = set(json.loads(done_file.read_text(encoding='utf-8'))) if done_file.is_file() else set()
    total = sum(p['size'] for p in pins)
    have = 0
    for i, p in enumerate(pins, 1):
        if p['file'] not in done:
            base = have
            log(f'PROGRESS toolchain {i}/{len(pins)} {p["file"]}')
            archive = download(p['file'], p['size'], p['sha256'], cache, lambda got: None)
            unpack(archive, dest)
            done.add(p['file'])
            done_file.write_text(json.dumps(sorted(done)), encoding='utf-8')
            archive.unlink()
            have = base
        have += p['size']
        log(f'PERCENT toolchain {100 * have // total}')
    ucrt = dest / 'ucrt64'
    if not (ucrt / 'bin' / 'clang.exe').is_file():
        raise RuntimeError(f'{ucrt / "bin" / "clang.exe"} is missing after unpacking')
    return ucrt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('pin')
    f = sub.add_parser('fetch')
    f.add_argument('dest', type=Path)
    a = ap.parse_args()
    if a.cmd == 'pin':
        pin()
    else:
        print(fetch(a.dest))


if __name__ == '__main__':
    main()
