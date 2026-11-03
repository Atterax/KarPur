"""Report the functions of a Lua 5.4 bytecode chunk (the game's .blua scripts in game/dump) by names and numbers.

Purpose: find the script functions behind the main menu (X menu) and the settings page, to add a "PC settings" entry
(docs/progress.md 2026-11-03). The game's UI logic is Haxe compiled to Lua 5.4 and shipped as bytecode without debug
names (script/lua/bin/release/main/main.blua). This tool walks the chunk's function prototypes and, for each function
whose string constants contain one of the words given, prints its path in the prototype tree (0.12.3), its size in
instructions, its numbers of constants, upvalues and inner functions, and its identifier-like string constants (field,
method and pane names; texts are left out). --at OFFSET names the function whose constants hold that byte offset.

  .venv\\Scripts\\python tools\\luareport.py <file.blua> --find L_xbutton_04,N_dlc_00
  .venv\\Scripts\\python tools\\luareport.py <file.blua> --at 0xb5c4b9
  .venv\\Scripts\\python tools\\luareport.py <file.blua> --stats

Prints names, counts and offsets only, never instructions or other script content.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_.:]{0,79}$')


class Chunk:
    def __init__(self, b):
        self.b, self.p = b, 0
        if b[:5] != b'\x1bLua\x54':
            raise ValueError(f'not Lua 5.4 bytecode (header {b[:5]!r})')
        if b[6:12] != b'\x19\x93\r\n\x1a\n':
            raise ValueError('unexpected LUAC_DATA')
        self.isz, self.intsz, self.numsz = b[12], b[13], b[14]
        if (self.isz, self.intsz, self.numsz) != (4, 8, 8):
            raise ValueError(f'sizes {self.isz}/{self.intsz}/{self.numsz}, expected 4/8/8')
        self.p = 15 + 8 + 8 + 1   # LUAC_INT, LUAC_NUM, size of the main function's upvalues
        self.funcs = []

    def byte(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def varint(self):
        x = 0
        while True:
            c = self.byte()
            x = (x << 7) | (c & 0x7F)
            if c & 0x80:
                return x

    def string(self):
        n = self.varint()
        if n == 0:
            return None, self.p
        at = self.p
        s = self.b[self.p:self.p + n - 1]
        self.p += n - 1
        return s, at

    def function(self, path):
        start = self.p
        self.string()                     # source (stripped: NULL)
        line0, line1 = self.varint(), self.varint()
        self.p += 3                       # numparams, is_vararg, maxstacksize
        ncode = self.varint()
        self.p += ncode * self.isz
        nk = self.varint()
        consts = []
        for _ in range(nk):
            t = self.byte()
            if t in (0x04, 0x14):         # short and long string
                s, at = self.string()
                consts.append((s, at))
            elif t == 0x03:               # integer
                self.p += 8
            elif t == 0x13:               # float
                self.p += 8
            elif t in (0x00, 0x01, 0x11): # nil, false, true
                pass
            else:
                raise ValueError(f'constant type {t:#x} at {self.p - 1:#x} in function {path}')
        nup = self.varint()
        self.p += nup * 3
        info = {'path': path, 'start': start, 'lines': (line0, line1), 'code': ncode, 'consts': consts, 'nup': nup}
        self.funcs.append(info)
        nproto = self.varint()
        for i in range(nproto):
            self.function(f'{path}.{i}')
        info['protos'] = nproto
        # debug information (empty when stripped)
        n = self.varint()
        self.p += n                       # lineinfo
        n = self.varint()
        for _ in range(n):                # abslineinfo
            self.varint(), self.varint()
        n = self.varint()
        for _ in range(n):                # locvars
            self.string(), self.varint(), self.varint()
        n = self.varint()
        for _ in range(n):                # upvalue names
            self.string()
        info['end'] = self.p


def idents(f):
    out = []
    for s, _ in f['consts']:
        if s is None:
            continue
        t = s.decode('utf-8', 'replace')
        if IDENT.match(t):
            out.append(t)
    return out


def show(f, limit):
    names = idents(f)
    print(f'function {f["path"]}: bytes {f["start"]:#x}..{f["end"]:#x}, {f["code"]} instructions, '
          f'{len(f["consts"])} constants, {f["nup"]} upvalues, {f["protos"]} inner functions')
    print('  names: ' + ', '.join(names[:limit]) + (f', ... {len(names) - limit} more' if len(names) > limit else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('file', type=Path)
    ap.add_argument('--find', help='comma-separated words; functions whose string constants contain one')
    ap.add_argument('--at', help='byte offset (hex); the function whose constants hold it')
    ap.add_argument('--stats', action='store_true', help='totals only')
    ap.add_argument('--names', type=int, default=120, help='names shown per function (default 120)')
    a = ap.parse_args()
    c = Chunk(a.file.read_bytes())
    c.function('0')
    print(f'{a.file.name}: Lua 5.4 bytecode, {len(c.funcs)} functions, {sum(f["code"] for f in c.funcs)} instructions, '
          f'parsed {c.p} of {len(c.b)} bytes')
    if a.stats:
        return
    if a.at:
        off = int(a.at, 16)
        for f in c.funcs:
            if any(at <= off < at + len(s) for s, at in f['consts'] if s):
                show(f, a.names)
    if a.find:
        words = [w.strip() for w in a.find.split(',') if w.strip()]
        hits = [f for f in c.funcs if any(s and any(w.encode() in s for w in words) for s, _ in f['consts'])]
        print(f'functions with {", ".join(words)}: {len(hits)}')
        for f in hits:
            show(f, a.names)


if __name__ == '__main__':
    sys.setrecursionlimit(100000)
    main()
