"""Unpack the player's DLC (add-on content) into game/dlc/<index>/ for runtime/fs_host.c (MountAddOnContent).

Scarlet/Violet's DLC NSPs are small: the expansions' data ships in the update, and the add-on content itself is a
PublicData NCA with a little RomFS that tells the game the content is owned. Its title ID is the application's
+ 0x1000 + index (Violet 01008F6008C5E000: 01008F6008C5F001 is index 1). For each PublicData NCA of each DLC file this
tool copies the NCA out of its container into game/tmp, hands hactool (tools/build_hactool.py) the NSP's title key and
a cleaned copy of prod.keys through private files, and lets it write the RomFS's files to game/dlc/<index>/. The
runtime lists the indices it finds there to the game (nn::aoc) and mounts the folder when the game asks.

Prints indices, file counts and sizes only. Key values never appear. Without DLC it leaves an empty game/dlc/.

    .venv\\Scripts\\python tools\\extract_dlc.py
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

from extract import base_id, load_config  # noqa: E402
from extract_romfs import HACTOOL, LONG_HEX, copy_range, private_file, say, write_hactool_keys  # noqa: E402
from switchfs import GameFileError, load_keys, open_container, require  # noqa: E402

OUT = ROOT / 'game' / 'dlc'
PUBLIC_DATA = 5


def main():
    _, _, dlc_paths, keys_path = load_config(ROOT / 'config.local.toml')
    keys = load_keys(keys_path)
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    if not dlc_paths:
        say('No DLC given: nothing to unpack.')
        return
    tmp = ROOT / 'game' / 'tmp-dlc'
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    found = 0
    try:
        write_hactool_keys(keys_path, tmp / 'hactool.keys')
        for path in dlc_paths:
            _, tickets, ncas = open_container(Path(path), keys)
            with private_file(tmp / f'title-{found}.keys') as f:
                for rights, key in tickets.items():
                    f.write(f'{rights.hex()} = {key.hex()}\n')
            for nca in ncas:
                if nca.content_type != PUBLIC_DATA:
                    continue
                index = nca.title_id - base_id(nca.title_id) - 0x1000
                require(0 < index < 0x1000, f'{Path(path).name}: add-on content with an unexpected title ID')
                out = OUT / str(index)
                out.mkdir(parents=True, exist_ok=True)
                part = tmp / f'aoc{index}.nca'
                copy_range(path, nca.offset, nca.size, part, f'DLC {index}')
                command = [str(HACTOOL), '-x', '--disablekeywarns', '--suppresskeys', '-k', str(tmp / 'hactool.keys'),
                           f'--romfsdir={out}', str(part)]
                run = subprocess.run(command, env=dict(os.environ, MNG_TITLE_KEYS=str(tmp / f'title-{found}.keys')),
                                     capture_output=True, encoding='utf-8', errors='replace')
                log = LONG_HEX.sub('<hex removed>', run.stdout + run.stderr)
                (ROOT / 'game' / 'report').mkdir(parents=True, exist_ok=True)
                (ROOT / 'game' / 'report' / f'hactool-dlc{index}.log').write_text(log, encoding='utf-8')
                files = [p for p in out.rglob('*') if p.is_file()]
                require(run.returncode == 0 and files, f'hactool could not unpack DLC {index} '
                        f'(exit code {run.returncode}; see game/report/hactool-dlc{index}.log)')
                size = sum(p.stat().st_size for p in files)
                say(f'DLC {index}: {len(files)} files, {size / 1024:.0f} KiB in game/dlc/{index}')
                found += 1
        if not found:
            say('The DLC files hold no add-on content data (PublicData NCA).')
    except GameFileError as e:
        sys.exit(f'error: {e}')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
