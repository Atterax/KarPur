"""Build tests/lifter/guest.c for AArch64 and wrap it as an NSO, the way Switch executables are laid out.

    python tests/lifter/build_guest.py OUT_DIR

Writes OUT_DIR/exefs/main (the NSO) and OUT_DIR/tests.json (test function offsets). Needs clang with the AArch64
target, ld.lld, llvm-objcopy and llvm-nm on PATH (the Linux CI has them; MSYS2 UCRT64 clang works as well).
"""

import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TARGET = ['--target=aarch64-linux-gnu', '-march=armv8-a']   # the Switch's Cortex-A57 is ARMv8.0
# -ffp-contract=off: the AArch64 build would fuse a*b+c into one FMADD (one rounding) where the host build does not
# (two roundings), which differ in the last bit. The lifter models FMADD exactly; the test compares like with like.
CFLAGS = ['-O2', '-ffp-contract=off', '-ffreestanding', '-fPIC', '-fvisibility=hidden', '-fasynchronous-unwind-tables']


def tool(name):
    path = shutil.which(name)
    if not path:
        sys.exit(f'{name} not found on PATH')
    return path


def run(*cmd):
    subprocess.run([str(c) for c in cmd], check=True)


def symbols(elf):
    out = subprocess.run([tool('llvm-nm'), str(elf)], check=True, capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def build(out):
    out.mkdir(parents=True, exist_ok=True)
    work = out / 'elf'
    work.mkdir(exist_ok=True)
    clang = tool('clang')
    run(clang, *TARGET, *CFLAGS, '-c', HERE / 'guest.c', '-o', work / 'guest.o')
    run(clang, *TARGET, '-c', HERE / 'crt0.S', '-o', work / 'crt0.o')
    run(tool('ld.lld'), '-shared', '-T', HERE / 'link.ld', '--eh-frame-hdr', '-z', 'notext',
        work / 'crt0.o', work / 'guest.o', '-o', work / 'guest.elf')
    run(tool('llvm-objcopy'), '-O', 'binary', work / 'guest.elf', work / 'flat.bin')
    flat = (work / 'flat.bin').read_bytes()
    sym = symbols(work / 'guest.elf')

    segments = [(0, sym['__text_end']), (sym['__ro_start'], sym['__ro_end']), (sym['__data_start'], sym['__data_end'])]
    blobs = [flat[a:b] + bytes(max(0, b - len(flat))) for a, b in segments]
    bss = sym['__bss_end'] - sym['__data_end']

    hdr = bytearray(0x100)
    hdr[0:4] = b'NSO0'
    file_off = 0x100
    for i, ((mem, _), blob) in enumerate(zip(segments, blobs)):
        struct.pack_into('<III', hdr, 0x10 + i * 0x10, file_off, mem, len(blob))
        struct.pack_into('<I', hdr, 0x60 + i * 4, len(blob))
        file_off += len(blob)
    struct.pack_into('<I', hdr, 0x3C, bss)
    hdr[0x40:0x48] = b'KARPURT\0'
    (out / 'exefs').mkdir(exist_ok=True)
    (out / 'exefs' / 'main').write_bytes(bytes(hdr) + b''.join(blobs))
    tests = {name: addr for name, addr in sym.items() if name.startswith('t_')}
    (out / 'tests.json').write_text(json.dumps(tests, indent=1, sort_keys=True))
    return tests


if __name__ == '__main__':
    tests = build(Path(sys.argv[1]).resolve())
    print(f'{len(tests)} test functions')
