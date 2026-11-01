"""Build a fake game in the real container formats, so the tools can be tested without game files or console keys.

    python tests/make_fake_game.py OUT_DIR

Writes OUT_DIR/{base.xci, update.nsp, dlc.nsp, prod.keys, config.toml}. Every key is random and every executable is
made up: nothing here comes from a game or a console.
"""

import hashlib
import os
import struct
import sys
from pathlib import Path

import lz4.block
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

APP = 0x01008F6008C5E000
KEYGEN = 0x12           # newest-style key generation, so the tools look up key names with a two-digit suffix


def enc(key, mode, data):
    c = Cipher(algorithms.AES(key), mode).encryptor()
    return c.update(data) + c.finalize()


def pfs0(files):
    names = b''.join(n.encode() + b'\0' for n, _ in files)
    names += b'\0' * (-(16 + 24 * len(files) + len(names)) % 32)
    table, data, name_off = b'', b'', 0
    for n, blob in files:
        table += struct.pack('<QQII', len(data), len(blob), name_off, 0)
        name_off += len(n) + 1
        data += blob
    return b'PFS0' + struct.pack('<III', len(files), len(names), 0) + table + names + data


def hfs0(files):
    names = b''.join(n.encode() + b'\0' for n, _ in files)
    table, data, name_off = b'', b'', 0
    for n, blob in files:
        table += struct.pack('<QQIIQ', len(data), len(blob), name_off, 0x200, 0) + hashlib.sha256(blob[:0x200]).digest()
        name_off += len(n) + 1
        data += blob
    return b'HFS0' + struct.pack('<III', len(files), len(names), 0) + table + names + data


