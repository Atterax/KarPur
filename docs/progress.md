# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M3, erstes Lebenszeichen

### Nächste Schritte
1. Maintainer baut neu, startet `--boot` und schickt das Log-Ende und `build/game/uncovered-imports.txt`.
2. Für jeden Bereich der Liste entscheiden: PC-Ersatz schreiben (Dateisystem, Eingabe, Konten, Zeit …) oder als „offline/nicht vorhanden“ beantworten (Netzwerk, Bluetooth, LDN, BCAT …).
3. Absturz- und Fehlerstellen aus dem Log nacheinander beheben (fehlende/abweichende SDK-Ersatzfunktionen, Strukturlayouts von SDK 13, Dienste, die Purpur zusätzlich nutzt).
4. Ziel M3: `karpur.exe` startet, die Spiel-Threads laufen los, Dateien aus der RomFS werden gelesen, und ein Log zeigt, wie weit der Start kommt.

### Offene Fragen
- Gleiche Namen heißen nicht gleiche Strukturen: Bei SDK 13 vs. 20 können sich Strukturlayouts und Revisionen ändern (z. B. `NpadFullKeyState`, `AudioRendererParameter`-Revision). Jede übernommene Ersatzfunktion muss gegen SDK 13 geprüft werden.
- Spin-Wait-Patches: TotK leitet ~860 Warteschleifen über `rt_spin_gate` (Patch-Set `spin`, `tools/spin_waits.py`). Wir haben keine Patch-Datei; die asynchrone Präemption (`TOTK_ASYNC_PREEMPT`, Standard an) sollte Hänger abfangen, aber das Spiel kann dadurch langsamer sein. Bei Hängern beim Start zuerst hier ansetzen.
- `nn::aoc::ListAddOnContent` meldet noch keine DLC (TotK-Verhalten). Für Purpur später die vorhandene DLC melden.
- Interne Namen und Umgebungsvariablen heißen noch `TOTK_*` (z. B. `TOTK_GPU=null`, `TOTK_TRACE`). Umbenennen erst, wenn die Runtime läuft.
- Werden `nn::movie`-Funktionen überhaupt aufgerufen? Die Videos sind Bink 2 (`.bk2`), das der Spielcode selbst dekodiert. TotKs `ov_movie.c` (VP9 über libvpx) wird dann wohl nicht gebraucht.

## Protokoll

### 2026-11-01: Erster Start von karpur.exe
- Build beim Maintainer: 502 Objekte, `karpur.exe` 236 MiB, Link 0,7 s. 905 Warnungen aus dem generierten Code (harmlos; jetzt mit `-w` stumm).
- `--selftest`: bestanden. 3 Module (main `0x7000000000`, subsdk0 `0x7004800000`, sdk `0x7005000000`), 233.286 Funktionen im Dispatch.
- `--boot`: Vulkan findet die RTX 5080, alle drei Module laden (main 283.698 Relocations), `nn::init::Start` läuft, `nnosInitialize` und `nninitInitializeSdkModule` laufen durch, die RomFS wird erkannt (30 Dateien). Abbruch in `nninitStartup` (main): `nn::fs::SetAllocator` → SDK-intern `IsEnabledAccessLog` → `GetFileSystemProxyServiceObject` → `sm: service "fsp-srv" is not implemented`.
- Ursache: TotKs Ersatzfunktionen greifen nur an Import-Stellen (GOT); SDK-interne Aufrufe laufen im übersetzten Nintendo-Code und wollen echte Systemdienste.
- Behoben: `nn::fs::SetAllocator` hat einen PC-Ersatz (`fs_host.c`).
- Neu: Der Loader schreibt `uncovered-imports.txt` neben `karpur.exe`: alle Importe von main/subsdk0 aus Systembereichen des SDK ohne PC-Ersatz. Damit sehen wir alle Lücken auf einmal statt Absturz für Absturz.

