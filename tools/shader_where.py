"""Report where the game keeps the shader programs KarPur has met in play: names and numbers only.

Purpose (docs/progress.md, shader pre-build): building every shader after the install needs the shader code from the
player's own game files. This tool takes the programs the runtime recorded while playing (build/game/shader-programs.bin,
the player's own data, never shared) and looks for their code in the unpacked RomFS (game/romfs/romfs.bin). Code inside
an Oodle-packed file of data.trpfs cannot be found this way: the report counts those as "not found".

For each recorded program an 8-byte anchor from the middle of its code is searched at every byte position (numpy, the
image in 256 MiB pieces); a hit counts once 64 bytes around it match the program. A hit is placed in its RomFS file
and, inside arc/data.trpfs, in its pack (name, main file type) and the packed file it lies in (stored or compressed).

It prints counts, file and pack names, sizes and offsets, never file contents, and writes the report to
game/report/shader-where.txt.

    .venv\\Scripts\\python tools\\shader_where.py
"""
import argparse
import bisect
import json
import struct
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from trpfs import Table, fnv1a64, inner_type  # noqa: E402

CONTROL = 0x880
HEAD = 12 + 32 + CONTROL          # runtime/gfx_shader.c PRG_HEAD
CHUNK = 256 << 20
ANCHOR_AT = 0.5                   # where in the code the anchor is taken (fraction of its length)