def fake_nso(symbols, strings, text_size=0x4000):
    """symbols: [(name, defined)]. strings: bytes placed in .rodata."""
    dynstr = b'\0'
    dynsym = bytes(24)                               # symbol 0 is the null symbol
    for name, defined in symbols:
        dynsym += struct.pack('<IBBHQQ', len(dynstr), 0x12, 0, 1 if defined else 0, 0x100 if defined else 0, 0)
        dynstr += name.encode() + b'\0'
    rodata = strings + b'\0' * 16
    dynsym_off = len(rodata)
    rodata += dynsym
    dynstr_off = len(rodata)
    rodata += dynstr
    rodata += b'\0' * (-len(rodata) % 0x1000)
    segs = [os.urandom(64) * (text_size // 64), rodata, b'\x11' * 0x1000]
    comp = [lz4.block.compress(s, store_size=False) for s in segs]
    hdr = bytearray(0x100)
    hdr[0:4] = b'NSO0'
    struct.pack_into('<I', hdr, 0xC, 0b111111)       # all three segments compressed and hashed
    file_off, mem_off = 0x100, 0
    for i, (seg, c) in enumerate(zip(segs, comp)):
        struct.pack_into('<III', hdr, 0x10 + i * 0x10, file_off, mem_off, len(seg))
        struct.pack_into('<I', hdr, 0x60 + i * 4, len(c))
        hdr[0xA0 + i * 0x20:0xC0 + i * 0x20] = hashlib.sha256(seg).digest()
        file_off += len(c)
        mem_off += len(seg)
    struct.pack_into('<I', hdr, 0x3C, 0x2000)
    hdr[0x40:0x54] = os.urandom(20)
    struct.pack_into('<IIII', hdr, 0x90, dynstr_off, len(dynstr), dynsym_off, len(dynsym))
    return bytes(hdr) + b''.join(comp)


def exefs():
    sdk_ver = b'NintendoSdk_nnSdk-17.5.3-Release\0'
    imports = [('_ZN2nn2fs8MountRomEPKcPvm', False), ('_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi', False),
               ('_ZNK2nn3gfx6Device4InitEv', False), ('nvnBootstrapLoader', False), ('memcpy', False),
               ('_ZN2nn5fancy9MissingFnEv', False), ('nnMain', True)]
    exports = [(n, True) for n, d in imports if not d and n != '_ZN2nn5fancy9MissingFnEv']
    return pfs0([
        ('main', fake_nso(imports, b'SDK MW+FakeVendor+FakeEngine-1.0\0SDK MW+Nintendo+NintendoSdk_nn_fs\0', 0x8000)),
        ('main.npdm', b'META' + bytes(0x200)),
        ('rtld', fake_nso([], b'')),
        ('sdk', fake_nso(exports, sdk_ver)),
        ('subsdk0', fake_nso([], b'SDK MW+FakeVendor+FakeAudio-2.1\0')),
    ])


def nca(keys, content_type, title_id, sections, rights_id=bytes(16), ticket_key=None):
    """sections: [(fs_type bytes, enc type, payload)] where a PFS0 payload gets a hash table."""
    ctr_key = os.urandom(16)
    body, fs_headers, entries = bytearray(), [], []
    offset = 0xC00
    for fs_type, enc_type, payload in sections:
        fs = bytearray(0x200)
        fs[0:2] = b'\x02\x00'
        fs[2:4] = fs_type
        fs[4] = enc_type
        if fs_type == b'\x01\x02':
            block = 0x1000
            padded = payload + b'\0' * (-len(payload) % block)
            # The last block is hashed over the bytes that exist, not its padding.
            table = b''.join(hashlib.sha256(payload[p:p + block]).digest() for p in range(0, len(payload), block))
            hashes = table + b'\0' * (-len(table) % 0x200)
            fs[8:40] = hashlib.sha256(table).digest()
            struct.pack_into('<II', fs, 0x28, block, 2)
            struct.pack_into('<QQQQ', fs, 0x30, 0, len(table), len(hashes), len(payload))
            plain = hashes + padded
        else:
            plain = payload
        plain += b'\0' * (-len(plain) % 0x200)
        fs[0x140:0x148] = os.urandom(8)
        iv = fs[0x140:0x148][::-1] + (offset // 16).to_bytes(8, 'big')
        body += enc(ctr_key, modes.CTR(iv), plain)
        entries.append((offset // 0x200, (offset + len(plain)) // 0x200))
        fs_headers.append(bytes(fs))
        offset += len(plain)
    size = 0xC00 + len(body)
    hdr = bytearray(0xC00)
    hdr[0x200:0x204] = b'NCA3'
    hdr[0x205] = content_type
    hdr[0x206] = 2
    hdr[0x207] = 0
    struct.pack_into('<Q', hdr, 0x208, size)
    struct.pack_into('<Q', hdr, 0x210, title_id)
    hdr[0x220] = KEYGEN
    hdr[0x230:0x240] = rights_id
    for i, ((start, end), fs) in enumerate(zip(entries, fs_headers)):
        struct.pack_into('<II', hdr, 0x240 + 16 * i, start, end)
        hdr[0x280 + 32 * i:0x2A0 + 32 * i] = hashlib.sha256(fs).digest()
        hdr[0x400 + 512 * i:0x600 + 512 * i] = fs
    gen = f'{KEYGEN - 1:02x}'
    if rights_id != bytes(16):
        ticket_key[rights_id] = enc(keys[f'titlekek_{gen}'], modes.ECB(), ctr_key)
    else:
        area = bytes(32) + ctr_key + bytes(16)
        hdr[0x300:0x340] = enc(keys[f'key_area_key_application_{gen}'], modes.ECB(), area)
    raw = b''.join(enc(keys['header_key'], modes.XTS(i.to_bytes(16, 'big')), bytes(hdr[i * 512:(i + 1) * 512]))
                   for i in range(6))
    return raw + bytes(body)


def ticket(rights_id, enc_key):
    t = bytearray(0x140 + 0x180)
    struct.pack_into('<I', t, 0, 0x10004)
    t[0x140 + 0x40:0x140 + 0x50] = enc_key
    t[0x140 + 0x141] = 0
    t[0x140 + 0x160:0x140 + 0x170] = rights_id
    return bytes(t)


def main(out):
    out.mkdir(parents=True, exist_ok=True)
    gen = f'{KEYGEN - 1:02x}'
    keys = {'header_key': os.urandom(32), f'key_area_key_application_{gen}': os.urandom(16), f'titlekek_{gen}': os.urandom(16)}
    (out / 'prod.keys').write_text(''.join(f'{k} = {v.hex()}\n' for k, v in keys.items()))

    base_prog = nca(keys, 0, APP, [(b'\x01\x02', 3, exefs()), (b'\x00\x03', 3, os.urandom(0x2000))])
    meta = nca(keys, 1, APP, [])
    secure = hfs0([('aaaa.nca', base_prog), ('bbbb.cnmt.nca', meta)])
    root = hfs0([('update', hfs0([])), ('secure', secure)])
    xci = bytearray(0x200)
    xci[0x100:0x104] = b'HEAD'
    struct.pack_into('<Q', xci, 0x130, 0x200)
    (out / 'base.xci').write_bytes(bytes(xci) + root)

    rights = struct.pack('>Q', APP) + bytes(7) + bytes([KEYGEN])
    tk = {}
    upd_prog = nca(keys, 0, APP, [(b'\x01\x02', 3, exefs()), (b'\x00\x03', 4, os.urandom(0x2000))], rights, tk)
    upd_meta = nca(keys, 1, APP | 0x800, [])
    (out / 'update.nsp').write_bytes(pfs0([('cccc.nca', upd_prog), ('dddd.cnmt.nca', upd_meta),
                                          (rights.hex() + '.tik', ticket(rights, tk[rights]))]))

    dlc = [('eeee.nca', nca(keys, 4, APP + 0x1001, [])), ('ffff.nca', nca(keys, 4, APP + 0x1002, [])),
           ('gggg.cnmt.nca', nca(keys, 1, APP + 0x1001, []))]
    (out / 'dlc.nsp').write_bytes(pfs0(dlc))
    (out / 'config.toml').write_text(
        f"[game]\nbase = '{out / 'base.xci'}'\nupdate = '{out / 'update.nsp'}'\ndlc = ['{out / 'dlc.nsp'}']\n"
        f"[keys]\nprod = '{out / 'prod.keys'}'\n")
    print(f'fake game written to {out}')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve())
