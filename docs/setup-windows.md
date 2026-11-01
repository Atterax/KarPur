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

## 6. Python-Umgebung einrichten (einmalig)

Die Werkzeuge brauchen zwei Zusatzpakete (`cryptography` zum Entschlüsseln, `lz4` zum Entpacken). Damit sie nicht mit anderen Python-Programmen auf deinem PC durcheinanderkommen, bekommen sie eine eigene, abgeschlossene Umgebung im Ordner `.venv`:

```
cd C:\KarPur\repo
```

```
python -m venv .venv
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

Am Ende sollte `Successfully installed ...` stehen.

Ab jetzt startest du die Werkzeuge immer mit `.venv\Scripts\python` statt nur `python`.

## 7. Spiel analysieren (Meilenstein M1)

```
.venv\Scripts\python tools\extract.py
```

Das Tool:
- öffnet Basisspiel, Update und DLC mit deinen Keys,
- entpackt die Programmdateien nach `C:\KarPur\repo\game\` (das ist Git-ignoriert, es wird nie hochgeladen),
- prüft dabei jede Prüfsumme,
- gibt einen **Bericht** aus, der zwischen zwei Linien aus `=====` steht.

Es dauert je nach Festplatte etwa eine Minute. Der Bericht enthält nur Kennungen, Größen, Prüfsummen und Funktionsnamen, also keine Spieldaten und keine Keys. **Diesen Bericht kannst du kopieren und schicken.** Er wird außerdem in `game\report\m1-report.txt` gespeichert.

Wenn statt des Berichts eine Zeile mit `error:` kommt, schick einfach diese Zeile.

## 8. C-Compiler installieren (Meilenstein M2)

Der übersetzte Spielcode ist C-Code, und der muss zu einem Windows-Programm kompiliert werden. Dafür brauchen wir **Clang** aus **MSYS2**. MSYS2 ist eine Sammlung von Entwickler-Werkzeugen für Windows. Die Installation lädt etwa 1–2 GB herunter.

MSYS2 installieren (landet in `C:\msys64`):

```
winget install --id MSYS2.MSYS2 -e
```

MSYS2 aktualisieren. **Diesen Befehl zweimal hintereinander ausführen**, weil der erste Durchlauf nur die Grundpakete erneuert und sich dann beendet:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
```

Clang, den Linker LLD und das Build-Werkzeug Ninja installieren:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-ninja"
```

Prüfen:

```
C:\msys64\ucrt64\bin\clang.exe --version
```

Erwartete Ausgabe: eine Zeile wie `clang version 2x.x.x` und darunter `Target: x86_64-w64-windows-gnu`.

## 9. Spielcode übersetzen und kompilieren (Meilenstein M2)

Neuen Stand holen und ein neues Python-Paket (`numpy`) nachinstallieren:

```
cd C:\KarPur\repo
```

```
git pull
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

Dann den Lifter starten:

```
.venv\Scripts\python tools\lift.py
```