def programs(path):
    """(stage, code bytes) of every record of shader-programs.bin."""
    b = path.read_bytes()
    if b[:8] != b'TOTKSPRG':
        sys.exit(f'error: {path} is not a shader-programs.bin (play a little first: the game records them)')
    out, o = [], 16
    while len(b) - o >= HEAD:
        stage, ln, c1 = struct.unpack_from('<III', b, o)
        size = HEAD + ln + c1
        if ln > (64 << 20) or c1 > ln or size > len(b) - o:
            break
        out.append((stage, b[o + HEAD:o + HEAD + ln]))
        o += size
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--programs', type=Path, default=ROOT / 'build' / 'game' / 'shader-programs.bin')
    ap.add_argument('--romfs', type=Path, default=ROOT / 'game' / 'romfs')
    a = ap.parse_args()
    out = []

    def say(line=''):
        print(line, flush=True)
        out.append(line)

    progs = programs(a.programs)
    say('=' * 24 + ' KarPur shader location report ' + '=' * 24)
    say(f'recorded programs: {len(progs)}, code {sum(len(c) for _, c in progs) / 2**20:.1f} MiB; stages '
        + ', '.join(f'{s}: {n}' for s, n in sorted(Counter(s for s, _ in progs).items())))

    # anchors: 8 bytes from the middle of each program's code (programs shorter than 128 bytes are left out)
    anchors = {}
    for i, (_, code) in enumerate(progs):
        if len(code) < 128:
            continue
        at = (int(len(code) * ANCHOR_AT) // 8) * 8
        v = struct.unpack_from('<Q', code, at)[0]
        if v and v != 0xFFFFFFFFFFFFFFFF:
            anchors.setdefault(v, []).append((i, at))
    keys = np.array(sorted(anchors), dtype=np.uint64)
    say(f'anchors: {len(keys)} distinct for {sum(len(v) for v in anchors.values())} programs')

    index = json.loads((a.romfs / 'index.json').read_text(encoding='utf-8'))
    files = sorted(index['files'], key=lambda e: e['offset'])
    starts = [e['offset'] for e in files]
    image = a.romfs / 'romfs.bin'
    total = image.stat().st_size

    # search: every byte position, as 8 shifted uint64 views of each piece
    found = {}   # program index -> romfs offset of its code
    t0 = time.time()
    with image.open('rb') as f:
        pos = 0
        while pos < total:
            f.seek(pos)
            buf = f.read(CHUNK + 64)
            if not buf:
                break
            for shift in range(8):
                n = (len(buf) - shift) // 8
                if n <= 0:
                    continue
                view = np.frombuffer(buf, dtype=np.uint64, count=n, offset=shift)
                idx = np.searchsorted(keys, view)
                idx[idx >= len(keys)] = 0
                hits = np.nonzero(keys[idx] == view)[0]
                for h in hits:
                    v = int(view[h])
                    off_in_buf = shift + int(h) * 8
                    for pi, at in anchors[v]:
                        if pi in found:
                            continue
                        code = progs[pi][1]
                        lo = off_in_buf - at
                        a0, a1 = max(0, at - 32), min(len(code), at + 32)
                        if lo + a0 >= 0 and lo + a1 <= len(buf) and buf[lo + a0:lo + a1] == code[a0:a1]:
                            found[pi] = pos + lo
            pos += CHUNK
            done = min(pos, total)
            print(f'  searched {done / 2**30:5.1f} of {total / 2**30:.1f} GiB, {len(found)} found, '
                  f'{time.time() - t0:4.0f} s', end='\r', flush=True)
    print()
    say(f'found {len(found)} of {len(progs)} programs in romfs.bin ({time.time() - t0:.0f} s)')

    # where: the RomFS file, and inside data.trpfs the pack and its packed file
    img = image.open('rb')

    def read(off, n):
        img.seek(off)
        return img.read(n)

    def romfs_of(off):
        i = bisect.bisect_right(starts, off) - 1
        return files[i] if i >= 0 and off < files[i]['offset'] + files[i]['size'] else None

    trpfs = next((e for e in files if e['path'] == 'arc/data.trpfs'), None)
    trpfd = next((e for e in files if e['path'] == 'arc/data.trpfd'), None)
    packs = []
    if trpfs and trpfd:
        fd = Table.root(read(trpfd['offset'], trpfd['size']))
        names, info = fd.strings(1), [(t.scalar(0, 'Q'), t.scalar(1, 'Q')) for t in fd.tables(3)]
        _, root_at = struct.unpack('<8sQ', read(trpfs['offset'], 16))
        fs = Table.root(read(trpfs['offset'] + root_at, trpfs['size'] - root_at))
        off_of = dict(zip(fs.u64s(0), fs.u64s(1)))
        for i, nm in enumerate(names):
            o = off_of.get(fnv1a64(nm))
            if o is not None:
                packs.append((trpfs['offset'] + o, info[i][0], nm))
        packs.sort()
    pack_starts = [p[0] for p in packs]
    toc_cache = {}

    def packed_file(pack, off):
        """(entry number, compression, packed size) of the file of pack whose data holds off, or None."""
        start, size, nm = pack
        if nm not in toc_cache:
            toc_cache[nm] = None
            if size <= 512 << 20:
                buf = read(start, size)
                pk = Table.root(buf)
                spans = []
                for k, e in enumerate(pk.tables(1)):
                    ds, dn = e._vec(len(e.slots) - 1)
                    spans.append((start + ds, start + ds + dn, k, e.scalar(1, 'b', -1)))
                toc_cache[nm] = spans
        for s, e, k, comp in toc_cache[nm] or []:
            if s <= off < e:
                return k, comp, e - s
        return None

    by_file, by_pack, by_type, comp_kinds, inside = Counter(), Counter(), Counter(), Counter(), Counter()
    per_pack_progs = defaultdict(int)
    for pi, off in found.items():
        rf = romfs_of(off)
        by_file[rf['path'] if rf else '?'] += 1
        if rf is trpfs and packs:
            j = bisect.bisect_right(pack_starts, off) - 1
            if j >= 0 and off < packs[j][0] + packs[j][1]:
                p = packs[j]
                by_pack[p[2]] += 1
                by_type[inner_type(p[2])] += 1
                pf = packed_file(p, off)
                if pf:
                    comp_kinds['stored' if pf[1] == -1 else f'compression {pf[1]}'] += 1
                    inside[(p[2], pf[0])] += 1
    say('RomFS files holding found programs: ' + ', '.join(f'{p} {n}' for p, n in by_file.most_common(12)))
    if by_pack:
        say(f'data.trpfs packs holding found programs: {len(by_pack)}; by main file type: '
            + ', '.join(f'{t} {n}' for t, n in by_type.most_common()))
        say('their packed files: ' + ', '.join(f'{k} {n}' for k, n in comp_kinds.most_common())
            + f'; {len(inside)} distinct packed files')
        say('packs with the most programs:')
        for nm, n in by_pack.most_common(15):
            say(f'  {n:5}  {nm}')
    st = Counter(progs[pi][0] for pi in found)
    say('found by stage: ' + ', '.join(f'{s}: {n} of {sum(1 for x, _ in progs if x == s)}' for s, n in sorted(st.items())))
    # alignment of the code start in its packed file, to plan the census
    al = Counter()
    for pi, off in found.items():
        for b in (256, 128, 64, 32, 16, 8):
            if off % b == 0:
                al[b] += 1
                break
        else:
            al[1] += 1
    say('code start alignment in romfs.bin: ' + ', '.join(f'{b}: {n}' for b, n in sorted(al.items(), reverse=True)))
    say('=' * 79)
    rep = ROOT / 'game' / 'report' / 'shader-where.txt'
    rep.parent.mkdir(parents=True, exist_ok=True)
    rep.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print(f'report: {rep}')


if __name__ == '__main__':
    main()
