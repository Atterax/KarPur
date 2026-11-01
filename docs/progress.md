# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M1, Werkzeuge & Extraktion

### Nächste Schritte
1. Maintainer führt Schritt 6 und 7 aus [setup-windows.md](setup-windows.md) aus und schickt den M1-Bericht.
2. Bericht auswerten: SDK-Version, Modulgrößen, Middleware, welche `nn::*`-Funktionen `main` importiert; mit der TotK-Runtime vergleichen.
3. Danach: RomFS-Extraktion (Basis + BKTR-Update zusammenführen, wie bei TotK mit gepatchtem hactool) und Toolchain für M2 (MSYS2/Clang).

### Offene Fragen
- Welche Spielversion genau (Build-ID von `main`)? Kommt aus dem M1-Bericht.
- Wie viel der TotK-Runtime passt zur SDK-Version von Karmesin/Purpur?

## Protokoll

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