Das Tool:
- übersetzt `main`, `subsdk0` und `sdk` in C-Code (Ordner `generated\`, Git-ignoriert),
- kompiliert jede C-Datei mit Clang, auf allen Prozessorkernen gleichzeitig (Ordner `build\lift\`),
- gibt am Ende einen **Bericht** zwischen zwei `=====`-Linien aus.

**Das dauert eine Weile, rechne mit 10 bis 40 Minuten.** Der PC wird dabei voll ausgelastet und kann laut werden, das ist normal. Es braucht etwa 5–8 GB freien Platz auf C:. Zwischendurch siehst du Fortschrittszeilen wie `125/400 files, 300s`.

Den Bericht am Ende bitte kopieren und schicken. Er enthält nur Zahlen, Instruktionsarten und Dateinamen, keinen Spielcode.

## 10. Spieldaten entpacken (Meilenstein M3)

Die Spieldaten (Grafiken, Texte, Welt) stecken im Basisspiel, und das Update ändert einen Teil davon. Das Werkzeug **hactool** führt beides zusammen. hactool wird einmal auf deinem PC gebaut.

Neuen Stand holen:

```
cd C:\KarPur\repo
```

```
git pull
```

Das Build-Werkzeug `make` für MSYS2 installieren:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make"
```

hactool herunterladen und bauen (dauert unter einer Minute):

```
.venv\Scripts\python tools\build_hactool.py
```

Erwartete Ausgabe: eine Zeile, die mit `...\toolchain\hactool\hactool.exe: 1d64a83 + portable.patch, unicode.patch, mbedtls-asm-clobbers.patch` endet.

Dann die Spieldaten zusammenführen:

```
.venv\Scripts\python tools\extract_romfs.py
```

Das Werkzeug kopiert die beiden großen Datenpakete aus deinen Dumps, lässt hactool sie zusammenführen und dabei jede Prüfsumme kontrollieren, und legt das Ergebnis als `game\romfs\romfs.bin` ab. **Das dauert etwa 5–20 Minuten** und braucht zeitweise rund 30 GB freien Platz, danach bleiben etwa 6–7 GB. Am Ende kommt wieder ein Bericht zwischen `=====`-Linien, den bitte schicken.

## 11. karpur.exe bauen und zum ersten Mal starten (Meilenstein M3)

Neuen Stand holen:

```
cd C:\KarPur\repo
```

```
git pull
```

Die Bibliotheken installieren, die die Runtime braucht (Fenster und Eingabe, Ton, Video, Kompression, Vulkan):

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-libvpx mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-vulkan-memory-allocator"
```

Das Inhaltsverzeichnis der Spieldaten einmal neu schreiben (dauert etwa eine halbe Minute, die Daten selbst bleiben unverändert):

```
.venv\Scripts\python tools\extract_romfs.py --index-only
```

`karpur.exe` bauen (übersetzter Spielcode + Runtime, etwa 5 Minuten):

```
.venv\Scripts\python tools\build_game.py
```

Am Ende sollte eine Zeile wie `build\game\karpur.exe: file ... MiB` stehen.

Selbsttest, braucht keine Spieldaten und dauert nur Sekunden:

```
build\game\karpur.exe --selftest
```

Und dann der erste echte Start. Er läuft ohne Fenster, das Spiel arbeitet nur im Hintergrund und schreibt mit, wie weit es kommt:

```
cmd /c "build\game\karpur.exe --boot > game\report\boot.log 2>&1"
```

Falls sich nach 2 Minuten nichts mehr tut, mit **Strg+C** abbrechen. Danach die letzten 80 Zeilen des Logs anzeigen:

```
Get-Content game\report\boot.log -Tail 80
```

Diese Zeilen bitte schicken. Ein Absturz ist hier ganz normal: Er zeigt uns, welcher Teil der Switch als Nächstes fehlt.

## 12. Shader-Übersetzer einrichten (Meilenstein M4)

Die Switch-Grafikkarte hat ihre eigene Shader-Sprache (kleine Programme, die Pixel und Ecken berechnen). Ein Übersetzer aus dem Emulator Ryujinx wandelt sie in SPIR-V um, die Sprache, die Vulkan auf deiner RTX 5080 versteht. Der Übersetzer ist in C# geschrieben und wird mit **.NET** zu einer eigenen `.exe` gebaut. Zum Verlinken dieser `.exe` braucht .NET den Linker von Microsoft aus den **Visual Studio Build Tools**.

.NET 10 SDK installieren:

```
winget install --id Microsoft.DotNet.SDK.10 -e
```

Visual Studio Build Tools mit dem C++-Paket installieren. **Das lädt etwa 3–5 GB herunter und dauert 10–20 Minuten.** Ein Fenster des Visual Studio Installers zeigt den Fortschritt; es schließt sich am Ende von selbst:

```
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Das Prüfwerkzeug für SPIR-V aus MSYS2 installieren:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-spirv-tools"
```

**Danach das Terminal schließen und neu öffnen.** Prüfen:

```
dotnet --version
```

Erwartete Ausgabe: eine Zahl, die mit `10.` beginnt.

```
Test-Path "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
```

Erwartete Ausgabe: `True`

Neuen Stand holen:

```
cd C:\KarPur\repo
```

```
git pull
```

Den Übersetzer bauen. Beim ersten Mal lädt .NET noch Pakete nach, **rechne mit 3–10 Minuten**:

```
.venv\Scripts\python tools\build_xlate.py
```

Erwartete Ausgabe: zuerst neun Zeilen `applied 0001-...` bis `applied 0009-...`, dann viele Zeilen von .NET, am Ende `translator identity ...` und `built ...\build\xlate\totk_xlate.exe (... MB) in ... s`. (Der Name `totk_xlate` stammt aus dem Projekt, aus dem wir den Übersetzer übernommen haben; umbenannt wird später.)

Schnelltest mit einem selbstgebauten Mini-Shader, braucht keine Spieldaten:

```
.venv\Scripts\python tools\xlate_smoke.py
```

Erwartete Ausgabe:

```
replies: ['ok const_ps', 'fail ? ...', 'ok again']
spirv-val exit 0 (... bytes)
```

Die mittlere `fail`-Antwort ist Absicht: Der Test schickt einmal Unsinn und prüft, dass der Übersetzer danach weiterarbeitet.

Wenn `build_xlate.py` mit einer Fehlermeldung endet: die letzten 30 Zeilen schicken.

## 13. Erster Start mit Fenster (Meilenstein M4)

Bisher lief `karpur.exe` mit `--boot` ohne Bild und hörte beim ersten Grafikaufruf auf. Jetzt starten wir es ohne `--boot`: Es öffnet ein Fenster und versucht zu zeichnen. **Ein schwarzes Fenster oder ein Absturz ist hier normal**; das Log zeigt uns, welche Teile der Grafik noch fehlen.

```
cd C:\KarPur\repo
```

```
build\game\karpur.exe
```

Warte etwa 2 Minuten (oder bis sich das Fenster von selbst schließt). Wenn es dann noch offen ist, schließe es mit dem **X** oben rechts oder drücke im Terminal **Strg+C**.

Das Log liegt in `build\game\logs\karpur.log`. Die letzten 80 Zeilen anzeigen:

```
Get-Content build\game\logs\karpur.log -Tail 80
```

Und dazu die Zeilen über Grafik und Shader (höchstens 150):

```
Select-String -Path build\game\logs\karpur.log -Pattern "nvn|gfx|vk_|shader|fatal|error|missing|unimpl" | Select-Object -Last 150
```

Beide Ausgaben bitte schicken. Sie enthalten Funktionsnamen, Adressen und Fehlermeldungen, keine Spieldaten.
