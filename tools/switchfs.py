"""Read Switch game containers in place: XCI and NSP, NCA3 headers, tickets and the ExeFS of a Program NCA.

Adapted from MacNativeGames `games/totk/scripts/extract_exefs.py` (MIT, Copyright (c) 2026 MacNativeGames
contributors), as vendored by TotK Recomp at upstream b75be8cfa704df3701e86130c8ced268e79d6b11. Changes: no fixed
TotK title IDs (the caller decides which NCA it wants), DLC inventory, errors that name the missing key.

Keys stay in memory and are never printed; errors name a key, never its value. Nothing is written here: the caller
decides what to do with the returned files.
"""

import hashlib
import io
import struct
from dataclasses import dataclass
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

CONTENT_TYPES = {0: 'Program', 1: 'Meta', 2: 'Control', 3: 'Manual', 4: 'Data', 5: 'PublicData'}
FS_TYPE_PFS0 = b'\x01\x02'   # partition type 1 (PFS0), hash type 2 (hierarchical SHA-256)
SECTION_PATCH = 4            # encryption type 4: AES-CTR with a BKTR patch table (an update's RomFS)


class GameFileError(ValueError):
    """A game file that cannot be read with the given keys, or is not what it should be."""


def require(condition, message):
    if not condition:
        raise GameFileError(message)


def decrypt(key, mode, data):
    ctx = Cipher(algorithms.AES(key), mode).decryptor()
    return ctx.update(data) + ctx.finalize()


def read_at(f, offset, length):
    require(0 <= offset and 0 <= length <= 512 * 1024 * 1024, 'invalid or too large read')
    f.seek(offset)
    data = f.read(length)
    require(len(data) == length, 'file ends early (damaged or not copied completely)')
    return data


def partition_entries(f, offset, bound, magic=b'PFS0', stride=24):
    """[(name, absolute offset, size)] of a PFS0 or HFS0 table, without reading the payloads."""
    require(bound >= 16, 'truncated partition header')
    hdr = read_at(f, offset, 16)
    require(hdr[:4] == magic, f'expected {magic.decode()}')
    count, strings_size = struct.unpack_from('<II', hdr, 4)
    require(count <= 4096 and strings_size <= 1024 * 1024, 'invalid partition table')
    size = 16 + count * stride + strings_size
    require(size <= bound, 'partition table out of bounds')
    raw = read_at(f, offset, size)
    strings = raw[16 + count * stride:]
    entries, names = [], set()
    for i in range(count):
        off, length, nameoff = struct.unpack_from('<QQI', raw, 16 + i * stride)
        require(nameoff < len(strings), 'invalid partition file name offset')
        end = strings.find(b'\0', nameoff)
        require(end >= 0, 'unterminated partition file name')
        name = strings[nameoff:end].decode('utf-8')
        require(name not in ('', '.', '..') and '/' not in name and '\\' not in name, 'unsafe partition file name')
        require(name not in names, 'duplicate partition file name')
        names.add(name)
        require(size + off + length <= bound, 'partition entry out of bounds (file damaged or not copied completely)')
        entries.append((name, offset + size + off, length))
    return entries


def container_entries(f, size):
    """('nsp' | 'xci', entries). For an XCI the entries are those of its secure partition, which holds the NCAs."""
    if read_at(f, 0, 4) == b'PFS0':
        return 'nsp', partition_entries(f, 0, size)
    require(size >= 0x138 and read_at(f, 0x100, 4) == b'HEAD', 'not an XCI or NSP file (NSZ/XCZ must be decompressed first)')
    root = struct.unpack('<Q', read_at(f, 0x130, 8))[0]
    partitions = partition_entries(f, root, size - root, b'HFS0', 64)
    secure = [e for e in partitions if e[0] == 'secure']
    require(len(secure) == 1, 'expected one XCI secure partition')
    return 'xci', partition_entries(f, secure[0][1], secure[0][2], b'HFS0', 64)


def load_keys(path):
    keys = {}
    for number, line in enumerate(Path(path).read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            name, value = line.split('=', 1)
            try:
                keys[name.strip().lower()] = bytes.fromhex(value.strip())
            except ValueError:
                raise GameFileError(f'prod.keys line {number} is not hex') from None
    require('header_key' in keys, 'prod.keys has no header_key: dump the keys again with Lockpick_RCM')
    return keys


def common_tickets(f, entries):
    """{rights ID: encrypted title key} of the common tickets in an NSP. Personalized tickets are refused."""
    tickets = {}
    bodies = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}
    for name, offset, size in entries:
        if not name.lower().endswith('.tik'):
            continue
        sig = struct.unpack('<I', read_at(f, offset, 4))[0]
        require(sig in bodies, 'unsupported ticket signature type')
        body = bodies[sig]
        require(size >= body + 0x180, 'truncated ticket')
        data = read_at(f, offset + body, 0x180)
        require(data[0x141] == 0, 'personalized ticket: the NSP needs a common ticket (dump it again as a standard NSP)')
        rights, key = data[0x160:0x170], data[0x40:0x50]
        require(tickets.get(rights, key) == key, 'conflicting tickets')
        tickets[rights] = key
    return tickets


