# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M1, Werkzeuge & Extraktion

### Nächste Schritte
1. Maintainer richtet die Grundwerkzeuge unter Windows ein ([setup-windows.md](setup-windows.md)).
2. Extraktions- und Analyseskript `tools/extract.py` schreiben (Basis + Update zusammenführen, ExeFS/RomFS entpacken, Bericht ausgeben).
3. Bericht des Maintainers auswerten: SDK-Version, Modulgrößen, Vergleich mit TotK.

### Offene Fragen
- Welche Spielversion genau (Build-ID von `main`)? Kommt aus dem M1-Bericht.
- Wie viel der TotK-Runtime passt zur SDK-Version von Karmesin/Purpur?

## Protokoll

### 2026-11-01: Setup begonnen
- README auf Englisch umgestellt, mit Hinweis auf die KI-Mitarbeit.
- Maintainer hat Git 2.51.0 und Python 3.14.7. Festgelegt: Python 3.14+ (wegen `compression.zstd`).

### 2026-11-01: M0 erledigt
- Repo-Grundgerüst angelegt: README, Lizenz, Projektregeln (`CLAUDE.md`), `.gitignore`, Inhaltsprüfung (`tools/check_content.py`) als Pre-Commit-Hook und in der CI, Fahrplan.
- Hardware des Maintainers: Windows, Ryzen 7 9800X3D, RTX 5080, 96 GB RAM. Spiel und Keys von der eigenen Konsole sind vorhanden (neuestes Update).
