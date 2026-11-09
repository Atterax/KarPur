# Der KarPur-Launcher

Mit dem Launcher baust du das Spiel ohne Befehle. Er braucht keine Installation und keine Administratorrechte, und er lädt nur Werkzeuge von deren offiziellen Seiten herunter. Deine Dumps, deine Schlüssel und das fertige Spiel bleiben auf deinem PC. Der Launcher ist auf Englisch, damit ihn alle nutzen können.

## Was du brauchst
- *Pokémon Purpur* von deiner eigenen Konsole:
  - das **Grundspiel als `.xci`**. Eine `.nsp` als Grundspiel funktioniert nicht, denn die Systemschriften stammen aus dem Kartenabbild.
  - das **Update 4.0.0 als `.nsp`**
  - das **DLC als `.nsp`**, wenn du es hast (optional)
- `prod.keys` von derselben Konsole
- Windows mit einer Grafikkarte, die Vulkan kann, und einem aktuellen Treiber
- Rund **30 GB freien Speicher** im Ziel-Ordner während des Baus. Danach bleiben etwa 12,5 GB belegt.
- Eine Internetverbindung für die Werkzeuge (rund 250 MB)

## So geht's
1. Starte `KarPur-Launcher.exe`.
2. Wähle im Tab **Setup** mit „Browse …“ die vier Dateien und einen **Target folder** ohne Leerzeichen, zum Beispiel `C:\Spiele`.
3. Klick auf **Build**. Der erste Bau dauert je nach PC 30–60 Minuten. Dein PC ist dabei voll ausgelastet und kann laut werden. „Show details“ zeigt, was gerade passiert.
4. Danach startest du das Spiel mit **Play**.

Im Ziel-Ordner liegen danach:
- eine **Verknüpfung „Pokémon Violet (KarPur)“** mit dem Spiel-Icon, zum Starten per Doppelklick
- eine Kopie von **`KarPur-Launcher.exe`** für Einstellungen, Reparatur und Updates
- der Ordner **`KarPur`** mit dem Spiel und allen Daten

Den Launcher kannst du jederzeit schließen oder den Bau mit **Cancel** abbrechen. Beim nächsten **Build** geht es an derselben Stelle weiter.

## Die Tabs
- **Home:** die Startseite, sobald das Spiel gebaut ist. Ein Bild in den Farben deiner Edition (Lila und Neon-Türkis für Purpur, Rot und Bernstein für Karmesin), der **Play**-Knopf, „Open game folder“ und auf einen Blick: Spielversion, DLC, wie viele Shader vorbereitet sind, wann du zuletzt gespeichert hast und **dein aktuelles Team** mit Name und Level. Das Team schreibt das Spiel bei jedem Speichern in die Datei `team.json` neben deinen Spielständen.
- **Setup:** Pfade, **Build** (bzw. **Rebuild**), **Repair** und **Cancel**. **Repair** stellt alle Projektdateien wieder her, richtet Python neu ein, prüft die Spieldaten und baut nur nach, was fehlt oder verändert wurde. Spielstände und Einstellungen fasst Repair nicht an.
- **Settings:** dieselben Einstellungen wie die PC-Einstellungen im Spiel: Vollbild, Fenstergröße, FPS-Limit, Bildsynchronisation, Hochskalierung, Schärfe, Leistungsanzeige und Anzahl wilder Pokémon. Dazu DLAA, DLSS, Frame Generation, der **Kompatibilitätsmodus** (falls das Spiel nach der Charaktererstellung abstürzt) und die **Farbe des Launchers**: automatisch nach deinem Spiel (Lila für Purpur, Rot für Karmesin) oder fest gewählt.
- **Controls:** Tastatur, Maus und Controller. Knopf anklicken, dann die gewünschte Taste drücken; Rechtsklick setzt Standard oder „keine“.
- **Saves:** dein Spielstand. **Export…** speichert ihn als `.zip` (zum Sichern oder für einen anderen PC), **Import…** holt einen solchen Spielstand zurück, **Open folder** öffnet den Ordner. Unter **Backups** kannst du einen der letzten Spielstände wiederherstellen. Bei Import und Wiederherstellen legt der Launcher den aktuellen Spielstand vorher als Backup ab, es geht also nichts verloren. Das Spiel muss dabei geschlossen sein.
- **Feedback:** ein Bericht, wie KarPur bei dir läuft. Launcher-Version, Spiel, Update, DLC, Grafikkarte, Prozessor, Arbeitsspeicher (Typ und Größe) und Windows-Version füllt der Launcher selbst aus, du kannst alles ändern. Dazu kommt ein Textfeld (bis 5000 Zeichen). „Attach log“ hängt das Ende des Spiel-Logs an, dabei werden die Ordnernamen deines Benutzerkontos weggelassen. **Open on GitHub** öffnet den fertig ausgefüllten Bericht als GitHub-Issue im Browser; dort prüfst du ihn und schickst ihn ab. Dafür brauchst du ein kostenloses GitHub-Konto. Die Berichte sind öffentlich. **Copy report** kopiert den Bericht, zum Beispiel für Discord.
- **Update:** sucht auf der GitHub-Releases-Seite nach einer neuen Version. **Download and install** ersetzt den Launcher und baut danach nur, was sich geändert hat. Meist dauert das wenige Minuten.

## Was der Launcher herunterlädt
- Python (offizielles Paket von python.org) und fünf Python-Pakete von PyPI
- den Compiler: Clang und die Bibliotheken des Spiels als Pakete von MSYS2 (mirror.msys2.org)

Jede Datei hat eine feste Prüfsumme. Eine veränderte Datei lehnt der Launcher ab.

## Wenn etwas nicht klappt
- Der Launcher nennt den Fehler. Das vollständige Protokoll steht in `KarPur\launcher.log` im Ziel-Ordner, die Ausgaben der Werkzeuge in `KarPur\build\setup.log`.
- Hilft das nicht, probier **Repair**.
- Die Protokolle enthalten keine Schlüssel. Teile trotzdem nie deine Dumps oder `prod.keys`.

## Für Entwickler
- **Launcher bauen:** `tools/make_launcher.py`. Das braucht das .NET 10 SDK, den Shader-Übersetzer und hactool. Die Version steht in `launcher/KarPurLauncher.csproj`.
- **Bau-Skript:** `tools/setup_build.py`. Es lässt sich auch ohne Launcher starten. Es baut nur Schritte nach, deren Ergebnis fehlt oder deren Eingabedateien sich geändert haben (`STEP_INPUTS`).
- **Versionen der Downloads:** `tools/launcher_pins.py` (Python) und `tools/toolchain_msys2.py pin` (Compiler) legen sie fest.
- **Update veröffentlichen:** Release mit Tag `vX.Y.Z` und der Datei `KarPur-Launcher.exe` anlegen. Vorher die Version im `.csproj` erhöhen.
- **Menü-Liste:** `tools/data/ui_patch_violet_4.0.0.txt` ist die Liste hinter den PC-Einstellungen im Spielmenü. Sie enthält nur Größen, Prüfsummen, Positionen, unsere Werte und unsere Texte.
