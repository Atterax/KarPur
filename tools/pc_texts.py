"""Write the texts of the PC settings entry and page as msgtext lines (game/ui_patch.txt) for runtime/data_patch.c.

The PC settings page is the game's own options page with another list of entries (runtime/lua/menu_mod.lua,
docs/progress.md 2026-11-04). The page fetches every text by label from the message file "option", so the PC texts take
labels of that file no script of the game names. Which label holds which text is set once, in the PC_LABELS block of
menu_mod.lua; this tool reads it, takes the text of each id from TEXTS below and writes one msgtext line per label and
language: the runtime puts the text in its own memory and points the label's line there (any length). The main menu's
entry is the label xmenu_dlc_button of the message file "xmenu".

A language is identified by its files in game/data_watch.txt (tools/trpfs.py --watch
"messagedat[A-Za-z_]+common(xmenu|option)\\.tbl|state_xmenu"). Each pack holds a label table and the texts: the table is
the file whose key equals that of the table dumped in game/dump, and a language with another table is left out, since
its line numbers are unknown. A language without a text of its own gets the English one. JPN and JPN_KANJI share their
files, which get one set of lines. The texts are KarPur's own.

  .venv\\Scripts\\python tools\\pc_texts.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

from msgpatch import labels_of  # noqa: E402

LANGUAGES = ['German', 'English', 'French', 'Italian', 'Spanish', 'JPN', 'JPN_KANJI', 'Korean', 'Simp_Chinese',
             'Trad_Chinese']
FALLBACK = 'English'


def t(de, en, fr, it, es, ja, ko, zh, tw):
    return {'German': de, 'English': en, 'French': fr, 'Italian': it, 'Spanish': es, 'JPN': ja, 'JPN_KANJI': ja,
            'Korean': ko, 'Simp_Chinese': zh, 'Trad_Chinese': tw}


def same(text):
    return dict.fromkeys(LANGUAGES, text)


def experimental(text):
    """A frame rate above 60: walking indoors follows it, the effects do not (docs/progress.md 2026-11-04)."""
    return t(f'{text} (experimentell)', f'{text} (experimental)', f'{text} (expérimental)', f'{text} (sperimentale)',
             f'{text} (experimental)', f'{text}(実験的)', f'{text}(실험적)', f'{text}(实验性)', f'{text}(實驗性)')


# id: the text per language. "\\n" is a line break.
ENTRY = t('PC-Einstellungen', 'PC Settings', 'Options PC', 'Opzioni PC', 'Ajustes de PC', 'PC設定', 'PC 설정', 'PC设置',
          'PC設定')
TEXTS = {
    'title': t('PC-EINSTELLUNGEN', 'PC SETTINGS', 'OPTIONS PC', 'OPZIONI PC', 'AJUSTES DE PC', 'PC設定', 'PC 설정', 'PC设置',
               'PC設定'),
    'fullscreen': t('Vollbild', 'Fullscreen', 'Plein écran', 'Schermo intero', 'Pantalla completa', 'フルスクリーン', '전체 화면',
                    '全屏', '全螢幕'),
    'fullscreen_info': t('Zeigt das Spiel im Vollbild oder in einem Fenster.', 'Shows the game in fullscreen or in a window.',
                         'Affiche le jeu en plein écran ou dans une fenêtre.',
                         'Mostra il gioco a schermo intero o in una finestra.',
                         'Muestra el juego a pantalla completa o en una ventana.',
                         'ゲームをフルスクリーンまたは\\nウィンドウで表示します。', '게임을 전체 화면 또는 창으로 표시합니다.',
                         '以全屏或窗口模式显示游戏。', '以全螢幕或視窗模式顯示遊戲。'),
    'window': t('Fenstergröße', 'Window Size', 'Taille de la fenêtre', 'Dimensioni finestra', 'Tamaño de la ventana', 'ウィンドウサイズ',
                '창 크기', '窗口大小', '視窗大小'),
    'window_info': t('Wirkt nur außerhalb des Vollbilds.\\nDas Spielbild selbst bleibt bei 1920 x 1080.',
                     'Only applies outside fullscreen.\\nThe game image itself stays at 1920 x 1080.',
                     "S'applique uniquement hors plein écran.\\nL'image du jeu reste en 1920 x 1080.",
                     "Vale solo fuori dallo schermo intero.\\nL'immagine del gioco resta a 1920 x 1080.",
                     'Solo se aplica fuera de pantalla completa.\\nLa imagen del juego sigue en 1920 x 1080.',
                     'フルスクリーン以外で有効です。\\nゲームの画像は1920 x 1080のままです。',
                     '전체 화면이 아닐 때만 적용됩니다.\\n게임 화면 자체는 1920 x 1080입니다.',
                     '仅在非全屏时生效。\\n游戏画面本身仍为1920 x 1080。', '僅在非全螢幕時生效。\\n遊戲畫面本身仍為1920 x 1080。'),
    'window_720': same('1280 x 720'),
    'window_1080': same('1920 x 1080'),
    'window_1440': same('2560 x 1440'),
    'window_2160': same('3840 x 2160'),
    'fps': t('FPS-Limit', 'Frame Rate Limit', "Limite d'images/s", 'Limite FPS', 'Límite de FPS', 'FPS上限', 'FPS 제한',
             '帧率上限', '影格率上限'),
    'fps_info': t('Die höchste Bildrate. Über 60 FPS experimentell: Effekte\\nkönnen ruckeln. Nach einer Änderung neu starten.',
                  'The highest frame rate. Above 60 FPS is experimental:\\neffects may stutter. Restart after a change.',
                  'Le nombre maximal d\'images par seconde. Au-delà de 60 :\\nexpérimental, effets saccadés possibles. Relancez après un changement.',
                  'La frequenza massima. Oltre 60 FPS è sperimentale:\\ngli effetti possono scattare. Riavvia dopo una modifica.',
                  'La tasa máxima de imágenes. Más de 60 FPS es experimental:\\nlos efectos pueden ir a tirones. Reinicia tras un cambio.',
                  'フレームレートの上限です。60 FPSを超える設定は実験的で、\\nエフェクトがカクつくことがあります。変更後は再起動してください。',
                  '최대 프레임 수입니다. 60 FPS 초과는 실험적이며\\n효과가 끊길 수 있습니다. 변경 후 다시 시작하세요.',
                  '最高帧率。超过60 FPS为实验性功能,\\n特效可能卡顿。更改后请重新启动。',
                  '最高影格率。超過60 FPS為實驗性功能,\\n特效可能卡頓。更改後請重新啟動。'),
    'fps_30': same('30 FPS'),
    'fps_60': same('60 FPS'),
    'fps_90': experimental('90 FPS'),
    'fps_120': experimental('120 FPS'),
    'fps_144': experimental('144 FPS'),
    'fps_display': t('Bildschirm (experimentell)', 'Display (experimental)', 'Écran (expérimental)', 'Schermo (sperimentale)',
                     'Pantalla (experimental)', 'ディスプレイ(実験的)', '디스플레이(실험적)', '显示器(实验性)', '顯示器(實驗性)'),
    'sync': t('Bildsynchronisation', 'Frame Sync', "Synchro d'image", 'Sincronia immagine', 'Sincronización', '画面同期', '화면 동기화',
              '画面同步', '畫面同步'),
    'sync_info': t('VSync verhindert Bildrisse. Mailbox reagiert schneller,\\nAus hat die geringste Verzögerung.',
                   'VSync prevents tearing. Mailbox responds faster,\\nOff has the lowest latency.',
                   "VSync évite les déchirures. Mailbox réagit plus vite,\\nsans synchro, le délai est minimal.",
                   'VSync evita gli strappi. Mailbox risponde più in fretta,\\nsenza sincronia il ritardo è minimo.',
                   'VSync evita los cortes. Mailbox responde más rápido,\\nsin sincronía el retraso es mínimo.',
                   'VSyncは画面のズレを防ぎます。\\nMailboxはより速く反応し、\\nオフは遅延が最小です。',
                   'VSync는 화면 찢어짐을 막습니다.\\nMailbox는 더 빠르게 반응하고\\n끄면 지연이 가장 적습니다.',
                   'VSync可防止画面撕裂。\\nMailbox响应更快,\\n关闭时延迟最低。',
                   'VSync可防止畫面撕裂。\\nMailbox反應更快,\\n關閉時延遲最低。'),
    'sync_mailbox': same('Mailbox'),
    'sync_vsync': same('VSync'),
    'upscaler': t('Hochskalierung', 'Upscaling', "Mise à l'échelle", 'Upscaling', 'Escalado', 'アップスケール', '업스케일링', '画面放大',
                  '畫面放大'),
    'upscaler_info': t('Wie das Bild auf die Fenstergröße kommt:\\nAMD FSR 1 ist schärfer, Bilinear weicher.',
                       'How the image is scaled to the window:\\nAMD FSR 1 is sharper, Bilinear is softer.',
                       "Comment l'image s'adapte à la fenêtre :\\nAMD FSR 1 est plus net, Bilinear plus doux.",
                       "Come l'immagine si adatta alla finestra:\\nAMD FSR 1 è più nitido, Bilinear più morbido.",
                       'Cómo se adapta la imagen a la ventana:\\nAMD FSR 1 es más nítido, Bilinear más suave.',
                       '画像をウィンドウに合わせる方法です。\\nAMD FSR 1はシャープ、Bilinearは柔らかめです。',
                       '화면을 창 크기에 맞추는 방식입니다.\\nAMD FSR 1은 선명하고 Bilinear는 부드럽습니다.',
                       '画面适应窗口的方式:\\nAMD FSR 1更清晰,Bilinear更柔和。',
                       '畫面適應視窗的方式:\\nAMD FSR 1更清晰,Bilinear更柔和。'),
    'upscaler_fsr': same('AMD FSR 1'),
    'upscaler_bilinear': same('Bilinear'),
    'sharpness': t('Schärfe', 'Sharpness', 'Netteté', 'Nitidezza', 'Nitidez', 'シャープネス', '선명도', '锐度', '銳利度'),
    'sharpness_info': t('Wie stark AMD FSR 1 nachschärft. Wirkt nur, wenn\\ndas Fenster größer ist als 1920 x 1080.',
                        'How much AMD FSR 1 sharpens. Only applies when\\nthe window is larger than 1920 x 1080.',
                        "L'intensité de netteté d'AMD FSR 1. Seulement si\\nla fenêtre dépasse 1920 x 1080.",
                        'Quanto AMD FSR 1 aumenta la nitidezza. Solo se\\nla finestra supera 1920 x 1080.',
                        'Cuánto enfoca AMD FSR 1. Solo si la ventana\\nsupera 1920 x 1080.',
                        'AMD FSR 1のシャープさの強さです。\\nウィンドウが1920 x 1080より大きいときのみ有効です。',
                        'AMD FSR 1의 선명하게 하는 정도입니다.\\n창이 1920 x 1080보다 클 때만 적용됩니다.',
                        'AMD FSR 1的锐化强度。\\n仅在窗口大于1920 x 1080时生效。', 'AMD FSR 1的銳化強度。\\n僅在視窗大於1920 x 1080時生效。'),
    'perf': t('Leistungsanzeige', 'Performance Overlay', 'Affichage des performances', 'Dati prestazioni', 'Datos de rendimiento',
              'パフォーマンス表示', '성능 표시', '性能显示', '效能顯示'),
    'perf_info': t('Zeigt die Bildrate oben im Bild an.', 'Shows the frame rate at the top of the screen.',
                   "Affiche le nombre d'images par seconde en haut de l'écran.", 'Mostra i FPS nella parte alta dello schermo.',
                   'Muestra los FPS en la parte superior de la pantalla.', '画面上部にフレームレートを表示します。',
                   '화면 위쪽에 프레임 수를 표시합니다.', '在画面上方显示帧率。', '在畫面上方顯示影格率。'),
    'perf_fps': same('FPS'),
    'perf_full': t('Ausführlich', 'Detailed', 'Détaillé', 'Dettagliato', 'Detallado', '詳細', '자세히', '详细', '詳細'),
    'population': t('Pokémon-Anzahl', 'Pokémon Population', 'Nombre de Pokémon', 'Numero di Pokémon', 'Cantidad de Pokémon', 'ポケモンの数', '포켓몬 수',
                    '宝可梦数量', '寶可夢數量'),
    'population_info': t('Wie viele wilde Pokémon gleichzeitig unterwegs sind: 15, 40,\\n80 oder 120. Hohe Stufen können Leistung kosten.\\nWirkt erst nach einem Neustart.',
                         'How many wild Pokémon roam at once: 15, 40, 80 or 120.\\nHigher levels can cost performance.\\nTakes effect after a restart.',
                         'Nombre de Pokémon sauvages en même temps : 15, 40, 80 ou 120.\\nLes niveaux élevés peuvent réduire les performances.\\nPrend effet après un redémarrage.',
                         'Quanti Pokémon selvatici insieme: 15, 40, 80 o 120.\\nI livelli alti possono ridurre le prestazioni.\\nHa effetto dopo il riavvio.',
                         'Cuántos Pokémon salvajes a la vez: 15, 40, 80 o 120.\\nLos niveles altos pueden reducir el rendimiento.\\nSe aplica tras reiniciar.',
                         '同時に現れる野生のポケモンの数です(15、40、80、120)。\\n高い設定では動作が重くなることがあります。\\n再起動後に反映されます。',
                         '동시에 나타나는 야생 포켓몬 수입니다: 15, 40, 80, 120.\\n높은 단계는 성능이 떨어질 수 있습니다.\\n다시 시작한 후 적용됩니다.',
                         '同时出现的野生宝可梦数量:15、40、80或120。\\n高等级可能影响性能。\\n重新启动后生效。',
                         '同時出現的野生寶可夢數量:15、40、80或120。\\n高等級可能影響效能。\\n重新啟動後生效。'),
    'population_switch': same('Switch'),
    'population_switch2': same('Switch 2'),
    'population_enhanced': same('Enhanced'),
    'population_extreme': same('Extreme'),
    'keymap': t('Steuerung', 'Controls', 'Commandes', 'Comandi', 'Controles', '操作設定', '조작 설정', '操作设置', '操作設定'),
    'keymap_info': t('Links oder rechts drücken: Öffnet Tastenbelegung,\\nController, Vibration und Maus.',
                     'Press left or right to open key mapping,\\ncontroller, rumble and mouse settings.',
                     'Gauche ou droite : ouvre les touches, la manette,\\nles vibrations et la souris.',
                     'Sinistra o destra: apre tasti, controller,\\nvibrazione e mouse.',
                     'Izquierda o derecha: abre teclas, mando,\\nvibración y ratón.',
                     '左右を押すとキー設定、コントローラー、\\n振動、マウスの設定を開きます。',
                     '왼쪽이나 오른쪽을 누르면 키 설정,\\n컨트롤러, 진동, 마우스 설정이 열립니다.',
                     '按左或右打开按键、手柄、\\n振动和鼠标设置。', '按左或右開啟按鍵、控制器、\\n震動和滑鼠設定。'),
    'keymap_open': t('Öffnen', 'Open', 'Ouvrir', 'Apri', 'Abrir', '開く', '열기', '打开', '開啟'),
}


def lua_labels(path):
    """id -> label from the PC_LABELS block of menu_mod.lua (lines `  id = "label",`)."""
    text = path.read_text(encoding='utf-8')
    m = re.search(r'local PC_LABELS = \{(.*?)\n\}', text, re.S)
    if not m:
        sys.exit(f'error: no PC_LABELS block in {path}')
    return dict(re.findall(r'^\s*(\w+)\s*=\s*"(\w+)"', m.group(1), re.M))


def dumped_table(name):
    """The label table of message file `name` in game/dump: (its data, its name), or an error."""
    for p in sorted((ROOT / 'game' / 'dump').glob(f'messagedat*common{name}.tbl__*')):
        data = p.read_bytes()
        if data[:4] == b'AHTB':
            return data, p.name
    sys.exit(f'error: no dumped {name}.tbl in game/dump (tools/trpfs.py --watch for its pack, then the game with '
             'DATA_WATCH=1)')


def split_pack(rows, lang, name, table_name):
    """(key of the texts) of a language's pack, or None when it is missing or its label table is another one."""
    table_key = next((r[:3] for r in rows if r[3] == table_name), None)
    pack = [r for r in rows if r[3].startswith(f'messagedat{lang}common{name}.tbl__')]
    tables = [r for r in pack if r[:3] == table_key]
    texts = [r for r in pack if r[:3] != table_key]
    return texts[0][:3] if len(tables) == 1 and len(texts) == 1 else None