@dataclass
class Nca:
    name: str
    offset: int          # absolute offset in the container file
    size: int
    content_type: int
    title_id: int
    key_generation: int
    header: bytes        # decrypted 0xC00-byte header

    @property
    def type_name(self):
        return CONTENT_TYPES.get(self.content_type, f'type {self.content_type}')

    @property
    def rights_id(self):
        return self.header[0x230:0x240]

    def section_types(self):
        """Encryption type of each present section (4 = BKTR patch)."""
        out = []
        for i in range(4):
            start, end = struct.unpack_from('<II', self.header, 0x240 + 16 * i)
            if start or end:
                out.append(self.header[0x400 + 512 * i + 4])
        return out


def read_nca_header(f, offset, size, keys, name):
    require(size >= 0xc00, f'{name}: truncated NCA header')
    raw = read_at(f, offset, 0xc00)
    # Nintendo's XTS takes the sector number big-endian as the tweak, unlike standard XTS.
    hdr = b''.join(decrypt(keys['header_key'], modes.XTS(i.to_bytes(16, 'big')), raw[i * 512:(i + 1) * 512])
                   for i in range(6))
    require(hdr[0x200:0x204] == b'NCA3', f'{name}: header does not decrypt to NCA3 (wrong prod.keys or damaged file)')
    require(struct.unpack_from('<Q', hdr, 0x208)[0] == size, f'{name}: NCA size does not match its container entry')
    for i in range(4):
        start, end = struct.unpack_from('<II', hdr, 0x240 + i * 16)
        require((start == end == 0) or 0xc00 <= start * 512 < end * 512 <= size, f'{name}: NCA section out of bounds')
    return hdr


def nca_inventory(f, entries, keys):
    out = []
    for name, offset, size in entries:
        if not name.lower().endswith('.nca'):
            continue
        hdr = read_nca_header(f, offset, size, keys, name)
        out.append(Nca(name, offset, size, hdr[0x205], struct.unpack_from('<Q', hdr, 0x210)[0],
                       max(hdr[0x206], hdr[0x220]), hdr))
    return out


def nca_ctr_key(nca, keys, tickets):
    generation = max(nca.key_generation - 1, 0)
    if nca.rights_id != bytes(16):
        require(nca.rights_id in tickets, f'{nca.name}: no ticket for its rights ID in the file')
        name = f'titlekek_{generation:02x}'
        require(name in keys, f'prod.keys has no {name}: dump the keys again with Lockpick_RCM on current firmware')
        return decrypt(keys[name], modes.ECB(), tickets[nca.rights_id])
    area = nca.header[0x207]
    require(area < 3, f'{nca.name}: unsupported key area')
    name = f'key_area_key_{("application", "ocean", "system")[area]}_{generation:02x}'
    require(name in keys, f'prod.keys has no {name}: dump the keys again with Lockpick_RCM on current firmware')
    return decrypt(keys[name], modes.ECB(), nca.header[0x300:0x340])[32:48]


def read_exefs(f, nca, keys, tickets):
    """{file name: bytes} of the ExeFS (PFS0) section of a Program NCA, with every hash checked."""
    ctr_key = nca_ctr_key(nca, keys, tickets)
    hdr = nca.header
    for i in range(4):
        start, end = struct.unpack_from('<II', hdr, 0x240 + 16 * i)
        if not start:
            continue
        fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
        require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], f'{nca.name}: FS header hash mismatch')
        if fs[2:4] != FS_TYPE_PFS0:
            continue
        require(fs[4] == 3, f'{nca.name}: ExeFS is not AES-CTR encrypted')
        blocksize, levels = struct.unpack_from('<II', fs, 0x28)
        hashoff, hashsize, pfsoff, pfssize = struct.unpack_from('<QQQQ', fs, 0x30)
        secstart, secsize = start * 512, (end - start) * 512
        require(levels == 2 and blocksize > 0 and pfssize <= 512 * 1024 * 1024, f'{nca.name}: unsupported ExeFS layout')

        def section_read(relative, size):
            require(relative >= 0 and relative + size <= secsize, f'{nca.name}: section read out of bounds')
            absolute = secstart + relative
            aligned = absolute & ~15
            iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
            data = read_at(f, nca.offset + aligned, size + absolute - aligned)
            return decrypt(ctr_key, modes.CTR(iv), data)[absolute - aligned:]

        hashes = section_read(hashoff, hashsize)
        require(hashlib.sha256(hashes).digest() == fs[8:40], f'{nca.name}: ExeFS hash table mismatch (damaged file)')
        pfs = section_read(pfsoff, pfssize)
        for index, pos in enumerate(range(0, pfssize, blocksize)):
            require(hashlib.sha256(pfs[pos:pos + blocksize]).digest() == hashes[index * 32:(index + 1) * 32],
                    f'{nca.name}: ExeFS block {index} hash mismatch (damaged file)')
        files = {name: pfs[off:off + size] for name, off, size in partition_entries(io.BytesIO(pfs), 0, len(pfs))}
        require('main.npdm' in files, f'{nca.name}: PFS0 section is not an ExeFS')
        return files
    raise GameFileError(f'{nca.name}: no ExeFS section')


def open_container(path, keys):
    """(kind, tickets, [Nca]) of an XCI or NSP."""
    path = Path(path)
    require(path.is_file(), f'file not found: {path}')
    try:
        with path.open('rb') as f:
            kind, entries = container_entries(f, path.stat().st_size)
            return kind, common_tickets(f, entries), nca_inventory(f, entries, keys)
    except GameFileError as e:
        raise GameFileError(f'{path.name}: {e}') from None