### 2026-11-01: Runtime übernommen
- Komplette Runtime aus TotK Recomp nach `runtime/` übernommen (141 Dateien, ~59.000 Zeilen, MIT; Herkunft jeder Datei steht in ihrem Kopf, die vollständige Liste in `licenses/TOTK-RECOMP-THIRD_PARTY_NOTICES.md`).
- Angepasst: `boot.c` prüft keine TotK-Rücksprungadressen und init_array-Größen mehr, sondern loggt, wo die Checkpoints erreicht werden; Hauptthread-Priorität/Kern/Stack kommen aus `main.npdm` (`kern_set_main_params`); `fs_host.c` nimmt die Title-ID aus dem Build (`rt_application_id`); sichtbare Namen `karpur.exe`, `karpur.ini`, `karpur.log`, Fenstertitel „KarPur“; kein TotK-Icon.
- `tools/build_game.py` (aus TotK, angepasst): Standardpfade `generated/`, `game/extracted/update/exefs`, `game/romfs`, `build/game`; ExeFS-Hashes und Title-ID aus der eigenen Extraktion statt einer Tabelle; `rt_game_version = 0` (keine TotK-Version, alle TotK-Sonderfälle aus). `tools/romfs_index.py`, `tools/build_shaders.py` unverändert. `extract_romfs.py` schreibt jetzt `status: pass` in `index.json` (braucht `romfs_index.py`).
- Lokal unter Linux kompiliert die Runtime bis auf Umgebungsgrenzen (kein SDL3, älteres Clang 18 und Vulkan-Header 1.3.275); alle geänderten Dateien kompilieren. Gelinkt und gestartet wird erst beim Maintainer.
- Bekannt: `--probe-loader` und andere Probes vergleichen noch mit TotK-Messwerten und schlagen fehl; sie sind für Purpur nicht gedacht.

### 2026-11-01: RomFS entpackt
- hactool beim Maintainer gebaut (1d64a83 + 3 Patches), BKTR-Zusammenführung in 43 s, alle sechs IVFC-Ebenen GOOD.
- RomFS Purpur 4.0.0: **9,24 GiB, nur 30 Dateien**, sha256 `6258fef055415246…`.
  - `arc/`: 2 Dateien, 7,48 GiB: ein großes Archiv `data.trpfs` (7,47 GiB) mit Inhaltsverzeichnis `data.trpfd` (Game-Freak-Format, liest der Spielcode selbst).
  - `audio/`: 23 Dateien, 1,55 GiB: `.pck` und `.bnk` sind Audiokinetic-Wwise-Soundbanks. Wwise ist im Spielcode enthalten und wird mitübersetzt; Ausgabe über `nn::audio` (TotKs Audio-Renderer).
  - `demo/`: 2 Videos `.bk2` (Bink 2 von RAD, wird im Spielcode dekodiert).
  - `system/`, `event/`: kleine Dateien (`.trpre`, `.trevt`, `.trmdd`).
- Folge für die Runtime: Das Dateisystem muss vor allem schnelle wahlfreie Lesezugriffe in einer 7,5-GiB-Datei liefern; TotKs `romfs.c` liest direkt aus `romfs.bin`, das passt.

### 2026-11-01: RomFS-Extraktion vorbereitet
- Maintainer hat 100 GB frei (bei Bedarf 200–400 GB).
- `tools/build_hactool.py` und `tools/hactool/*.patch` aus TotK Recomp übernommen (baut hactool 1d64a83 mit MSYS2-gcc und `make`).
- `tools/extract_romfs.py`: kopiert Basis- und Update-Program-NCA nach `game/tmp`, übergibt Title-Key und bereinigte `prod.keys` über private Dateien, lässt hactool die BKTR-Zusammenführung mit Prüfung aller sechs IVFC-Ebenen machen, schreibt `game/romfs/romfs.bin` + `index.json` und berichtet Größen und Dateitypen. Der RomFS-Index ist mit `tests/test_romfs.py` getestet.
- Nicht hier getestet: der hactool-Build und die echte Zusammenführung. Das Herunterladen und Ausführen von fremdem Code wurde in der Agent-Umgebung blockiert; das läuft nur beim Maintainer.

