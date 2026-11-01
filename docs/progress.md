# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M2, Lifter läuft

### Nächste Schritte
1. Maintainer installiert MSYS2 mit Clang, LLD und Ninja ([setup-windows.md](setup-windows.md), Schritt 8).
2. Lifter von TotK Recomp (`recomp/`, MIT) nach `recomp/` übernehmen: NSO-Laden, ARM64-Decoder, NEON-Lowering, Sprungtabellen, Generator. TotK-Spezifisches (Patches, Versionstabellen) weglassen.
3. Werkzeug, das den Lifter auf `main`, `sdk`, `subsdk0` laufen lässt und berichtet: Funktionen, nicht unterstützte Instruktionen, Sprungtabellen; danach Kompiliertest mit Clang.

### Offene Fragen
- Gleiche Namen heißen nicht gleiche Strukturen: Bei SDK 13 vs. 20 können sich Strukturlayouts und Revisionen ändern (z. B. `NpadFullKeyState`, `AudioRendererParameter`-Revision). Jede übernommene Ersatzfunktion muss gegen SDK 13 geprüft werden.

## Protokoll

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