def main():
    labels = lua_labels(ROOT / 'runtime' / 'lua' / 'menu_mod.lua')
    missing = sorted(set(labels) - set(TEXTS))
    if missing:
        sys.exit(f'error: menu_mod.lua names ids without a text here: {", ".join(missing)}')
    option_tbl, option_name = dumped_table('option')
    xmenu_tbl, xmenu_name = dumped_table('xmenu')
    option_lines, xmenu_lines = labels_of(option_tbl), labels_of(xmenu_tbl)
    for label in labels.values():
        if label not in option_lines:
            sys.exit(f'error: no label {label} in {option_name}')
    watch = ROOT / 'game' / 'data_watch.txt'
    rows = [r for r in (l.split() for l in watch.read_text().splitlines() if not l.startswith('#')) if len(r) == 4]
    out = ROOT / 'game' / 'ui_patch.txt'
    old = out.read_text(encoding='utf-8').splitlines() if out.is_file() else []
    # the lines of earlier runs, and the entry's earlier in-place text (tools/msgpatch.py)
    keep = [l for l in old if not (l.startswith('msgtext ') and l.split(' ', 6)[5].startswith('pc:'))
            and ' msg:xmenu_dlc_button:' not in l]
    lines, seen = [], set()
    for lang in LANGUAGES:
        option_key = split_pack(rows, lang, 'option', option_name)
        xmenu_key = split_pack(rows, lang, 'xmenu', xmenu_name)
        if not option_key or not xmenu_key:
            print(f'{lang}: its files are not in {watch.name} or have another label table: skipped')
            continue
        if tuple(option_key) in seen:
            print(f'{lang}: the same files as another language')
            continue
        seen.add(tuple(option_key))
        for ident, label in labels.items():
            text = TEXTS[ident].get(lang) or TEXTS[ident][FALLBACK]
            lines.append(f'msgtext {" ".join(option_key)} {option_lines.index(label)} pc:{ident}:{lang} {text}')
        lines.append(f'msgtext {" ".join(xmenu_key)} {xmenu_lines.index("xmenu_dlc_button")} pc:entry:{lang} {ENTRY[lang]}')
        print(f'{lang}: {len(labels) + 1} texts')
    out.write_text('\n'.join(keep + lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} msgtext lines in {out}')


if __name__ == '__main__':
    main()
