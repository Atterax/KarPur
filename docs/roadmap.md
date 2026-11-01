# Fahrplan

Jeder Meilenstein hat ein klares, prüfbares Ziel. Ein Meilenstein gilt erst als erledigt, wenn er auf dem PC des Maintainers mit dem echten Spiel geprüft wurde.

| | Ziel | Fertig, wenn … |
|---|---|---|
| **M0** | Repo-Grundgerüst | Regeln, Lizenzen, Schutz vor Spieldateien und CI stehen |
| **M1** | Werkzeuge & Extraktion | Die Toolchain läuft unter Windows, und ein Skript entpackt den eigenen Dump (Basis + Update) nach `game/` und gibt einen Bericht aus: Spielversion, Hashes, Module, SDK-Version |
| **M2** | Lifter läuft | Der aus `main`, `sdk` und `subsdk*` erzeugte C-Code kompiliert vollständig; ein Bericht listet nicht unterstützte Instruktionen (Ziel: 0) |
| **M3** | Erstes Lebenszeichen | `karpur.exe` startet den Spielcode, liest Dateien aus der RomFS und schreibt Logs, noch ohne Bild |
| **M4** | Erstes Bild | Der Titelbildschirm wird über Vulkan gerendert |
| **M5** | Spielbar | Herumlaufen, Kämpfe, Speichern und Laden funktionieren |
| **M6** | PC-Extras | Höhere Auflösung, höhere Framerate, Einstellungsmenü, Installer |

## Arbeitsweise

- Spieldateien sieht nur der Maintainer. Werkzeuge laufen auf seinem PC und erzeugen **Berichte** (Zahlen, Adressen, Funktionsnamen, Fehlermeldungen), die geteilt werden können.
- Keine Bytes aus dem Spiel, keine Keys und kein erzeugter Code gehören in Berichte, Issues, Commits oder den Chat.
