"""Check the hints of tools/phases.py on a small hand-built module: a string reference, a PLT call and a vtable with
its RTTI name, plus the name demangling. No game files needed.

    python tests/test_phases.py
"""

import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import phases  # noqa: E402

BASE = 0x7000000000


def adrp(rd, pages):
    return 0x90000000 | ((pages & 3) << 29) | (((pages >> 2) & 0x7FFFF) << 5) | rd


def add_imm(rd, rn, imm):
    return 0x91000000 | (imm << 10) | (rn << 5) | rd


def bl(pc, target):
    return 0x94000000 | (((target - pc) >> 2) & 0x3FFFFFF)


def build():
    """.text 0x000-0x400 (functions at 0x0, 0x40 and a PLT stub at 0x200), strings at 0x2000, data at 0x3000."""
    img = bytearray(0x4000)
    words = np.zeros(0x100, dtype='<u4')
    words[0] = adrp(0, 2)                 # x0 = page 0x2000
    words[1] = add_imm(0, 0, 0x10)        # x0 = 0x2010, "Hello editor"
    words[2] = bl(0x8, 0x200)             # call the import
    words[3] = 0xD65F03C0                 # ret
    words[16] = bl(0x40, 0x200)           # 0x40: not covered, so its call is not counted
    words[17] = 0xD65F03C0
    img[0:0x400] = words.tobytes()
    img[0x2010:0x2010 + 13] = b'Hello editor\0'
    img[0x2100:0x2100 + 14] = b'N3gfl6EditorE\0'
    relative = {
        0x3008: 0x2100,                   # RTTI object at 0x3000: name pointer
        0x3108: 0x3000,                   # vtable at 0x3100: offset-to-top 0, RTTI, then two functions
        0x3110: 0x0,
        0x3118: 0x40,
    }
    symbolic = {0x3000}                   # RTTI object: pointer to the type_info vtable (an import)
    plt = lambda t: '_ZN2nn3hid12GetNpadStateEPv' if t == BASE + 0x200 else None
    v = phases.View(BASE, BASE, words, bytes(img), relative, symbolic, [BASE, BASE + 0x40, BASE + 0x200], plt)
    return v, words


def main():
    fails = 0

    def check(what, got, want):
        nonlocal fails
        ok = got == want
        fails += not ok
        print(f'{"ok  " if ok else "FAIL"} {what}: {got!r}' + ('' if ok else f' (want {want!r})'))

    check('demangle nested', phases.demangle('_ZN2nn3hid12GetNpadStateEPNS0_9NpadStateE'), 'nn::hid::GetNpadState')
    check('demangle const method', phases.demangle('_ZNK2nn2fs4Path3GetEv'), 'nn::fs::Path::Get')
    check('demangle template', phases.demangle('_ZN3gfl6VectorIiE4pushEi'), 'gfl::Vector<...>::push')
    check('demangle ctor', phases.demangle('_ZN3gfl6EditorC2Ev'), 'gfl::Editor::Editor')
    check('demangle C name', phases.demangle('nvnBootstrapLoader'), 'nvnBootstrapLoader')
    check('type name', phases.type_name('N3gfl4core6ObjectE'), 'gfl::core::Object')
    check('type name, top level', phases.type_name('6Object'), 'Object')
    check('type name, not a class', phases.type_name('Hello editor'), None)
    check('group nn', phases.group('nn::hid::GetNpadState'), 'nn::hid')
    check('group C', phases.group('nvnBootstrapLoader'), 'C nvn')

    v, _ = build()
    funcs = {0x0: 0b1000}   # only the function at 0x0 ran, in phase 3
    classes, imports, strings = phases.analyse(v, funcs)
    check('class of 0x0', classes.get(BASE), ['gfl::Editor'])
    check('class of 0x40', classes.get(BASE + 0x40), ['gfl::Editor'])
    check('strings of 0x0', strings.get(BASE), {'Hello editor'})
    check('imports of 0x0', imports.get(BASE), {'nn::hid::GetNpadState'})
    check('imports of 0x40 (not covered)', imports.get(BASE + 0x40), None)
    check('first phase', phases.first_phase(0b1000), 3)
    check('first use', phases.first_use({1: {'assert', 'editor'}, 2: {'assert'}}, {1: 3, 2: 0}),
          {'assert': 0, 'editor': 3})

    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / 'functions.txt').write_text('# header\n0 8\n40 3\n')
        (d / 'phases.txt').write_text('# header\n0 0 2 2 1\n3 1998 1 1 1\n')
        got = phases.read_coverage(d)
        check('read coverage', got, ({0x0: 0x8, 0x40: 0x3}, {0: 0, 3: 1998}))
        phases.write_functions(d / 'out.txt', {0x0: 0x8}, v, classes, imports, strings)
        check('function list line', (d / 'out.txt').read_text().splitlines()[1],
              '0 8 16 gfl::Editor nn::hid::GetNpadState "Hello editor"')
        (d / 'hostcalls.txt').write_text('# header\n# presents: 0 100 300\n'
                                         '5 0 nvn:nvnDeviceInitialize\n0 600 nvn:nvnCommandBufferDrawElements\n'
                                         '2 4 <rtld module_inits>\n'
                                         '0 6000 exec:draws seen\n0 480 exec:calls\n7 1 shader:translated\n'
                                         '0 300000000 time:replay walk\n0 409600 bytes:inline copies\n')
        presents, rows = phases.read_hostcalls(d)
        check('hostcalls presents', presents, [0, 100, 300])
        check('hostcalls rows', rows[:3], [([5, 0], 'nvn:nvnDeviceInitialize'),
                                           ([0, 600], 'nvn:nvnCommandBufferDrawElements'), ([2, 4], '<rtld module_inits>')])
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            phases.host_report(presents, rows, [], [], 5)
        text = out.getvalue()
        check('host report: draws first in phase 1', '600  nvnCommandBufferDrawElements' in text.split('phase 1')[1], True)
        check('host report: 3 draws per present', '3.0  nvnCommandBufferDrawElements' in text, True)
        check('host report: replayed per present', 'draws seen 30.0, calls 2.4' in text, True)
        check('host report: replay ratio', 'draws recorded 600, replayed 6000: each recorded draw ran 10.0 times' in text,
              True)
        check('host report: time and bytes per present', 'replay walk 1.50 ms, inline copies 2 KiB' in text, True)
        check('host report: counters not listed as host functions', 'exec:' not in text.split('first called here')[1],
              True)

    print('all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
