"""The -march level the game is built for: x86-64-v3 (AVX2, FMA, BMI2) when the CPU of this PC has it, else x86-64-v2.

The build runs on the player's own PC, so the code may use what this CPU offers. A CPU without AVX2 (older Intel
Pentium / Celeron / Atom, some virtual machines) gets x86-64-v2: the lifted code is the same, the FMADD family then goes
through the C library's fma() in software (correct, slower). KARPUR_MARCH=x86-64-v2 (or another -march value) forces a
level. The answer is clang's own view of the host (-march=native's predefined macros), so it needs the compiler.
"""

import os
import subprocess


def march(clang):
    forced = os.environ.get('KARPUR_MARCH')
    if forced:
        return forced
    try:
        out = subprocess.run([str(clang), '-march=native', '-E', '-dM', '-x', 'c', os.devnull],
                             capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return 'x86-64-v3'   # no answer: the level every build so far used
    have = {line.split()[1] for line in out.splitlines() if line.startswith('#define __')}
    if {'__AVX2__', '__FMA__', '__BMI2__'} <= have:
        return 'x86-64-v3'
    return 'x86-64-v2'
