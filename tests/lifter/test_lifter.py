"""Differential test of the lifter: lift tests/lifter/guest.c from AArch64, run it on x86-64, compare with native.

    python tests/lifter/test_lifter.py [--keep DIR] [--inner]

Steps: build the guest as an NSO (build_guest.py), generate C with recomp/gen.py, load the image at its guest base
with relocations applied, and run tests/lifter/harness.c, which calls every lifted test function on 400 inputs and
compares each result with the host build of the same function. Linux only (the harness maps the image at a fixed
address); needs clang, ld.lld, llvm-objcopy and llvm-nm. --inner lifts every function after the first as an extra
entry into a range that starts at the previous function (gen.py --inner-entry-test), to test entries inside a function
lifted whole.
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / 'recomp'))
sys.path.insert(0, str(HERE))

import build_guest  # noqa: E402
from nso import Nso, R_AARCH64_RELATIVE  # noqa: E402

# The flags tools/lift.py compiles generated code with.
GEN_FLAGS = ['-O1', '-fno-strict-aliasing', '-Wno-return-type', '-Wno-unused-label', '-fvisibility=hidden',
             '-march=x86-64-v3']


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f'failed ({r.returncode}): {" ".join(map(str, cmd))}\n{r.stdout}{r.stderr}')
    return r


def relocated_image(main, base):
    nso = Nso(str(main))
    img = bytearray(nso.image)
    for off, typ, _, addend in nso.relocs:
        if typ != R_AARCH64_RELATIVE:
            sys.exit(f'unexpected relocation type {typ}')
        struct.pack_into('<Q', img, off, base + addend)
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--keep', type=Path, help='work in this directory and keep it')
    ap.add_argument('--inner', action='store_true', help='lift with gen.py --inner-entry-test')
    args = ap.parse_args()
    cc = os.environ.get('CC', 'clang')
    with tempfile.TemporaryDirectory() as tmp:
        work = args.keep.resolve() if args.keep else Path(tmp)
        if work.exists() and args.keep:
            shutil.rmtree(work)
        tests = build_guest.build(work)
        gen = work / 'gen'
        run([sys.executable, ROOT / 'recomp' / 'gen.py', '--exefs', work / 'exefs', '--out', gen,
             '--modules', 'main', '--jobs', '2', *(['--inner-entry-test'] if args.inner else [])])
        stats = json.loads((gen / 'main' / 'manifest.json').read_text())['stats']
        print(f'lifted {stats["emitted_functions"]} functions, {stats["instructions"]} instructions, '
              f'{stats["jump_tables"]} jump tables, {stats["unsupported"]} unsupported')
        if stats['unsupported']:
            sys.exit('unsupported instructions in the test program')

        base = int(json.loads((gen / 'layout.json').read_text())[0]['base'], 16)
        (work / 'image.bin').write_bytes(relocated_image(work / 'exefs' / 'main', base))
        lines = [f'uint64_t nat_{n}(uint64_t, uint64_t, uint64_t);' for n in sorted(tests)]
        lines.append('static const Test TESTS[] = {')
        lines += [f'    {{"{n}", {a:#x}, nat_{n}}},' for n, a in sorted(tests.items())]
        lines.append('};')
        (work / 'tests.h').write_text('\n'.join(lines) + '\n')

        objs = []
        inc = ['-I', ROOT / 'runtime', '-I', gen, '-I', work]
        for src in sorted((gen / 'main').glob('*.c')):
            obj = work / (src.stem + '.o')
            run([cc, *GEN_FLAGS, *inc, '-c', src, '-o', obj])
            objs.append(obj)
        run([cc, '-O1', '-march=x86-64-v3', '-Wall', *inc, '-c', HERE / 'harness.c', '-o', work / 'harness.o'])
        run([cc, '-O2', '-ffp-contract=off', '-DNATIVE', '-ffreestanding', '-c', HERE / 'guest.c', '-o', work / 'native.o'])
        run([cc, *objs, work / 'harness.o', work / 'native.o', '-o', work / 'harness'])
        r = subprocess.run([str(work / 'harness'), str(work / 'image.bin')], capture_output=True, text=True)
        print(r.stdout + r.stderr, end='')
        if r.returncode:
            sys.exit('test_lifter: FAILED')
    print('test_lifter: OK')


if __name__ == '__main__':
    main()
