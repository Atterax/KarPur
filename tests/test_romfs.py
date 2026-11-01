"""Index and report a small hand-built RomFS image with tools/extract_romfs.py --index-only. No game files needed.

    python tests/test_romfs.py
"""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NONE = 0xFFFFFFFF


def name_bytes(name):
    raw = name.encode()
    return raw + b'\0' * (-len(raw) % 4)


def build_romfs(path):
    """Root with file 'readme.txt' and folder 'arc' holding 'data.trpfs' and 'data.trpfd'."""
    data = b'hello' + bytes(11) + b'A' * 1000 + bytes(8) + b'B' * 40
    # Root file at 0, arc files after it.
    f0 = struct.pack('<IIQQII', 0, NONE, 0, 5, NONE, 10) + name_bytes('readme.txt')
    f1_off = len(f0)
    f2_off = f1_off + 32 + len(name_bytes('data.trpfs'))
    f1 = struct.pack('<IIQQII', 24, f2_off, 16, 1000, NONE, 10) + name_bytes('data.trpfs')
    f2 = struct.pack('<IIQQII', 24, NONE, 1024, 40, NONE, 10) + name_bytes('data.trpfd')
    files_meta = f0 + f1 + f2
    dirs_meta = struct.pack('<6I', 0, NONE, 24, 0, NONE, 0) + struct.pack('<6I', 0, NONE, NONE, f1_off, NONE, 3) + name_bytes('arc')
    hdr_size = 80
    dir_off = hdr_size
    file_off = dir_off + len(dirs_meta)
    data_off = (file_off + len(files_meta) + 15) & ~15
    hdr = struct.pack('<10Q', 80, dir_off, 0, dir_off, len(dirs_meta), file_off, 0, file_off, len(files_meta), data_off)
    img = hdr + dirs_meta + files_meta
    img += bytes(data_off - len(img)) + data
    path.write_bytes(img)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / 'romfs'
        out.mkdir()
        build_romfs(out / 'romfs.bin')
        r = subprocess.run([sys.executable, str(ROOT / 'tools' / 'extract_romfs.py'), '--index-only', '--out', str(out)],
                           capture_output=True, text=True, cwd=ROOT)
        assert r.returncode == 0, r.stdout + r.stderr
        index = json.loads((out / 'index.json').read_text())
        paths = {f['path']: f['size'] for f in index['files']}
        assert paths == {'readme.txt': 5, 'arc/data.trpfs': 1000, 'arc/data.trpfd': 40}, paths
        assert '3 files' in r.stdout and '.trpfs' in r.stdout and 'arc' in r.stdout, r.stdout
    print('test_romfs: OK')


if __name__ == '__main__':
    main()