### 2026-11-01: M2 erledigt
- Erster echter Lauf von `tools/lift.py` auf Purpur 4.0.0 (Windows, Clang 23.1.4):

  | Modul | Funktionen | Instruktionen | C | Sprungtabellen | Fallbacks | br über Dispatch | Traps | nicht unterstützt |
  |---|---|---|---|---|---|---|---|---|
  | main | 180.337 | 13.696.804 | 939 MB | 4.390 | 31 | 18.238 | 54.847 | 0 |
  | subsdk0 | 13.662 | 850.420 | 59 MB | 335 | 1 | 3.957 | 5.589 | 0 |
  | sdk | 39.287 | 1.529.240 | 106 MB | 661 | 16 | 10.327 | 24.661 | 0 |

- **0 nicht unterstützte Instruktionen, alle 404 Dateien kompilieren fehlerfrei.** Lifting 0,8 min, Kompilieren 3,5 min.
- „Traps“ sind keine Fehler: `brk`-Instruktionen (Abbruchpfade nach `abort`/Assertions) und Füllwörter zwischen Funktionen, die nur bei einem echten Absturz erreicht würden.
- „br über Dispatch“: berechnete Sprünge ohne erkannte Tabelle (meist Tail-Calls über Funktionszeiger) laufen über `rt_dispatch`. Das ist korrekt, nur etwas langsamer.

### 2026-11-01: Lifter übernommen und getestet
- MSYS2 beim Maintainer installiert: Clang 23.1.4, LLD, Ninja (UCRT64).
- Lifter aus TotK Recomp unverändert übernommen (`recomp/`: lifter, simd_c, nso, module, layout, jumptables, flags, regs, gen, patches; `runtime/cpu.h`, `simd.h`, `simd_fp.h`). Ursprung: LAS Recomp und TotK Recomp (MIT), siehe `THIRD_PARTY_NOTICES.md`.
- Neuer Differenztest `tests/lifter/`: Ein C-Testprogramm (Arithmetik, Division, Bitoperationen, Schleifen, Rekursion, Sprungtabelle, Funktionszeiger mit RELATIVE-Relocations, double/float, Sonderwerte wie NaN/Inf/Denormal, NEON-Vektorschleifen, Atomics mit LDXR/STXR, 128-Bit-Multiplikation, Sortieren) wird für AArch64 (ARMv8.0 wie der A57 der Switch) gebaut, als NSO verpackt, geliftet, auf x86-64 ausgeführt und mit dem nativ kompilierten Original verglichen: 16 Tests × 400 Eingaben, 0 Abweichungen. Läuft in der CI.
- Erkenntnis: Clang fasst `a*b+c` auf AArch64 zu FMADD zusammen (eine Rundung), auf x86 ohne FMA nicht. Der Lifter bildet FMADD korrekt nach; der Test baut beide Seiten mit `-ffp-contract=off`.
- `tools/lift.py`: lässt `recomp/gen.py` auf `main`, `subsdk0`, `sdk` laufen, kompiliert alles mit Clang über Ninja und gibt einen Bericht aus (Compilermeldungen ohne Quelltextzeilen).

### 2026-11-01: TotK-Vergleich, M1 erledigt
- **Alle 210 SDK-Symbole, die die TotK-Runtime behandelt, gibt es in Purpur unter demselben Namen.** Das Ersatzkonzept (Austausch auf `nn::*`-Funktionsebene) lässt sich direkt übertragen.
- Bereiche, die TotK nie anfasst: reine Bibliotheken, die einfach mitübersetzt werden (`nn::util` 44, `nn::fontll` 24, `nn::mem` 13, `nn::crypto` 12, `nn::lmem` 6, `nn::result`, `nn::detail`) und Online-/Systemfunktionen, die wir als „offline“ beantworten (`nn::bluetooth` 34, `nn::socket` 29, `nn::ldn` 27, `nn::bcat` 22, `nn::nifm` 8, `nn::ssl` 8, `nn::web` 8, `nn::settings` 3, `nn::ngc` 3, `nn::album` 3, `nn::nsd` 2, `nn::ec` 1).

