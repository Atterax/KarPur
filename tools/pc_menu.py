"""Write game/ui_patch.txt, the list that makes the PC settings entry and page appear in Violet 4.0.0's main menu.

The list holds only numbers and KarPur's own texts, but it is keyed to the player's game files, so it stays under game/
and every player makes their own. It sets (docs/progress.md 2026-11-03/04):
  - the seventh main menu button (the downloadable-content one) as a normal list button with the gear icon, in the row
    under Options (tools/uilayout.py: its part's layout, the default icon, its place, the longer box, Save one row lower)
  - the cursor order: that row before Save (the grid rows of a small file of the same archive)
  - the texts of the entry and the page in every language (tools/pc_texts.py)

Without the list the game starts as it ships: no PC settings entry, and the F1 menu whole (runtime/lua_mod.c).

  .venv\\Scripts\\python tools\\pc_menu.py

It reads files the game unpacked into game/dump: the main menu's archive, its label tables. When they are missing it
says how to get them. An older list is kept as game/ui_patch.txt.bak. Prints names and counts only.
"""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import pc_texts  # noqa: E402
import uilayout  # noqa: E402

OUT = ROOT / 'game' / 'ui_patch.txt'
DUMP = ROOT / 'game' / 'dump'
WATCH = ROOT / 'game' / 'data_watch.txt'
WATCH_REGEX = r'messagedat[A-Za-z_]+common(xmenu|option)\.tbl|state_xmenu'
ARCHIVE_SIZE, GRID_SIZE = 288704, 1480          # the two files of state_xmenu.truis used here, in 4.0.0
# The key values of xmenu_top_00_pattern_button.bflan at six visible buttons (uilayout.py --anim): offset, 4.0.0's value,
# ours: Save one row lower, the box one row longer.
KEYS = [(0x465d0, -80.0, -160.0), (0x46720, 570.0, 650.0)]


def dumped(pattern, size=None):
    for p in sorted(DUMP.glob(pattern)):
        if size is None or p.stat().st_size == size:
            return p
    return None


def main():
    if 'messagedatTrad_Chinesecommonoption' not in (WATCH.read_text() if WATCH.is_file() else ''):
        print('game/data_watch.txt lacks the message files: listing them (takes a moment)')
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'trpfs.py'), '--watch', WATCH_REGEX], check=True,
                       stdout=subprocess.DEVNULL)
    archive = dumped('applidataxmenustate_xmenu.truis__*', ARCHIVE_SIZE)
    grid = dumped('applidataxmenustate_xmenu.truis__*', GRID_SIZE)
    tables = [dumped(f'messagedat*common{n}.tbl__*') for n in ('xmenu', 'option')]
    if not archive or not grid or not all(tables):
        sys.exit('error: game/dump lacks the unpacked menu files. Start the game once with\n'
                 '  .venv\\Scripts\\python tools\\run_with.py DATA_WATCH=1\n'
                 'open the main menu (X) and Options, close the game, and run this tool again.')
    data = archive.read_bytes()
    for at, want, _ in KEYS:
        if abs(struct.unpack_from('<f', data, at)[0] - want) > 0.01:
            sys.exit(f'error: {archive.name} differs from Violet 4.0.0 at {at:#x}: nothing written')
    if OUT.is_file():
        OUT.replace(OUT.with_name(OUT.name + '.bak'))
    OUT.write_text('# made by tools/pc_menu.py; UI bytes and texts set by runtime/data_patch.c\n', encoding='utf-8')
    uilayout.swap_part(archive, 'L_xbutton_06=xmenu_button_00', OUT)
    uilayout.rename_texture(archive, 'xmenu_button_00:xmenu_button_icon_01^s=xmenu_button_icon_05^s', OUT)
    uilayout.set_keys(archive, [f'{at:x}={ours}' for at, _, ours in KEYS], OUT)
    uilayout.set_fields(archive, 'N_dlc_00:y=-80', OUT)
    uilayout.set_fields(archive, 'L_xbutton_06:w=510,h=80', OUT)
    uilayout.hide(archive, 'P_base_01', OUT)
    uilayout.set_grid_rows(grid, ['dlc=5', 'report=6'], OUT)
    pc_texts.main()
    print(f'done: {OUT}')


if __name__ == '__main__':
    main()
