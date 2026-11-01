# Einrichtung unter Windows

Schritt-für-Schritt-Anleitung, ganz ohne Vorkenntnisse. Wenn irgendwo etwas anders aussieht als beschrieben oder eine Fehlermeldung kommt: **Text kopieren und schicken**, nicht raten.

## 1. Terminal öffnen

Startmenü → **„Terminal“** eintippen → *Terminal* öffnen (oben im Tab sollte „PowerShell“ stehen).

Alle Befehle unten tippst (oder kopierst) du dort ein und bestätigst mit Enter. Einfügen geht mit **Strg+V** oder mit Rechtsklick.

## 2. Git und Python installieren

```
winget install --id Git.Git -e
```

```
winget install --id Python.Python.3.14 -e
```

Bestätige Nachfragen mit `Y`. **Danach das Terminal schließen und neu öffnen**, damit Windows die neuen Programme findet.

Prüfen, ob alles da ist:

```
git --version
```

```
python --version
```

Beide sollten eine Versionsnummer ausgeben. Python muss **3.14 oder neuer** sein, weil die Spieldaten mit zstd komprimiert sind und erst Python 3.14 das von Haus aus kann. Wenn du schon Python 3.14 hast, überspringst du die Installation einfach.

## 3. Projektordner anlegen und das Repo holen

Wir legen alles unter `C:\KarPur` ab. Pfade ohne Leerzeichen ersparen später viel Ärger.

```
mkdir C:\KarPur
```

```
cd C:\KarPur
```

```
git clone https://github.com/Grunttanamo/KarPur.git repo
```

```
cd C:\KarPur\repo
```

Den Schutz einschalten, der Commits mit Spieldateien verhindert:

```
git config core.hooksPath .githooks
```

Testen, ob die Prüfung läuft:

```
python tools/check_content.py
```

Erwartete Ausgabe: `check_content: OK`

## 4. Deine Spieldateien eintragen

Die Dumps und `prod.keys` bleiben **außerhalb** des Repos, zum Beispiel in `C:\KarPur\dumps\`. Sie werden nie kopiert, hochgeladen oder geteilt.

1. Kopiere `config.example.toml` nach `config.local.toml`:

   ```
   copy config.example.toml config.local.toml
   ```

2. Öffne sie im Editor:

   ```
   notepad config.local.toml
   ```

3. Trage die **Pfade** zu deinen Dateien ein (Basisspiel, Update, `prod.keys`) und speichere. `config.local.toml` wird von Git ignoriert, sie bleibt also nur bei dir.

## 5. Aktuellen Stand holen

Wann immer es Neuerungen im Repo gibt:

```
cd C:\KarPur\repo
```

```
git pull
```

## Was später noch dazukommt

Für den Lifter (Meilenstein M2) kommen MSYS2 mit Clang, Ninja und das Vulkan SDK dazu. Diese Anleitung wird dann erweitert.