### 2026-11-01: Erster M1-Bericht (Purpur, Update 4.0.0)
- Basis: XCI 6,63 GiB, Program-NCA keygen 14. Update 4.0.0: NSP 4,88 GiB, keygen 17, BKTR. ExeFS beider entpackt, alle Hashes OK.
- `main` des Updates: sha256 `f05bd7fedaf6f0ba…`, Build-ID `709BFD66115298640155FCC4979DBA151C7CC79A`.
- Module (Update): `main` 52,25 MB Code + 15,26 MB rodata + 3,41 MB data; `sdk` 5,83 MB Code (27.157 Exporte); `subsdk0` 3,24 MB Code (10.882 Exporte, importiert 659); `rtld`.
- **SDK 13.3.13** (TotK 1.4.3: SDK 20.5.22, TotK 1.0–1.2.1: älter). Andere SDK-Version heißt: Ersatzfunktionen müssen pro Name geprüft werden.
- `main` importiert 1013 Symbole, alle werden von `sdk`/`subsdk0` bereitgestellt (0 offen). NVN nur über `nvnBootstrapLoader` wie bei TotK, also passt das NVN-Frontend-Konzept.
- Middleware: Havok AI 2021.1, NintendoWare (G3d, Ui2d, Vfx, Font), NVN/gfx, movie, libcurl, libz, nlib LZ4, Online-Stack (Pia 6.20.1, NPLN 1.8.3, gRPC, OpenSSL). Kein Havok Physics im Bericht.
- Größte Import-Bereiche: `nn::os` 106, `std::__1` 95, `nn::hid` 56, `nn::util` 44, `nn::audio` 42, `nn::bluetooth` 34, `nn::fs` 31, `nn::socket` 29, `nn::ldn` 27, `nn::account` 25, `nn::bcat` 22.
- DLC-NSP enthält nur eine 0,1-MiB-PublicData-NCA (`01008F6008C5F001`): Bei Karmesin/Purpur stecken die DLC-Inhalte im Update, die DLC-Datei schaltet sie nur frei. Für uns heißt das: `nn::aoc` muss die DLC als vorhanden melden.
- Neues Werkzeug `tools/compare_totk.py`: vergleicht die Symbole des Spiels mit den ~210 SDK-Funktionen, die die TotK-Runtime behandelt.

### 2026-11-01: M1-Werkzeug geschrieben
- `tools/extract.py` (+ `tools/switchfs.py`, `tools/nso.py`): entpackt die ExeFS von Basis (XCI) und Update (NSP) nach `game/extracted/`, inventarisiert die DLC und gibt einen Bericht aus. Abhängigkeiten: `cryptography`, `lz4` in `.venv`.
- Getestet mit einem künstlichen Spiel mit Zufallsschlüsseln (`tests/make_fake_game.py`, `tests/test_extract.py`, läuft auch in der CI).
- Spiel des Maintainers: Pokémon Purpur (Violet), Basis `01008F6008C5E000` als XCI, Update 4.0.0 als NSP, eine DLC-NSP.
- RomFS wird noch nicht entpackt: Das kommt erst, wenn wir sie brauchen (M3).

### 2026-11-01: Setup begonnen
- README auf Englisch umgestellt, mit Hinweis auf die KI-Mitarbeit.
- Maintainer hat Git 2.51.0 und Python 3.14.7. Festgelegt: Python 3.14+ (wegen `compression.zstd`).

### 2026-11-01: M0 erledigt
- Repo-Grundgerüst angelegt: README, Lizenz, Projektregeln (`CLAUDE.md`), `.gitignore`, Inhaltsprüfung (`tools/check_content.py`) als Pre-Commit-Hook und in der CI, Fahrplan.
- Hardware des Maintainers: Windows, Ryzen 7 9800X3D, RTX 5080, 96 GB RAM. Spiel und Keys von der eigenen Konsole sind vorhanden (neuestes Update).
