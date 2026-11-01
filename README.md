# KarPur

**KarPur** ist ein Fanprojekt mit dem Ziel, *Pokémon Karmesin & Purpur* (Nintendo Switch, 2022) per **statischer Recompilation** in ein natives Windows-Programm (x86-64, Vulkan) zu übersetzen: kein Emulator, sondern echter PC-Code.

*KarPur is a fan project aiming to statically recompile Pokémon Scarlet & Violet (Nintendo Switch) into a native Windows x86-64 application with a Vulkan renderer.*

> [!IMPORTANT]
> **Dieses Repository enthält keine Spieldateien und wird nie welche enthalten.** Du brauchst eine eigene, legal erworbene Kopie des Spiels und die Keys deiner eigenen Konsole. Wie man diese beschafft, wird hier nicht erklärt.
>
> Alles, was aus dem Spiel erzeugt wird, entsteht auf deinem eigenen Rechner und ist nur für dich. Teile es nicht.

## Status

**Ganz am Anfang (Meilenstein M0).** Es gibt noch nichts zum Spielen. Den Fahrplan findest du in [docs/roadmap.md](docs/roadmap.md), den aktuellen Stand in [docs/progress.md](docs/progress.md).

## Wie es funktionieren soll

1. Die ARM64-Programmdateien des Spiels werden automatisch in C-Code übersetzt („Lifting“).
2. Eine eigene Laufzeitumgebung ersetzt das Switch-Betriebssystem (Threads, Dateien, Eingabe, Audio).
3. Die Grafikaufrufe der Switch (NVN) werden auf Vulkan umgesetzt, die Shader mit dem Shader-Übersetzer von Ryujinx.
4. Das Ganze wird mit Clang zu einer `.exe` kompiliert, und zwar auf dem Rechner des Spielers.

Geplante Grundlage sind die MIT-lizenzierten Projekte [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) und [LAS Recomp](https://github.com/peiikai/LASRecomp). Übernommener Code wird in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) aufgeführt.

## Mitmachen

Lies zuerst [CLAUDE.md](CLAUDE.md) (Projektregeln). Vor jedem Commit:

```
python tools/check_content.py --staged
```

Einmalig pro Klon den Schutz-Hook aktivieren:

```
git config core.hooksPath .githooks
```

## Lizenz

Der eigene Code des Projekts steht unter der [MIT-Lizenz](LICENSE). Fremder Code behält seine eigene Lizenz. Die Lizenz gewährt keinerlei Rechte am Spiel.

KarPur ist ein unabhängiges, nicht-kommerzielles Fanprojekt und steht in keiner Verbindung zu Nintendo, The Pokémon Company, Game Freak oder Creatures Inc. *Pokémon*, *Pokémon Karmesin*, *Pokémon Purpur* und *Nintendo Switch* sind Marken ihrer jeweiligen Inhaber.
