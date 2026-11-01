"""Parse Switch NSO executables: header, segments, dynamic symbols and identifying strings.

Format: https://switchbrew.org/wiki/NSO. Only metadata leaves this module through the report: sizes, hashes,
symbol names and version strings, never code or data bytes.
"""

import hashlib
import re
import struct
from collections import Counter
from dataclasses import dataclass, field

import lz4.block

SEGMENTS = ('text', 'rodata', 'data')
SDK_VERSION = re.compile(rb'NintendoSdk_nnSdk-([0-9][0-9A-Za-z_.\-]{2,40})')
MIDDLEWARE = re.compile(rb'SDK MW\+([\x21-\x7e]{1,120})')


class NsoError(ValueError):
    pass


@dataclass
class Symbol:
    name: str
    defined: bool
    kind: int        # ELF symbol type: 1 object, 2 function
    binding: int     # ELF binding: 1 global, 2 weak


@dataclass
class Nso:
    name: str
    build_id: str
    segments: dict = field(default_factory=dict)     # name -> (memory offset, size, decompressed bytes)
    bss_size: int = 0
    symbols: list = field(default_factory=list)

    @property
    def imports(self):
        return [s for s in self.symbols if not s.defined and s.name]

    @property
    def exports(self):
        return [s for s in self.symbols if s.defined and s.name]

    def strings(self, pattern):
        found = []
        for _, _, data in self.segments.values():
            found += [m.group(1).decode('ascii') for m in pattern.finditer(data)]
        return sorted(set(found))


def parse(name, raw):
    if len(raw) < 0x100 or raw[:4] != b'NSO0':
        raise NsoError(f'{name}: not an NSO file')
    flags = struct.unpack_from('<I', raw, 0xC)[0]
    build_id = raw[0x40:0x60].rstrip(b'\0').hex().upper() or '0'
    nso = Nso(name, build_id, bss_size=struct.unpack_from('<I', raw, 0x3C)[0])
    for i, seg in enumerate(SEGMENTS):
        file_off, mem_off, size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
        file_size = struct.unpack_from('<I', raw, 0x60 + i * 4)[0]
        blob = raw[file_off:file_off + file_size]
        if len(blob) != file_size:
            raise NsoError(f'{name}: {seg} segment ends early')
        if flags & (1 << i):
            try:
                blob = lz4.block.decompress(blob, uncompressed_size=size)
            except lz4.block.LZ4BlockError as e:
                raise NsoError(f'{name}: {seg} segment does not decompress ({e})') from None
        if len(blob) != size:
            raise NsoError(f'{name}: {seg} segment has the wrong size')
        if flags & (8 << i) and hashlib.sha256(blob).digest() != raw[0xA0 + i * 0x20:0xC0 + i * 0x20]:
            raise NsoError(f'{name}: {seg} segment hash mismatch (damaged file)')
        nso.segments[seg] = (mem_off, size, blob)
    nso.symbols = _dynamic_symbols(nso, raw)
    return nso


def _dynamic_symbols(nso, raw):
    """.dynsym/.dynstr, whose header offsets are relative to the start of .rodata."""
    dynstr_off, dynstr_size, dynsym_off, dynsym_size = struct.unpack_from('<IIII', raw, 0x90)
    ro = nso.segments['rodata'][2]
    if not dynsym_size or dynsym_off + dynsym_size > len(ro) or dynstr_off + dynstr_size > len(ro):
        return []
    strtab = ro[dynstr_off:dynstr_off + dynstr_size]
    out = []
    for pos in range(dynsym_off, dynsym_off + dynsym_size - 23, 24):
        st_name, st_info, _, st_shndx = struct.unpack_from('<IBBH', ro, pos)
        end = strtab.find(b'\0', st_name)
        sym_name = strtab[st_name:end if end >= 0 else None].decode('ascii', 'replace')
        out.append(Symbol(sym_name, st_shndx != 0, st_info & 0xF, st_info >> 4))
    return out


def namespace(symbol, depth=2):
    """'nn::fs' for a mangled nn::fs::... name; 'C' for an unmangled (C) name."""
    if not symbol.startswith('_Z'):
        return 'C'
    s = symbol[2:]
    if not s.startswith('N'):
        m = re.match(r'(\d+)', s)
        return s[len(m.group(1)):len(m.group(1)) + int(m.group(1))] if m else '?'
    s = s[1:].lstrip('KVr')
    parts = []
    while len(parts) < depth:
        if s.startswith('St'):            # std:: abbreviation
            parts.append('std')
            s = s[2:]
            continue
        m = re.match(r'(\d+)', s)
        if not m:
            break
        n = int(m.group(1))
        start = len(m.group(1))
        parts.append(s[start:start + n])
        s = s[start + n:]
    return '::'.join(parts) or '?'


def namespace_counts(symbols, depth=2):
    return Counter(namespace(s.name, depth) for s in symbols)
