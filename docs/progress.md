# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M5, spielbar

### Nächste Schritte
1. **Transform Feedback fertig** (stabil, Figuren/Pokémon/NPCs korrekt, mit GPL an). Aufräumen bei Gelegenheit: Schrittlogs der ersten 2 TF-Draws in `vk_xfb.c`; Pause/Resume und DrawTransformFeedback sind nicht umgesetzt (Purpur nutzt sie bisher nicht).
0. **Darstellung und Bildrate stimmen** (Mirror + späte Sync + Pool-Abgabe ≤64 MiB + heiße Stücke; `SUBMIT_SYNC` aus). Test-Schalter aufgeräumt. 60 fps **pausiert** (Logik läuft pro Bild, kein einzelner Zeitschritt gefunden; Runtime hält im Gebiet keine 60 fps, siehe Protokoll). Als Nächstes: **Sichtweite/LOD**, danach ggf. Leistung bei 60 fps. Heiße Stücke über längere Sitzungen beobachten.
0b. **Sichtweite/LOD erweitern** (Wunsch des Maintainers, wie Switch-2-Fassung): LOD-/Distanz-Parameter im Spielcode finden, per Patch-Schalter vergrößern. Nach fehlerfreier Darstellung.
1. **Baum-Flackern**: behoben durch `SUBMIT_SYNC=1` als Standard (Wettlauf GPU-Lesen vs. Spiel-Schreiben). Sauberer: herausfinden, welche Puffer die GPU zu spät liest, und sie beim Submit kopieren (wie `vk_usnap` für Uniforms), dann das Warten wieder abschalten.
2. **Bildfehler.** Unsichtbare Objekte **behoben** (Übersetzer-Patch 0010, Optimierer wieder an, siehe Protokoll 2026-11-02). Offen: Schatten-Feinheiten, `SetScissors: differing scissor ignored`, konservative Rasterung. Baum-Flackern behoben durch `SPLIT_DRAWS=0` als Standard (Ursache dahinter offen, siehe Protokoll). Schatten prüfen. Außerdem: Flackern, fehlende/fehlerhafte Schatten, schwarze Flächen (Dunstabzug, Fensterscheiben), kurze „Stachel“-Artefakte bei Effekten, `SetScissors: differing scissor ignored`, `copy of format 0x35 texture ... skipped`, `conservative rasterisation requested: drawn normally`.
3. Die übrigen NVN-Funktionen umsetzen, wenn sie aufgerufen werden.
4. Offen aus der Importliste: `codec::HardwareOpusDecoder` (Klassen-API), DLC-Freischaltung (`aoc` meldet keine DLC).

### Offene Fragen
- Gleiche Namen heißen nicht gleiche Strukturen: Bei SDK 13 vs. 20 können sich Strukturlayouts und Revisionen ändern (z. B. `NpadFullKeyState`, `AudioRendererParameter`-Revision). Jede übernommene Ersatzfunktion muss gegen SDK 13 geprüft werden.
- Spin-Wait-Patches: TotK leitet ~860 Warteschleifen über `rt_spin_gate` (Patch-Set `spin`, `tools/spin_waits.py`). Wir haben keine Patch-Datei; die asynchrone Präemption (`TOTK_ASYNC_PREEMPT`, Standard an) sollte Hänger abfangen, aber das Spiel kann dadurch langsamer sein. Bei Hängern beim Start zuerst hier ansetzen.
- `nn::aoc::ListAddOnContent` meldet noch keine DLC (TotK-Verhalten). Für Purpur später die vorhandene DLC melden.
- Interne Namen und Umgebungsvariablen heißen noch `TOTK_*` (z. B. `TOTK_GPU=null`, `TOTK_TRACE`). Umbenennen erst, wenn die Runtime läuft.
- `--seconds S` beendet den Lauf über die TotK-Weltuhr (`frame_step_world_us`); mit `rt_game_version = 0` ist unklar, ob sie läuft. Für zeitbegrenzte Läufe mit Bildschirmfoto (`--shot`) später eine eigene Abbruchbedingung (z. B. Anzahl Frames) einbauen.
- `--probe-shader` braucht einen Shader-Census (`tools/shader_census.py` aus TotK, nicht übernommen). Erst übernehmen, wenn wir wissen, wo Purpur seine Shader-Pakete hat (wahrscheinlich in `data.trpfs`, nicht als lose `.bnsh`).
- Werden `nn::movie`-Funktionen überhaupt aufgerufen? Die Videos sind Bink 2 (`.bk2`), das der Spielcode selbst dekodiert. TotKs `ov_movie.c` (VP9 über libvpx) wird dann wohl nicht gebraucht.

## Protokoll

### 2026-11-02: 60 fps, erster Schritt: Experiment mit festem Bildintervall
- Ausgangslage: Violet setzt das Present-Intervall 2 (30 fps); die Runtime taktet danach (`nvn_api_obj.c present_period`). TotKs Weg (Patch-Sets `fps60`/`realtime`, `frame_rate.c`) braucht spielspezifische Adressen (Intervall-Regler, Zeitschritt pro Frame), die es für Violet noch nicht gibt.
- Neu: `TOTK_PRESENT_INTERVAL=n` taktet unabhängig vom Spiel mit n Ticks à 60 Hz; die ersten 8 Intervall-Wechsel des Spiels werden geloggt („the game sets the present interval …“).
- Frage des Experiments: Läuft Violet mit Intervall 1 bei 60 fps in normaler Geschwindigkeit (Spielzeit aus gemessener Zeit) oder doppelt so schnell (fester Schritt 1/30 s)? Davon hängt ab, ob ein Zeitschritt-Patch nötig ist.
- Ergebnis: Mit `PRESENT_INTERVAL=1` läuft das Spiel **doppelt so schnell** → fester Schritt pro Frame; ein Zeitschritt-Patch ist nötig. Das Spiel setzt das Intervall einmal bei Frame 0 von 1 auf 2 und ändert es danach nie. Nach dem Aufräumen sieht alles aus wie vorher (Maintainer).
- Nächster Schritt: Die Runtime loggt beim Intervall-Setzen Aufrufer und Frame-Kette (`called from`, `frame N returns to`); mit `calls.py` die Quelle der 2 finden und prüfen, ob dieselbe Größe den Zeitschritt bestimmt (dann reicht ein Patch der Quelle auf 1).
- Aufrufkette von `nvnWindowSetPresentInterval(2)`: Rücksprung `main+0xbcc044`, dann `main+0xbcb328`, `main+0xbc5344`, `main+0xbc5290`, `main+0x11bccec`, `main+0x11acbb0`, `main+0x92bdc0`. `calls.py` kann jetzt mit `--origin MOD+OFF:xN` die Herkunft eines Registers zeigen (Konstante, 32/64-Bit-Load mit Basis/Offset, Adressrechnung, Kopie).
- Bericht: Das Intervall (x1 am BLR `main+0xbcc040`) ist das Ergebnis der Funktion `main+0xbcc17c` (17 Instruktionen), aufgerufen bei `main+0xbcbec8+0x140`; weiter oben ist x1 nur durchgereicht (Kopien). Neu `calls.py --returns MOD+OFF` (Herkunft von x0 an jedem Return, Vergleiche, Tail-Calls) und CSEL/CSINC in der Registerherkunft. `main+0xbcc17c` gibt das Ergebnis von `main+0xbcd35c` (134 Instruktionen) zurück; sein Objekt kommt aus `[x19 + 0x30]` des Aufrufers. `--returns` folgt solchen Weiterleitungen jetzt bis 4 Ebenen.
- `main+0xbcd35c` ist eher eine allgemeine Nachschlagefunktion (Rückgabe = Ergebnis eines weiteren Aufrufs); `main+0xbcc17c` hat nur einen Aufrufer (das Intervall-Setzen). Dieser Weg führt nicht zum Zeitschritt. Websuche: nur Berichte über einen Emulator-60-fps-Mod (theboy181, 2022) ohne technische Details.
- Plan B: `runtime/value_scan.c`. `TOTK_VALUE_SCAN=1` durchsucht bei Present 900 alle beschreibbaren Seiten (Modul-Daten, Heap, gemappter Speicher) nach float 1/30, float 30.0 und double 1/30 und loggt Anzahlen und erste Fundstellen (Moduloffset oder Gastadresse); bei Present 1200 wird geprüft, welche Fundstellen den Wert behalten. `TOTK_POKE=ADDR=f:/d:/i:WERT[,…]` schreibt vor jedem Present Werte (Kandidaten testen: mit `PRESENT_INTERVAL=1` auf 1/60 setzen → normale Geschwindigkeit?).
- Wertesuche (Present 900): float 1/30: 852 Treffer, double 1/30: 36, float 30.0: 50537 (wohl Animationsdaten mit 30 Bildern/s) – alle im gemappten Speicher (alias), keine in Modul-Daten oder Heap; bei Present 1200 hielten fast alle ihren Wert.
- Halbierungs-Test: `TOTK_POKE_SCAN=f32:0-851,d64:0-35` setzt ab dem Scan die gewählten Treffer jede Millisekunde (eigener Thread) auf 1/60 (f30: 60.0). Erst alle zusammen mit `PRESENT_INTERVAL=1`: normale Geschwindigkeit → Zeitschritt ist darunter, dann halbieren; sonst liegt er nicht als gespeicherte 1/30 vor.
- Ergebnis alle zusammen: Lauf 1 erst doppelt so schnell, dann ~20 s Hänger (Scan), danach **normale Geschwindigkeit**, später Absturz in `main+0x3306380`. Lauf 2: sofortiger Absturz bei `main+0x41d14` mit x0 = double 1/60 → manche Treffer sind keine Fließkommazahlen (wiederverwendeter Speicher, Schlüssel o. Ä.), unser Schreiben hat sie zerstört. Der Zeitschritt liegt also sehr wahrscheinlich unter den float-1/30-Treffern.
- Umbau: `TOTK_POKE_SCAN` halbiert jetzt **in einem Lauf per Taste** (die Trefferreihenfolge schwankt zwischen Läufen, 816 vs. 854): Die gewählten Treffer sind Kandidaten; die erste Hälfte wird auf 1/60 gesetzt, der Rest auf 1/30 zurück. F6 = jetzt normale Geschwindigkeit (Schritt in der gesetzten Hälfte), F7 = immer noch zu schnell (andere Hälfte). Log: „bisect …: N candidates left“, am Ende „one candidate left: … at ADRESSE“. Geschrieben wird nur, wo noch 1/30 bzw. unser 1/60 steht, damit fremde Daten unberührt bleiben.
- Erster Versuch: nach dem Scan dauerhaft ~1 fps. Ursache: `plat_mem_readable` (VirtualQuery) für jeden Kandidaten jede Millisekunde – auf den großen gemappten Bereichen langsam und mit Adressraum-Sperre. Jetzt prüft der Thread einmal pro Sekunde über `mem_query` (eigenes Speichermodell), ob die Adressen noch gemappt sind.
- Halbierung über alle 795 float-1/30-Treffer: in jeder Hälfte immer doppelt so schnell (10× F7), danach 60 fps stabil (1% low 55,6). **Der Zeitschritt ist keiner der float-1/30-Treffer.** Der normale Lauf im ersten Test kam also wohl von einem der 28 double-1/30-Treffer (dort lag auch der Absturz). Nächster Test: `POKE_SCAN=d64:0-99`.
- Der Hänger von ~25 s (Frame 1201) war die Nachprüfung bei Present 1200 (`holds` mit VirtualQuery für ~50000 Treffer), nicht der Scan (525 ms). `holds` nutzt jetzt `mem_query`.
- Halbierung über die 27 double-1/30-Treffer: ebenfalls immer doppelt so schnell. **Der Zeitschritt liegt nicht als beschreibbarer float/double 1/30 im Speicher.** Der „normale Lauf“ im allerersten Test war wohl eine Täuschung (ungeprüftes Schreiben, Absturz kurz danach).
- Neu: `tools/consts.py` sucht im Spielcode (main, auf Wunsch weitere Module) nach Zeitschritt-Konstanten: 1/30, 30, 1/60, 60 als float/double, 33,333/16,667 ms, 33333/16666 µs, 33333333/16666667 ns. Gefunden werden MOVZ/MOVN/MOVK-Ketten, FMOV-Immediates, Wörter in .rodata/.data und die Loads darauf (LDR literal, ADRP+LDR, ADRP+ADD, Vektor-LDR q), jeweils mit Funktion. Nur Adressen und Anzahlen.
- `TOTK_POKE` schreibt jetzt auch in schreibgeschützte Modul-Konstanten (Host-Seite wird einmal beschreibbar gemacht): Kandidaten aus .rodata lassen sich so ohne Neubau testen. Konstanten als Immediate im Code brauchen dagegen einen Generator-Patch (`recomp/patches.py`, Patch-Seite +0..15 ist für den Frame-Schritt reserviert).
- `consts.py`-Bericht (main): float 1/30 als Immediate an **18 Stellen** (MOVZ+MOVK) in 13 Funktionen, 2 ungelesene .rodata-Wörter; float 30.0: 6× MOVZ/MOVK, 26× FMOV, 1 Vektor-Load; float 1/60: 4×; float 60.0: 10×; double 30.0: 2× FMOV; 33333 µs und 33334 µs je 1×; 16666667 ns 1×; double 1/30 nirgends.
- Neu: Testweg für Code-Konstanten in **einem** Build. `consts.py --patch-file build\fps_candidates.txt` schreibt eine Patch-Tabelle (lokal, enthält Instruktionswörter) mit einem Schalter pro Stelle: `fs_<off>` (float 1/30 → 1/60; nur das MOVK mit der oberen Hälfte, die untere 0x8889 ist bei beiden gleich), `fr_<off>` (MOVZ/MOVK 30.0 → 60.0; FMOV #30.0 geht nicht, FMOV reicht nur bis 31.0), `us_<off>` (33333/33334 → 16667). `lift.py --patches DATEI --gen-only` lässt den Generator damit laufen, `build_game.py` baut nur die betroffenen Chunks neu. `TOTK_PATCH_BISECT=1` halbiert ab Present 900 die Schalter per F6/F7 wie bei der Speicher-Halbierung (`rt_patch_count/rt_patch_name` neu in `patches.c`).
- Ergebnis Patch-Halbierung (26 Sätze: 18× fs, 6× fr, 2× us): Generator 46 s, Bau ok, in jeder Hälfte doppelt so schnell. **Keine der 1/30-, 30.0- oder 33333-µs-Code-Konstanten ist der Zeitschritt.** Kurze Einbrüche später (Menü/Karte, `fence/present wait` bis 27 ms, 1% low 33–43): bei 60 fps mit doppelt so viel Spielarbeit, vermutlich nicht durch die Patches.
- Neue Annahme: Der Schritt ist „Intervall × 1/60“ (oder Intervall / 60), wobei das Spiel sein eigenes Intervall 2 aus der Konfiguration kennt (unser `PRESENT_INTERVAL=1` ändert nur unseren Takt, nicht diese Zahl). Daher zusätzlich Sätze `fh_<off>` (float 1/60 → 1/120, 4 Stellen) und `fx_<off>` (float 60.0 → 120.0, 10 Stellen); alle Sätze halbieren einen Schritt. Wenn auch das nichts bringt: die Intervall-Quelle selbst (Konfigurationswert hinter `main+0xbcc17c`/`main+0xbcd35c`) auf 1 patchen, dann folgen Takt und ggf. Schritt.
- Ergebnis 40 Sätze (inkl. fh/fx): wieder in jeder Hälfte zu schnell. Der Maintainer kann es kaum beurteilen, weil die Bildrate bei `PRESENT_INTERVAL=1` je nach Ort zwischen 30 und 60 springt (Minute 1–4: mean 40–48 fps, 1% low 22–35, „fence/present wait“ 25–36 ms) und sich die Geschwindigkeit damit mitändert.
- **Schluss (vorläufig): 60-fps-Suche pausiert.** Die Spielgeschwindigkeit folgt der Bildrate (auch bei schwankenden fps), also läuft die Logik pro Bild ohne gemessene Zeit. Ein einzelner Zeitschritt-Wert war weder im Speicher (float/double 1/30) noch als Code-Konstante (1/30, 30, 1/60, 60, 33333 µs) zu finden. Möglich bleibt: die Intervall-2-Quelle in der Konfiguration (`main+0xbcc17c`/`main+0xbcd35c`) und eine engine-weite „Bilder pro Update“-Größe wie bei TotK – aufwendig. Außerdem hält die Runtime im offenen Gebiet derzeit keine 60 fps (GPU-/Sync-Wartezeit ~30 ms pro Bild); das wäre vorher zu lösen.
- Werkzeuge bleiben: `value_scan.c` (`VALUE_SCAN`, `POKE`, `POKE_SCAN`, `PATCH_BISECT`), `tools/consts.py` (`--patch-file`), `lift.py --patches/--gen-only`. Ohne Patch-Tabelle neu liften entfernt die Schalter wieder (`lift.py --gen-only`).
- Glättung ohne Logik-Änderung: Bildgenerierung im Treiber bzw. extern (z. B. NVIDIA Smooth Motion, falls für Vulkan verfügbar; Lossless Scaling) – nicht Teil des Projekts.
- **Neuer Hinweis vom Maintainer: NX Optimizer** (github.com/MaxLastBreath/nx-optimizer, Lizenz **CC BY-NC 2.0** → nur lesen, nichts übernehmen; Checkout in `refs/`). Für Violet liefert es „DynamicFPS“: ein exlaunch-Modul (`subsdk3`, „Universal-FPS“) für **Violet 3.0.1** (wir haben 4.0.0). Verhalten laut Disassemblierung:
  - Hook in der NVN-Bibliothek (sdk-Modul) beim Setzen des Present-Intervalls: Intervall aus der Ziel-fps (≤30 → 2, 60 → 1, sonst 0). Bei uns ersetzt die Runtime NVN; entspricht `PRESENT_INTERVAL`.
  - Hook pro Bild (ebenfalls sdk/NVN): misst die echte Bildzeit mit `GetSystemTick`, mittelt über ~8 Bilder → Bildzeit s in Sekunden; dazu ein Bildbegrenzer mit `SleepThread`.
  - **Inline-Hooks im Spielcode (3.0.1-Offsets)**, die ein Register mit einem Wert proportional zu s überschreiben: `main+0x92dd2c` → x2 = s·10⁹ ns (bei 30 fps 33333334), `main+0xd058dc` → x23 (bei 30 fps 0x43340000), `main+0x2b7c540` → x0 (dort zusätzlich fest `mov w0, #60.0f`).
  - Feste Code-Patches: `main+0x170c408` → `mov w10, #10.0f`; `main+0xb92780/84` → x1 = 33333334.
  - „LodBias Patch“ 1/2: `main+0xc07dc4` und `main+0x7f1a6c` → `fmov s0, #-15.0` (für Sichtweite/LOD interessant).
  - Folgerung: Violet hat doch Zeitgrößen, nur nicht als 1/30 s, sondern als **Nanosekunden pro Bild** (33333334) und weitere Werte, die unsere Suche nicht kannte.
- `consts.py` sucht jetzt zusätzlich 33333334 ns, 0x43340000 (float 180.0), 0x84e00000, float 10.0 und float -15.0 und zeigt das Zielregister (`--only TEXT` filtert). Nächster Schritt: die 4.0.0-Gegenstücke der 3.0.1-Stellen finden (gleiches Register, ähnlicher Offset), dann eigene Umsetzung über die Patch-Tabelle: Werte aus der Patch-Seite laden, die `frame_rate.c` mit der gemessenen Bildzeit füllt.
- Bericht 4.0.0: **`main+0xd058dc` baut w23 = 0x43340000 – exakt der 3.0.1-Offset des Mods.** Der untere Teil von main ist also zwischen 3.0.1 und 4.0.0 offenbar unverschoben. Weitere 4.0.0-Funde: Funktion `main+0x2b79f60` enthält 33333334 (w10, +0x89c) und 16666667 (w10, +0x8e0) – Bildzeit für 30 bzw. 60 fps; 33333334 auch in `main+0x165d004`/`main+0x165d43c` (w8); float -15.0 nur einmal (`main+0xc3578`, s1). 0x84e00000 kommt nicht vor.
- Neu: `calls.py --insn MOD+OFF` zeigt grob die Instruktionen um eine Adresse (Register, Immediates, Load-Offsets, Aufrufziele), um die übrigen Mod-Stellen in 4.0.0 zu prüfen.

### 2026-11-02: Test-Schalter der Sichtbarkeits-Fehlersuche entfernt
- 26 Diagnose-Commits per `git revert` zurückgenommen (Konflikte nur dort, wo die Spiegel-Arbeit dieselben Zeilen berührt; von Hand gelöst, Doku behalten): `OCCLUSION`, `OQ_GPU/FILL/CLAMP`, `IND_CPU`, `IND_SHOW`, `ARGS_FORCE`, `CS_SHARED`, `SSBO_WHOLE`, `VB_WHOLE`, `DEPTH_EQUAL`, `NO_DISCARD`, `A2C`-Schalter (A2C-Verhalten wie vorher), `RECONVERGE` samt Aktivierung von maximal reconvergence, `ONE_CPU`, `READ_WATCH` (VEH in `platform/win32.c`), F8-Schnappschüsse, Compute-Dispatch-Bericht und Übersetzer-Notizen, Occlusion-/Counter-/Indirect-Statistiken, Tiefenziel-Diagnose, `symbolize.py --log/--grep`. Rund 1070 Zeilen weniger.
- Behalten: `IND_BASE` (Basisinstanz-Korrektur, 06244c2), Zeichenparameter-Übersetzung (f52ac2a), `XLATE_DEBUG`, `SUBMIT_SYNC` (Standard 0), `TEX_RECLAIM`, `MIRROR_LATE/DEMOTE/DEMOTE_MAX/HOT`, `XLATE`-Patch 0010.
- Geprüft: Lifter-Test grün; geänderte Runtime-Dateien kompilieren hier (Header-bedingte Fehler in anderen Dateien bestanden vorher); Erweiterungsliste in `vk_device.c` wieder 28 Plätze, belegt höchstens 22.

### 2026-11-02: SUBMIT_SYNC wieder aus
- Test `SUBMIT_SYNC=0` mit Mirror + später Sync + heißen Stücken: keine Einbrüche, kein Flackern, nichts Auffälliges (Maintainer). Das Warten nach jedem Submit hatte nur die zu späten Spiegel-Daten verdeckt. Standard jetzt 0 (`nvn_api_core.c`).

### 2026-11-02: Heiße Stücke bestätigt: kein Flackern, keine Einbrüche
- Maintainer: nichts flackert, alles da; ein kurzer Einbruch am Anfang (Laden), danach keiner. Minute 2: mean 30.0 fps, 1% low 27.6, kein Frame über 50 ms.
- Gespiegelt 368,7 MiB; abgegeben die beiden kleinen Pools (49,5 MiB); heiße Stücke 121 (7,6 MiB) nach Frame 600, 293 (18,3 MiB) nach Frame 1800; ~2470 Bindungen pro Frame auf heiße Stücke; late sync 113–482 Seiten/Frame; Verfolgungskosten ~0,4 ms/Frame.

### 2026-11-02: Nur kleine Pools abgeben reicht nicht; heiße 64-KiB-Stücke
- Mit Abgabe bis 64 MiB: 48 MiB + 1,5 MiB abgegeben, 368,6 MiB gespiegelt, keine bemerkten Einbrüche (1% low 28,6 ab Minute 2), aber es flackert wieder einiges; late sync 369 Seiten/Frame. Die späten CPU-Schreibzugriffe im 350-MiB-Pool sind also bildrelevant.
- Umgesetzt (`vk_memory.c`): heiße Stücke in großen gespiegelten Pools. Ein 64-KiB-Stück mit späten CPU-Schreibzugriffen in 2 Submissions wird heiß; ebenso Stücke, die die GPU über den importierten Puffer beschreibt (sonst wäre der Spiegel dort veraltet). Bindungen, die ein heißes Stück berühren, nehmen den importierten Puffer – außer die GPU hat in dieser Submission schon einen überlappenden Bereich in den Spiegel geschrieben (dann ist der Spiegel die einzige aktuelle Kopie bis zum Write-back). Neue Schnittstelle `vk_mem_use_n/pick_n` bzw. `VK_MEM_USE_N/PICK_N` mit Bindungsgröße (Storage/Uniform-Deskriptoren, Vertex-Puffer, Kopien, Fills, Uniform-Updates, Report-Schreibzugriffe, Textur-Kopien, Transform Feedback); ohne Größe gilt 1 MiB. Schalter `TOTK_MIRROR_HOT` (Standard 1). Zusammenfassung: „hot chunks N (X MiB), bindings to hot chunks Y“ pro Frame. Nur Syntax geprüft.

### 2026-11-02: Spiegel-Abgabe: kein Flackern, aber 350-MiB-Pool abgegeben
- Normalbetrieb mit `MIRROR_DEMOTE`: nichts flackert, alles da, 2 FPS-Einbrüche. Abgegeben: `0x5d3368000+48 MiB` (CPU_UNCACHED|GPU_UNCACHED), `0x5d2d10000+1.5 MiB`, `0x5f8a87000+350 MiB` (CPU_UNCACHED|GPU_CACHED) = 399,5 MiB; gespiegelt bleiben 18,5 MiB. Late sync danach 0,1 Seiten/Frame.
- Anpassung: Pools über `TOTK_MIRROR_DEMOTE_MAX` MiB (Standard 64) behalten ihren Spiegel und werden nur per später Synchronisation versorgt.

### 2026-11-02: verify mit später Synchronisation: CPU schreibt auch während der GPU-Ausführung; Spiegel-Abgabe
- `MEMORY=verify` (mit `MIRROR_LATE`): Palmen und große Bäume flackern dauernd (verify ändert das Timing durch zusätzliche Kopien je Submission, also kein Maßstab fürs Bild). Wichtig: weiterhin übersprungene Seiten (147 bzw. 348 je Zusammenfassung) bei 0 Abweichungen – die CPU schreibt Seiten noch nach der späten Synchronisation, also während die GPU die Submission ausführt (vermutlich andere Spiel-Threads; der Spielthread selbst wartet wegen SUBMIT_SYNC).
- Umgesetzt `MIRROR_DEMOTE` (Standard 1, `vk_memory.c`): Ein Pool, in dem die späte Synchronisation in 4 Submissions CPU-Schreibzugriffe findet, gibt beim nächsten `vk_mem_mirror_begin` seinen Spiegel ab (`mirror_forget`, `MS_REFUSED`, `vk_defer_buffer`) und wird ab dann importiert gelesen (wie auf der Switch: GPU sieht CPU-Schreibzugriffe beim Ausführen). Log der ersten 8 Abgaben mit Pool und Flags; Zusammenfassung zeigt „mirrors given up N (X MiB)“.

### 2026-11-02: Späte Spiegel-Synchronisation wirkt; Mirror wieder Standard
- `MEMORY=mirror` mit `MIRROR_LATE`: keine Einbrüche mehr (Maintainer), fast alles richtig; nur vereinzelt kleine Büsche/Gegenstände flackern in der Entfernung, bis man nah herangeht. Späte Synchronisation: 135 Seiten/Frame (erste Minute), 510 Seiten/Frame (Minute 3), also bis ~2 MiB, die die CPU während der Aufzeichnung schreibt. Kosten der Verfolgung 0,41–0,45 ms/Frame.
- `mirror_mode`: auto ist wieder mirror auf diskreten GPUs.

### 2026-11-02: verify: Spiegel stimmen, aber CPU-Schreibzugriffe während der Aufzeichnung kommen ein Bild zu spät; späte Synchronisation
- Test `SUBMIT_SYNC=0` (import): alles da, Einbrüche bleiben (1% low 14.7 fps). Das Warten ist also nicht die Ursache der Einbrüche; die GPU-Zeit über PCIe ist es.
- Test `MEMORY=verify`: stabil, flackert, alle Objekte da. Abgleich: 1,7 Mio. Seiten verglichen, **0 Abweichungen**, 369 übersprungen (seit der Synchronisation von der CPU beschrieben).
- Folgerung: Die Spiegel enthalten den Stand vom ersten Zugriff einer Submission (`mirror_sync`). Was die CPU danach während der Aufzeichnung schreibt, kommt erst eine Submission später an. Auf der Switch (und mit import) liest die GPU den Speicher erst beim Ausführen, also nach dem Submit.
- Umgesetzt `vk_mem_mirror_late` (`vk_memory.c`, aufgerufen in `vk_sync.c prologue` direkt vor dem Submit): Für alle in dieser Submission benutzten gespiegelten Pools neuer Write-Watch-Snapshot (eigene Epoche ab 0x80000000) und Kopie aller seit der Pool-Synchronisation beschriebenen Seiten in den Prolog. Schalter `TOTK_MIRROR_LATE` (Standard 1); Mirror-Zusammenfassung zeigt „late sync N pages“ pro Frame. Standard bleibt vorerst import, bis der Test mit `MEMORY=mirror` bestätigt.

### 2026-11-02: import-Standard: störende Einbrüche
- Maintainer: mit import kommen kurze FPS-Einbrüche regelmäßig, auf Dauer störend. Log: Minute 1 1% low 15.0 fps, 30 Frames über 50 ms; schlimmste Frames 15–24 ms fence/present wait. Storage-Verkehr ~480 MiB/Frame (gebundene Bereiche) aus CPU_UNCACHED-Pools über PCIe; mit SUBMIT_SYNC wartet der Spielthread jedes Bild auf die GPU, also schlägt die GPU-Zeit voll durch.
- Nächste Tests: (A) `SUBMIT_SYNC=0` mit import (Überlappung CPU/GPU zurück; prüfen, ob das Baum-Flackern ohne Spiegel überhaupt noch auftritt). (B) `MEMORY=verify` (Mirror-Modus mit Abgleich: Mismatches nennen Pool, Flags und Offset), um die Mirror-Fehlerquelle einzugrenzen (CPU→Spiegel veraltet oder GPU→Gast zu spät).

### 2026-11-02: Ursache für Flackern/fehlende Objekte: VRAM-Spiegel; import ist Standard
- Mit TEX_RECLAIM: Log zeigt keine Rückholung (Fall trat nicht ein). Zweite Palme rechts vom Spieler fehlt (Vergleich mit Switch-2-Screenshot), Felsen/Palmen flackern gelegentlich. ~105 indirekte Draws pro Frame mit index count 0 in dieser Gegend.
- `MEMORY=import`: **alles richtig** – kein Flackern von Objekten oder Schatten, alle Objekte da; index count 0 = 0. Ein- bis zweimal kurz niedrige FPS (unter 2 s). Minute 1 mean 29.0 / 1% low 20.3 (Laden), danach mean 30.0; schlimmste Frames mit 20–30 ms fence/present wait.
- `IND_SHOW=1` (Mirror): alles da, aber vieles flackert.
- Folgerung: Der Mirror-Modus (aus TotK, `vk_memory.c`) ist die Ursache. Die GPU schreibt in die VRAM-Kopie; zurück in den Gastspeicher erst am Ende der Submission. Violets CPU liest Culling-Ergebnisse und Draw-Argumente zurück und entscheidet damit; vermutlich sieht sie teils alte Daten (passt auch zu SPLIT_DRAWS-Befund vom 2026-11-01).
- `runtime/vk_memory.c mirror_mode`: auto = import auf allen Geräteklassen. `TOTK_MEMORY=mirror` bleibt verfügbar.

### 2026-11-02: Veraltete GPU-Texturen nach CPU-Neubeschreiben werden neu geladen (TEX_RECLAIM)
- Tests: `TEX_ALIAS=0` → Palmen und andere Objekte fehlen (leere Blatt-Texturen, Alpha-Test wirft alles weg). `TEX_VRAM_PRESSURE=0` → Palme wieder richtig, aber viele Bäume und Palmen flackern stark. Folgerung: Inhalte unserer Texturkopien sind veraltet; Verwerfen und frisches Laden aus dem Speicher hilft.
- Ursache im Code (`runtime/vk_texture.c`, aus TotK): Ein gespiegeltes Bild, das die GPU einmal beschrieben hat (`gpu_owned`, auch per Alias-Kopie), wurde nie wieder aus dem Speicher hochgeladen (`make_fresh*` brach sofort ab). Schreibt das Spiel danach eine neue Textur gleicher Form (Adresse, Format, Größe → gleicher Schlüssel, gleicher Eintrag) in diesen Speicher, bleibt das alte Bild (Rinde auf Palmblättern).
- Korrektur `reclaim_if_rewritten` (in `make_fresh_snap`, also bei Draw-Bindungen): Wurden die eigenen Seiten des Speichers nach dem GPU-Schreiben (`gpu_gen`) von der CPU geändert, gibt das Bild die GPU-Eigenschaft ab und wird neu hochgeladen (gleiche Entscheidung wie `vk_texture_write_back`). Nur ganze eigene Seiten zählen; Texturen ohne eigene Seite nur mit Bytekopie. Alias-Kopien übernehmen `gpu_gen` der Quelle; nach einem Write-back wird `gpu_gen` erneuert (eigener Write-back zählt nicht). Schalter `TOTK_TEX_RECLAIM` (Standard 1), Log beim ersten Mal.
- Nicht getestet (kein Spiel im Container); Syntax geprüft.

### 2026-11-02: Mit SUBMIT_SYNC: Palmen flackern vereinzelt; Palme mit Rindentextur auf den Blättern
- Statt Bäumen flackern jetzt vereinzelt Palmen schnell. Eine Palme zeigt auf den Blättern die Rindentextur, ohne Alpha-Ausschnitt (grobe Blattflächen); dieselbe Palme war vorher richtig. Verdacht: Textur veraltet/verwechselt (Speicher einer gestreamten Textur wiederverwendet, Alias-Übernahme `TOTK_TEX_ALIAS` oder Freigabe unter VRAM-Druck `TOTK_TEX_VRAM_PRESSURE`).
- Log: Ab ca. Frame 1800 springt „index count 0“ bei indirekten Draws von ~0 auf 68000–85000 pro 600 Frames (vorher in anderen Szenen höchstens ~20000). Noch nicht eingeordnet.
- A/B-Tests angefragt: `TEX_ALIAS=0`, `TEX_VRAM_PRESSURE=0`.

### 2026-11-02: Baum-Flackern ist ein Zeit-Wettlauf; SUBMIT_SYNC Standard
- `OCCLUSION=0` (alle Occlusion-Zählungen groß): einzelne Bäume flackern weiter stark → nicht die Abfrage-Ergebnisse.
- `SUBMIT_SYNC=1` (Spiel-Thread wartet nach jedem Submit auf die GPU): nichts flackert mehr, nur sehr weit entfernt (vermutlich normaler LOD-Wechsel). Bildrate unverändert: Minute 2 mean 30.0 fps, 1% low 28.4 (ohne Warten gleich); kein spürbarer Unterschied für den Maintainer.
- Folgerung: Die GPU liest Daten pro Objekt (vermutlich Storage-Puffer, die `vk_usnap` nicht kopiert) erst, nachdem das Spiel sie fürs nächste Bild überschreibt.
- `runtime/nvn_api_core.c`: `SUBMIT_SYNC` Standard 1, Log-Zeile beim ersten Submit. `TOTK_SUBMIT_SYNC=0` schaltet ab.

### 2026-11-02: Fang-Tutorial läuft; Zahlen der ganzen FDE-Funktionen
- Bestätigt: Tutorial-Kampf, weitere Story und Zwischensequenzen laufen ohne Absturz (über 30000 Frames, stabil 30 fps).
- lift: main 179525 Funktionen (vorher 180254), 161 innere Einsprünge, 4396 Sprungtabellen (vorher 4392), 27 all-label fallbacks (29), br via dispatch 24802 (18238; die 161 Kopien ganzer Funktionen zählen ihre BRs erneut), 983 MB C (939). subsdk0: 1 innerer Einsprung, 0 fallbacks (1). sdk unverändert. Kompilieren 1,5 min, exe 249 MiB (237).
- Neu gemeldet: einzelne Bäume flackern schnell ein/aus, auch aus der Nähe (nicht das alte Culling-Ploppen).

### 2026-11-02: Funktionen mit .eh_frame werden ganz gehoben
- Dritter Bericht: Die FDE-Funktion `main+0x2a1f54c..main+0x2a2016c` (776 Instruktionen) war in **45 Stücke** zerlegt. Fast alle Schnitte kamen aus der Sprung-Fixpunktschleife (B/B.cond/CBZ/TBZ aus derselben FDE), ausgelöst durch wenige echte Anfänge darin (u. a. `main+0x2a20150`, per ADRP+ADD aus `main+0x1527cb4` und 7 weiteren Stellen genannt). Der `BR x10` der Sprungtabelle liegt bei `main+0x2a1f588` im ersten Stück, die Fälle in anderen Stücken; ein Fall ohne direkten Sprung dorthin (`main+0x2a1fc24`) war kein Anfang → Absturz.
- Korrektur `recomp/gen.py`: Eine Funktion mit FDE wird als Ganzes gehoben, `[FDE-Anfang, erster Anfang ab FDE-Ende)`. Die Fixpunktschleife betrachtet Sprünge innerhalb dieses Bereichs als intern (`owner_ranges`). Ein Anfang strikt innerhalb (BL-Ziel, Adresse von außen) wird zu einer eigenen C-Funktion über denselben Bereich, die an ihrer Adresse einsteigt (`Emitter.function(entry, end, lo)`, nutzt den Alle-Labels-Modus mit `fb_<entry>`, ohne lokale Register/Flags). Statistik `inner_entries` in der gen-Ausgabe.
- Geprüft: Lifter-Differenztest grün; neu `test_lifter.py --inner` (gen.py `--inner-entry-test`: jede Funktion ab der zweiten als innerer Einsprung in einen Bereich ab der vorherigen) ebenfalls grün, 6400 Läufe. Synthetischer Discovery-Test: keine Kaskade, ADR von außen → innerer Einsprung mit FDE-Bereich.
- Risiken: Größere Funktionen können die Sprungtabellen-Auflösung (MAX_STEPS) überschreiten → mehr all-label fallbacks, längeres Kompilieren; viele innere Einsprünge verdoppeln Code. Zahlen aus der lift-Ausgabe beim Maintainer prüfen (vorher: main 29 / subsdk0 1 / sdk 16 all-label fallbacks).

### 2026-11-02: FDE-Korrektur greift hier nicht
- Nach `lift.py`: main verwirft 12 Adressen, subsdk0 1, sdk 0; der Absturz bleibt identisch (gleiche Register). `main+0x2a1fbfc` ist also aus einem anderen Grund ein Anfang (BL-Ziel, Seed oder Sprung-Fixpunkt), nicht durch ein ADR aus derselben FDE.
- `calls.py --refs` gibt jetzt zusätzlich für die FDE-Funktion jeden Funktionsanfang darin mit Grund aus (eh_frame, export, RELATIVE, BL/B/ADR/ADRP+ADD mit Quelle und ob die Quelle in derselben FDE liegt) und jeden `BR` darin mit Registerherkunft.

### 2026-11-02: Fang-Tutorial-Absturz: Ursache Sprungtabelle in zerschnittener Funktion, Korrektur
- Zweiter Bericht: `main+0x2a1fc24` liegt **innerhalb** der FDE `main+0x2a1f54c..main+0x2a2016c`. Register beim Absturz: x10 = Ziel, x9 = `main+0x3c61c1e` (ungerade, also eine Byte-Tabelle in .rodata), x30 unverändert der Aufrufer der Funktion. Also kein virtueller Aufruf an dieser Stelle, sondern ein `BR x10` einer Clang-Sprungtabelle (Anker + Byte-Offset × 4) in der großen Funktion. Die Funktionsfindung hatte die Funktion bei `main+0x2a1fbfc` zerschnitten (vermutlich das ADR des Tabellen-Ankers, das die 16-Instruktionen/ADD-Basis-Regel nicht erkannte); der Fall hinter dem Schnitt war für den ersten Teil „außerhalb“ und wurde dynamisch angesprungen.
- Korrektur `recomp/gen.py discover`: Eine FDE deckt genau eine Funktion ab. Mit ADR/ADRP+ADD erzeugte Adressen, die strikt innerhalb einer FDE liegen und von innerhalb derselben FDE genannt werden, sind keine Funktionsanfänge mehr. Seeds und BL-Ziele bleiben; die Sprung-Fixpunktschleife bleibt unverändert (sie fügt weiter Anfänge ein, wo ein Sprung einen echten Anfang kreuzt). `Module.eh_frame_ranges()` liest die FDE-Bereiche (pc_begin pcrel sdata4, mit Abgleich gegen die Tabelle). Neue Statistik `inside_fde_dropped` in der gen-Ausgabe und im Manifest.
- Geprüft: Lifter-Differenztest grün (21 FDEs = 21 Funktionen, nichts verworfen); synthetischer Test: Anker-ADR innerhalb der Funktion verworfen, ADR von außen in dieselbe Funktion bleibt ein Anfang.
- Beim Maintainer: `lift.py` (erzeugt alles neu) und `build_game.py`; in der lift-Ausgabe steht pro Modul, wie viele Adressen verworfen wurden.

### 2026-11-02: Fang-Tutorial-Absturz: erster Bericht
- Aufrufer `main+0xfc38dc` (+0x88): `x8 = [x19]; x8 = [x8 + 0x68]; blr x8`, ein virtueller Aufruf (vtable-Eintrag 13).
- Ziel `main+0x2a1fc24` liegt in der von der Funktionsfindung angenommenen Funktion `main+0x2a1fbfc` bei +0x28, direkt hinter einem unbedingten `B` (Tail-Sprung). Kein direkter Sprung, kein ADR/ADRP+ADD, keine RELATIVE-Relocation, kein .eh_frame-Anfang, kein 32/64-Bit-Wort mit dem Offset. Also wahrscheinlich eine eigene Funktion, deren Zeiger zur Laufzeit entsteht oder über eine andere Relocation-Art in die vtable kommt.
- Erweitert: `calls.py --refs` zeigt jetzt auch die überdeckende .eh_frame-FDE, ABS64/GLOB_DAT-Relocations mit lokalem Symbol (die Funktionsfindung nimmt nur RELATIVE) samt Zählung solcher Ziele, die keine Funktionsanfänge sind, und 32-Bit-PC-relative Wörter. `rt_dispatch`-Absturz loggt Register mit Moduladressen und Register, deren erstes Wort eine Moduladresse ist (Objekt → vtable).
- Kandidat für die Korrektur: Code direkt hinter einem unbedingten Sprung (B/BR/RET), den kein direkter Sprung erreicht, als Funktionsanfang werten. Vorsicht: Sprungtabellen-Fälle (BR-Ziele) sehen genauso aus; die müsste die Funktionsfindung vorher ausschließen.

### 2026-11-02: Patch 0010 bestätigt, Optimierer wieder Standard; Absturz im Fang-Tutorial
- Test `XLATE_DEBUG=0` mit Übersetzer 1ab104289df9b0f2 (Patch 0010): Compute-Shader 1 hat **mit** Optimierer 18 Ballots (23441 statt 38475 SPIR-V-Wörter), nichts ploppt. Standard jetzt `XLATE_DEBUG=0` (alle Stufen mit Optimierer, eigene Cache-Schlüssel ohne `debug=cs`).
- Neuer Absturz im Tutorial-Kampf (Schwächen und Fangen, nach der zweiten Text-/Bildtafel): `FATAL: rt_dispatch: no function at guest pc 0x7002a1fc24 (main+0x2a1fc24): not a function start (lr main+0xfc3968)`. lr ≠ pc, also kein setjmp/longjmp-Muster, sondern ein indirekter Sprung (BLR/BR) an eine Adresse, die die Funktionsfindung nicht als Funktionsanfang kennt. Hat nichts mit dem Übersetzer zu tun (CPU-Code).
- `tools/calls.py --refs`: listet, wo eine Adresse referenziert wird (direkte Sprünge, ADR/ADRP+ADD samt „von der Funktionsfindung als Sprungtabellen-Anker übersprungen“, RELATIVE-Relocations, .eh_frame), ob sie direkt hinter einem Aufruf liegt (Rücksprungadresse), und für BLR/BR an bzw. vor der Adresse, woher das Register stammt (Lade-Basis und Offset, Adressrechnung, Kopie). Nur Adressen, Offsets, Registernummern.

### 2026-11-02: Übersetzer-Patch 0010 (Subgroup-Operationen bleiben an Ort und Stelle); Vergleich mit Switch 2
- Ursache genau: `AstOptimizer.IsWorthPropagating` hält jede Operation mit einer Quelle für billig und kopiert sie in alle Verwendungen; `Ballot(pred)` hat eine Quelle → 18 → 54. DebugMode schaltet in Ryujinx nur diese Weitergabe ab (sonst nur Kommentare), der Patch deckt also genau den Fehler ab.
- `tools/xlate/patches/0010-keep-subgroup-operations-in-place-in-the-ast-optimizer.patch`: Ballot, Vote*, Shuffle* werden nie weitergegeben (weder kopiert noch verschoben); unbenutzt dürfen sie entfallen. Gilt für alle Stufen, also auch Grafik-Shader. `build_xlate.py` erwartet 10 Patches. Hier geprüft: alle 10 Patches gelten sauber; gebaut wird beim Maintainer (kein .NET im Agent-Container).
- Standard bleibt vorerst `XLATE_DEBUG=1`. Test: Übersetzer neu bauen, `run_with.py XLATE_DEBUG=0` → im Dispatch-Bericht muss Compute-Shader 1 „ballot 18“ zeigen und nichts darf ploppen. Wenn ja: Standard auf 0 (Optimierer überall an, eine Übersetzungsart für alle Stufen).
- Vergleich mit Switch 2 (Switch-2-Edition-Update, 60 fps): Bild stimmt bis auf Schatten (bewegen sich, Switch 2 hat vermutlich bessere Schatten) und weicheres Bild (Auflösung/Kantenglättung der Switch-2-Fassung) überein. 60 fps braucht eine Frame-Rate-Patch-Tabelle für Violet (Log: `frame rate: no patch table`), später eigenes Thema.

### 2026-11-02: UNSICHTBARE OBJEKTE BEHOBEN: Ryujinx-Optimierer verdreifacht Ballots im GPU-Culling
- `XLATE_DEBUG=1` (Compute-Shader im DebugMode, ohne AST-Optimierer): **Alles ist da**, nichts ploppt, die Kamera-Ausblendung verhält sich wie auf der Switch (Bestätigung des Maintainers mit Screenshot).
- Ursache: Compute-Shader 1 (GPU-Culling) hat im DebugMode 18 Ballots (19 Gruppen-Ops), optimiert 54 (55). Der AST-Optimizer propagiert Ballot-Ergebnisse in ihre Verwendungen; jede Kopie läuft an anderer Stelle mit anderen aktiven Lanes, das warp-aggregierte Zählen/Kompaktieren geht schief, das Culling meldet sichtbare Objekte als unsichtbar, und die CPU (liest Storage 1 und die Draw-Argumente zurück, `f_7000b88d48`) lässt deren Draws weg. „Instanzzahl über 1“ stieg von ~2800 auf ~80800 pro 600 Frames.
- Bestätigt mit Standard-Build ohne Schalter (Commit 5248d3b): alles sichtbar, nichts ploppt; Log zeigt `debug=cs`, Ballots 18, stabil 30 fps (1% low 28 fps), Prewarm ohne Neuübersetzung (Cache gefüllt). Offen im Log: `SetScissors: differing scissor 1 ignored`, `conservative rasterisation requested: drawn normally`.
- Lösung jetzt: DebugMode für Compute-Shader ist Standard (`TOTK_XLATE_DEBUG=0` schaltet ab). Eigene Cache-Schlüssel (`debug=cs`), erster Start übersetzt alle Shader neu (~3 s Prewarm).
- Später sauberer: Patch für den Ryujinx-Übersetzer, der Ballot/Vote/Shuffle (konvergenzabhängig) nie propagiert oder dupliziert (wie HasSideEffects in Patch 0003); dann wieder mit Optimierer. Prüfen, ob Grafik-Shader mit Ballots betroffen sind.
- Aufräumen bei Gelegenheit: Test-Schalter dieser Suche (OCCLUSION, OQ_*, IND_*, ARGS_FORCE, SSBO_WHOLE, VB_WHOLE, DEPTH_EQUAL, A2C, NO_DISCARD, RECONVERGE, CS_SHARED, SUBMIT_SYNC, ONE_CPU, READ_WATCH) und Diagnosen (F8-Snapshots, Dispatch-Bericht) sind harmlos (aus bzw. nur Log), können aber reduziert werden.

### 2026-11-02: Übersetzer-Hinweise leer; Test DebugMode für Compute-Shader
- Alle vier Compute-Shader: `"notes": []` – keine c1-Zugriffe außerhalb des Bereichs, keine unbekannten Handles.
- Test-Schalter `TOTK_XLATE_DEBUG=1`: Compute-Shader werden im DebugMode von Ryujinx übersetzt (ohne AST-Optimierer, der bei TotK schon ein Atomic verloren hatte, Patch 0003). In die Übersetzer-Identität gemischt (`debug=cs`): eigene Cache-Schlüssel, erster Start übersetzt alle Shader neu.

### 2026-11-01: Ausrichtung ausgeschlossen; Übersetzer-Hinweise der Compute-Shader
- Storage-Adressen von Compute-Shader 1 liegen auf 32-Byte-Grenzen (+224/+192/+128 mod 256); keine Meldung über nicht ausgerichtete Storage-Bindungen in irgendeiner Stufe. Ausrichtungs-Verdacht widerlegt.
- Neu: Der Dispatch-Bericht gibt die Hinweise des Übersetzers für jeden Compute-Shader aus (`vk_exec: compute shader N translator "notes": [...]`), z. B. c1-Lesezugriffe außerhalb des Bereichs (die dann 0 liefern und als Konstanten in den Shader eingebaut werden) oder unbekannte Handles. `gfx_shader_notes()` neu in gfx_shader.c.

### 2026-11-01: ARGS_FORCE bringt Objekte zurück; Verdacht Ausrichtung der Storage-Buffer
- `ARGS_FORCE=1` (95–146 Instanzzahlen pro Submission von 0 auf 1): Großer Baum, viele Orangenbäume, Büsche und Gräser erscheinen, die vorher fehlten. Bewiesen: Compute-Shader 1 (GPU-Culling) rechnet sichtbare Objekte als unsichtbar; die CPU übernimmt das. Es ploppt weiter (die CPU liest auch Puffer 0 / Storage 1).
- Beobachtung: Ein Baum ist sichtbar, wenn man links davon steht, nie von rechts. Passt zu verschoben gelesenen Hüllen (Bounding-Daten), nicht zu einer falschen Kamera.
- Verdacht: Der Ausweg für minStorageBufferOffsetAlignment > 4 (NVIDIA 16; Entwicklung lief auf RX 9070 ohne ihn): Ryujinx' Unaligned-Storage-Pfad + Bindung der abgerundeten Adresse. Diagnose: Bericht der Compute-Dispatches zeigt jetzt die Adresse jedes Storage-Buffers modulo 256; Log `storage binding of stage N at an address X bytes past a 16-byte boundary` (je Stufe 3 Mal).

### 2026-11-01: Bestätigt: Die CPU liest die Ergebnisse des GPU-Cullings zurück
- `READ_WATCH=1` + `symbolize.py --log`: Spielcode `f_7000b88fc0` liest Puffer 0 (Storage 1 von Compute-Shader 1, 256 KiB) und `f_7000b89f1c` liest Puffer 2 (die indirekten Draw-Argumente, 256 KiB); beide aufgerufen von `f_7000b88d48`. Weitere Zugriffe: unsere Runtime (`vk_exec_op_draw`, `ov_memcpy`).
- Folgerung: Die CPU entscheidet mit den Ergebnissen des GPU-Cullings (vermutlich den Instanzzahlen) aus dem vorigen Frame, welche Objekte sie überhaupt zeichnet. Rechnet Compute-Shader 1 falsch (0 statt sichtbar), fehlen die Objekte samt ihren direkten Draws.
- Beweistest: `TOTK_ARGS_FORCE=1` setzt nach jeder Submission Instanzzahl 0 → 1 in allen bekannten Argument-Datensätzen (Gastspeicher). Erscheinen Zaun/Palme/Büsche, ist Compute-Shader 1 (bzw. seine Eingaben) die Ursache.

### 2026-11-01: Lesewächter für die Ausgaben der Compute-Shader
- Neu: `TOTK_READ_WATCH=1`. Storage-Buffer ≥ 64 KiB, die ein Dispatch schreibt (GPU-Culling: 256 KiB, 1 MiB, Draw-Argumente), werden nach Abschluss der Submission unzugänglich gemacht (höchstens alle 30 Frames); der erste Zugriff je Seite wird mit Befehl und Rücksprungadressen in karpur.exe geloggt (`vk_exec: read watch: buffer N … accessed at karpur+… called from …`), dann läuft der Zugriff weiter. Windows: Vectored Exception Handler `read_watch_veh` (platform/win32.c), Linux ohne Funktion.
- `tools/symbolize.py --log <log> --grep "read watch"` ersetzt alle karpur+0x… einer Logzeile durch Symbolnamen (f_<Gastadresse> = Spielcode).
- Zweck: Kandidat 1 prüfen (CPU liest Ergebnisse des GPU-Cullings). Zugriffe aus der Runtime selbst (Argumente lesen, Rückschreiben) erscheinen auch; Spielcode erkennt man an f_70….

### 2026-11-01: Kein Thread-Wettlauf; Stand der Suche und nächster Plan
- `ONE_CPU=1` (~15 fps) und `ONE_CPU=2`: gleiche fehlende Objekte, Ploppen unverändert. Wettlauf zwischen CPU-Threads ausgeschlossen.
- `MapPhysicalMemory … failed 0x10801` beim Start ist normal: das Spiel holt Speicher bis zur Grenze (Pool 0xCD500000 = Retail). Zelda-spezifische Eingriffe (ov_zstd, LOD guard) sind für Purpur nicht aktiv bzw. greifen nicht.
- Stand: Die CPU des Spiels schickt die Draws normaler Objekte (Zaun, Palme, Büsche, Bäume, Töpfe) nicht ab; nur Objekte in der Kamera-Ausblendung werden gezeichnet. Ausgeschlossen sind alle getesteten Grafik-Ursachen, GPU-Abfragen, Zeitpunkte, Speicher-Spiegel, Wettläufe.
- Verbleibende Kandidaten: (1) Die CPU liest Ergebnisse des GPU-Cullings (Compute-Shader 1 schreibt Storage 1, 256 KiB) zurück und entscheidet damit; falsche Ergebnisse von Shader 1 → Objekte fehlen. (2) Ein falsch übersetzter CPU-Befehl (SIMD) in der Sichtbarkeitsprüfung.
- Plan: Rücksprungadressen (LR) der NVN-Draw-Aufrufe für ein bestimmtes Mesh aufzeichnen (Spiel-Code-Adressen, keine Spieldaten), um die Funktion zu finden, die entscheidet; dann deren Eingaben verfolgen. Außerdem prüfen, ob die CPU Bereiche liest, die Compute-Shader 1 schreibt.

### 2026-11-01: Vergleich mit der echten Switch; Test auf Wettlauf zwischen CPU-Threads
- Vergleichsbilder (gleiche Stelle, Switch vs. KarPur): Bei uns fehlen auch **entfernte** Objekte dauerhaft: Palme, großer Baum, zwei von drei weißen Büschen, roter Topf, der komplette blaue Zaun. Auf der Switch wird ein Baum nah an der Kamera leicht durchsichtig (gepunktet); bei uns erscheint er **nur** in genau dieser Kamera-Ausblendung und verschwindet bei der kleinsten Bewegung wieder.
- Deutung: Die Sichtbarkeitsprüfung des Spiels auf der CPU sortiert normale Objekte fälschlich aus; Objekte in der Kamera-Ausblendung umgehen sie und werden gezeichnet. GPU-Abfragen und Zeitpunkt der Ergebnisse sind ausgeschlossen (OCCLUSION=0, OQ_FILL, SUBMIT_SYNC wartet auch auf die Abschluss-Callbacks).
- Verdacht: Wettlauf in einer mehrfädigen Sichtbarkeitsprüfung (Atomics/Exklusivzugriffe der Kern-Nachbildung) oder ein falsch übersetzter SIMD-Befehl. Test-Schalter `TOTK_ONE_CPU=<n>`: Prozess nur auf n logischen Prozessoren (ab dem ersten Present).

### 2026-11-01: Korrektur: Das Spiel lässt die Draws selbst weg (CPU-Entscheidung)
- F8 mit Layouts, 9 Aufnahmen (ungerade = Baum sichtbar, gerade = Kamera zum Baum, Baum weg): Beim Hinschauen werden die großen Meshes (26499/20628/17364/12960 … Indizes, Layouts 23/24/34) **nicht abgeschickt**, ohne Ersatz; beim Wegschauen wieder. Gleiche Layouts und Zustände in beiden Fällen. Die frühere Deutung „LOD0 unsichtbar“ war falsch herum.
- Folgerung: Die Grafik-Übersetzung ist hier nicht schuld; die CPU des Spiels lässt das Objekt weg (Kamera nah und blickt darauf). Vermutung: Kamera-Hindernis-Ausblendung (Objekt zwischen Kamera und Figur), die auf der Switch weich/gepunktet sein könnte und hier sofort ganz verschwindet (Zeit/Fade-Berechnung?), oder eine andere CPU-seitige Eingabe.
- Offen: Vergleich auf der echten Switch (gleicher Baum, Kamera dazwischen; Beet und Topf an der Tür sichtbar?).

### 2026-11-01: LOD0-Modelle haben einen anderen Vertex-Aufbau
- F8 mit Indextyp und Puffergrößen (alle Indizes 16 Bit): Die beim Hinschauen gezeichneten detailreichen Modelle (20628/16014/12225/10239 … Indizes) haben alle Vertex-Puffer 1 mit **760 Bytes**; die einfachen Modelle beim Wegschauen 2696/2780/9564 Bytes. `VB_WHOLE=1` änderte nichts, die Größe ist es nicht.
- Werkzeug zeigt jetzt das Vertex-Layout der gelisteten Meshes: `vk_exec: layout N: shader inputs …; aK fmt … sS +Offset; stream S stride … divisor …` (Attribut K = Location K des Vertex-Shaders).

### 2026-11-01: Ursache eingegrenzt: Die detailreichsten Modelle (LOD0) sind unsichtbar
- F8-Vergleich mit Zustand: „not issued“ und „newly drawn“ haben dieselben Zustände (Tiefe GREATER + Schreiben, Stencil, 4 Farbziele, teils A2C). Es ist ein **LOD-Tausch**: Beim Hinschauen ersetzt das Spiel 6270/2553-Index-Modelle durch 25359/18462/12288-Index-Modelle, beim Wegdrehen umgekehrt. Die hochdetaillierten Modelle kommen unsichtbar heraus.
- Nächster Test: `TOTK_VB_WHOLE=1` (Vertex-Puffer bis zum Ende des Speichers binden; robuster Vertex-Abruf liefert sonst Nullen hinter der Bindung). Snapshot zeigt jetzt Indextyp (0 Byte, 1 16 Bit, 2 32 Bit, 9 ohne Index) und die Größen der Vertex-Puffer 0/1.

### 2026-11-01: F8-Vergleich: Die CPU des Spiels schickt die Draws nicht mehr ab
- Kamera vom Baum weg (Blätter sichtbar) → zum Baum hin (weg): pro Vergleich 13–34 Meshes „now not issued“ (auch direkte indizierte Draws, z. B. 11805/3930/2694 Indizes), nur 0–2 „now without instances“, dafür 16–37 „newly drawn“. Das GPU-Culling ist es nicht; das Spiel wechselt auf der CPU zu anderen Draws, sobald das Objekt zwischen Kamera und Figur steht (Kamera-Ausblendung; auf der Switch gepunktet durchsichtig).
- Verdacht: Die neuen Draws der Ausblendung kommen unsichtbar heraus. Das Werkzeug zeigt jetzt für „not issued“ und „newly drawn“ den Zustand: Bits von `state` (0 Tiefentest, 1 Tiefe schreiben, 2–5 Tiefenfunktion, 6 Stencil, 7 A2C, 8 A2C-Dither, 9 Fragment-Shader, 10–11 Cull, 12–19 Blend-Maske, 20–23 Farbziele, 24 Raster-Discard, 25 Tiefenziel), Kanalmaske, Blend-Zustand von Ziel 0, Shader-Kennung.

### 2026-11-01: EQUAL-Tiefentest nicht die Ursache; Vergleichswerkzeug F8
- `DEPTH_EQUAL=0`: Das Spiel nutzt EQUAL, aber die Objekte ploppen weiter. Bisher ausgeschlossen: übersprungene Draws, Vertex-Formate, Ersatztexturen, Vorpass, GPL, Verdeckungsabfragen (Wert, Größe, GPU-seitig), Datenwettläufe, Spiegel, Shared Memory, Grenzprüfung, Rekonvergenz, A2C, Discard, EQUAL.
- Neues Werkzeug: **F8** im Spielfenster nimmt den nächsten Frame auf (Meshes: Indexpuffer/erster Index/Anzahl/Vertex-Offset → Instanzen); das zweite F8 vergleicht mit dem vorigen: `vk_exec: snapshot N vs N-1: drawn in both …; drawn before, now without instances …; now not issued …` plus die 8 größten je Gruppe. Zeigt, ob ein fehlender Baum vom Culling 0 Instanzen bekommt, gar nicht gezeichnet wird oder gezeichnet, aber unsichtbar ist.

### 2026-11-01: Pixelanzahl ebenfalls nicht die Ursache; Test Tiefentest EQUAL
- `OQ_CLAMP=1`: Objekte bleiben nah/mittig unsichtbar. Zusammen mit `OCCLUSION=0`: Die Abfrage-Ergebnisse steuern diese Objekte nicht. Größte Zählwerte ~0,5–0,9 Mio. (Box über das ganze 960×540-Bild, Vorder- und Rückseite).
- Neuer Verdacht: Tiefen-Vorpass + Farbpass mit Tiefentest EQUAL; minimal andere Tiefe im Farbpass → keine Farbe (Objekt verdeckt trotzdem). Der frühere `DEPTH_ONLY=0`-Test erfasste nur Vorpass-Draws ohne Fragment-Shader.
- Test-Schalter `TOTK_DEPTH_EQUAL=0`: EQUAL wird als GREATER_OR_EQUAL gezeichnet (Reverse-Z). Log `vk_pipeline: a draw tests depth EQUAL` zeigt, ob das Spiel EQUAL überhaupt nutzt.

### 2026-11-01: Nur Zeitstempel und Samples-Passed; Verdacht: Pixelanzahl steuert LOD/Ausblendung
- Zählerarten in 1800 Frames: Typ 0 (Zeitstempel) 1801, Typ 1 (Samples-Passed) 736999 (~400/Frame), Typ 16 (Zeitstempel oben) 1802. Kein bedingtes Zeichnen, kein RenderEnable.
- Neue Deutung: Das Spiel wertet die **Anzahl** der Pixel aus (Detailstufe oder Ausblenden nah/mittig), nicht nur 0/≠0. Passt zu allen Beobachtungen: nah + mittig → große Zahl → weg; Bildrand/fern → kleinere Zahl → sichtbar; `OCCLUSION=0` schrieb riesige Werte → kein Effekt. Mögliche Ursache: unsere Zählung ist größer als die der Switch (Einheit/Zählweise).
- Test-Schalter `TOTK_OQ_CLAMP=<n>`: Zählerwerte über n werden als n gemeldet. Occlusion-Bericht zeigt jetzt auch „of 10000 samples or more“ und „largest“.

### 2026-11-01: A2C/Discard nicht die Ursache; Hinweis von yuzu auf Zählerabfragen
- `A2C=0` (das Spiel fordert A2C mit Dither an, 1 Sample) und `NO_DISCARD=1`: Baum verschwindet weiter beim direkten Hinschauen aus der Nähe, sichtbar am Bildrand und aus der Ferne. Die Objekte werden also gar nicht gezeichnet (Culling/LOD), nicht weggeblendet.
- Web: yuzus Fortschrittsbericht August 2023 nennt für Karmesin/Purpur behobene Detailstufen und Baum-Flackern (nur mit GPU-Genauigkeit „Hoch“) durch die Überarbeitung des Query-Caches (Zählerabfragen, Host Conditional Rendering). Seite aus der Sitzung nicht abrufbar (yuzu-mirror.github.io nicht auflösbar).
- Diagnose: `vk: frames …: counter reports by type: … ; conditional render: rendered/skipped/undecided; render enable on/off` (alle 1800 Frames, dreimal). Alle Zählerarten außer Samples-Passed schreibt die Wiedergabe als 0 – liest das Spiel eine davon, sieht es 0.

### 2026-11-01: Ausblendung nahe der Bildmitte (Kamera-Fade) verdächtig
- `RECONVERGE=1` (Erweiterung verfügbar und aktiv): ploppt weiter.
- Beobachtung des Maintainers: Ein Baum nah an der Kamera ist sichtbar, solange er am Bildrand steht (Blätter oben mit Punktmuster), verschwindet, wenn man ihn direkt anschaut, und ist aus der Ferne wieder da. Passt zu einer Kamera-/Distanz-Ausblendung per Alpha (Dither) bzw. Discard, die bei uns zu stark greift.
- Test-Schalter: `TOTK_A2C=0` (Alpha-to-Coverage aus; NVN kann A2C dithern, Vulkan mit 1 Sample verwirft alles unter 0,5) mit Bericht `vk_pipeline: alpha to coverage requested (dither …, samples …)`; `TOTK_NO_DISCARD=1` (OpKill → OpReturn in Fragment-Shadern).

### 2026-11-01: SSBO_WHOLE ohne Wirkung; Test maximale Rekonvergenz
- `SSBO_WHOLE=1`: ploppt weiter; Grenzprüfung der Storage-Buffer ist nicht die Ursache.
- Neu: `VK_KHR_shader_maximal_reconvergence` (+ subgroup uniform control flow) wird aktiviert, wenn der Treiber es anbietet (Log `vk: maximal reconvergence …`). Test-Schalter `TOTK_RECONVERGE=1`: Shader mit Subgroup-Operationen bekommen `SPV_KHR_maximal_reconvergence` und `MaximallyReconvergesKHR` (vk_pipeline.c `reconverge`). Hintergrund: Ballot → Atomic des gewählten Lanes → Shuffle braucht dieselben aktiven Lanes; Maxwell rekonvergiert explizit, Vulkan ohne den Modus nach Ermessen des Treibers.

### 2026-11-01: Culling-Shader zählt mit Ballot + Atomic + Shuffle; Test ohne Grenzprüfung
- SPIR-V-Zählung Shader 1: 18 atomare Additionen, 54 Ballots, 18 Aufrufe der Shuffle-Hilfsfunktion (Ryujinx ShufflePass), keine Barrieren: Warp-aggregiertes Zählen/Kompaktieren. Shader 2–4 ohne Atomics.
- Unterschied zu Ryujinx/Switch: Wir binden Storage-Buffer exakt in der Spielgröße, und robustBufferAccess (alle Features an) liefert Nullen jenseits der Bindung; Maxwell prüft keine Grenzen. Test-Schalter `TOTK_SSBO_WHOLE=1`: nur gelesene Storage-Buffer bis zum Ende ihres Speichers binden.
- Weitere Kandidaten, falls das nicht hilft: Rekonvergenz bei Ballot/Shuffle (VK_KHR_shader_maximal_reconvergence), unsere Übersetzer-Patches 0001–0004.

### 2026-11-01: Compute-Shader 1 ist das GPU-Culling
- Bericht: Shader 1 (120 Gruppen × 64, im Feld je Frame) liest Storage 0 (32784 B) und 2 (4 MiB), schreibt 1 (256 KiB), 3 (1 MiB, kompaktierte Instanzliste) und 4 (256 KiB, **die indirekten Argumente**). Keine Textur: Sichtfeld-/Abstands-Culling, kein Hi-Z. Shader 2–4: je 1 Gruppe × 32, viele kleine Storage-Buffer (Vorbereitung).
- Datenwettläufe sind ausgeschlossen (SUBMIT_SYNC, MEMORY=import), Verdeckung auch. Verdacht: Übersetzung von Shader 1 (Atomics zum Zählen/Kompaktieren, Global-Memory-Zugriffe über Hilfsfunktionen). Bericht `vk_exec: compute shader N SPIR-V: …` zählt Funktionen, Aufrufe, Atomics, Gruppen-Ops, Barrieren (nur Zahlen).

### 2026-11-01: Shader lesen die Abfrage-Ergebnisse nicht; Verdeckung endgültig ausgeschlossen
- `OQ_FILL=1`: ploppt weiter. Berichte liegen in 9–10 Seiten; kein Compute- und kein Storage-Zugriff darauf, nur ~0,5 Uniform-Bindungen pro Frame (Nachbarn auf derselben Seite). Verdeckungsabfragen sind nicht die Ursache.
- Nächste Diagnose: `vk_exec: compute shader N dispatch M: …` (ab Frame 900, je Shader 3 Dispatches): Gruppen, Local-Size, gelesene Texturen/Images (Größe, Format, „occlusion depth“/„depth“), Storage-Buffer (Größe, geschrieben, „covers indirect draw arguments“). Zeigt, ob ein GPU-Culling ein Tiefenbild liest und die Argumente schreibt.

### 2026-11-01: Kein Wettlauf nach dem Submit; Verdacht: Zeichen-Shader lesen Abfrage-Ergebnisse im selben Frame
- `SUBMIT_SYNC=1` (auch mit `MEMORY=import`): Objekte ploppen unverändert. Datenwettlauf ausgeschlossen.
- Erklärung, die zu allen Tests passt: Ein Vertex-/Fragment-Shader liest das Samples-Passed-Ergebnis auf der GPU im selben Befehlspuffer (auf der Switch schreibt die GPU den Bericht der Reihe nach) und blendet das Objekt bei 0 aus. `OCCLUSION=0` wirkt erst nach dem Fence über die CPU, daher ohne Effekt.
- Genauere Diagnose: Seiten (4 KiB) mit Berichten als Menge; Zeile `samples-passed reports in N pages; bindings touching those pages: compute storage/uniform, draws storage/uniform` (UBOs vor dem Snapshot gezählt). Sicherer GPU-Test `TOTK_OQ_FILL=1`: jeder Bericht wird zusätzlich per vkCmdFillBuffer auf der GPU mit einem Nicht-Null-Wert gefüllt.

### 2026-11-01: Shared Memory nicht die Ursache; Verdacht Daten-Wettlauf nach dem Submit
- Mit `CS_SHARED=49152` ploppt es weiter. Nur 2 Compute-Programme: Local-Size 32 bzw. 64 (+0x748), das zweite hat 0x600 bei +0x760 (vermutlich Shared-Memory-Größe 1536 Bytes, unter Ryujinx' Standard 4096). Das Feld +0x760 notiert, Schalter bleibt als Test.
- Der Zähler „written by a shader“ ist wohl verfälscht (note_written-Bereiche sind ganze SSBO-Bindungen, auch von Fragment-Shadern).
- Neuer Verdacht nach dem Muster des TotK-„LOD guard“ (nvn_api_core.c): Das Spiel schreibt Objektdaten (Storage-Buffer, ~150–400 MiB Bindungen pro Frame aus CPU_UNCACHED) kurz nach dem Submit neu; die Switch-GPU hat sie bis dahin gelesen, unsere GPU läuft erst nach der Wiedergabe. Uniform-Snapshots decken nur UBOs ab. Test-Schalter `TOTK_SUBMIT_SYNC=1`: jeder Submit wartet auf die GPU.

### 2026-11-01: OCCLUSION=0 hilft auch mit gl_BaseInstance nicht; Verdacht Shared Memory der Compute-Shader
- `OCCLUSION=0` (jetzt mit gl_BaseInstance): Objekte ploppen weiter. Die Tiefe der Prüf-Boxen ist plausibel (pro Frame gelöscht, skalierte Kopie aus 1920x1080 D24S8, ein Vollbild-Draw schreibt Tiefe).
- Fund: Compute-Shader werden immer mit Shared-Memory-Größe 0 übersetzt (`answers_for` setzt `shared_bytes` nie; TotK holte sie aus dem gepaarten PC-Shader). Ryujinx deklariert dann 4096 Bytes; braucht das Culling/Kompaktieren mehr, liegen Zugriffe außerhalb des Arrays → falsche Instanzzahlen/-listen.
- Test-Schalter `TOTK_CS_SHARED=<Bytes>` (z. B. 49152, Gerätegrenze). Bericht `vk_exec: compute control N, words from +0x720: …` (24 Wörter um die Local-Size bei +0x748), um das echte Feld zu finden.

### 2026-11-01: Compute liest die Abfrage-Ergebnisse nicht; ~1000 Abfragen pro Frame, 95 % „verdeckt“
- `buffer bindings over them: compute 0` in allen Fenstern: kein Compute-Shader liest die Ergebnisse (die „draws“-Treffer kommen von der großen Spanne von 0x5652b00 Bytes). Verdacht OQ_GPU vom Tisch.
- Das Spiel macht ~1000 Samples-Passed-Abfragen pro Frame (Boxen gegen die 960x540-Tiefe, Reverse-Z GREATER), davon 90–97 % mit 0. Die CPU entscheidet pro Objekt. `OCCLUSION=0` wurde nie zusammen mit dem gl_BaseInstance-Fix getestet: erneut testen.
- Diagnose `vk_exec: frames …: occlusion depth target: clears …, draws writing depth …, copies in …, scaled copies in …`: woher die halbe Tiefe kommt (falsch gefüllte Tiefe → fast alles „verdeckt“).

### 2026-11-01: gl_BaseInstance allein behebt das Ploppen nicht
- Übersetzer gebaut (neue ID `1170d8f8…`), Objekte ploppen weiter (Zaun, Büsche je nach Kamera). Barriere zwischen Compute und indirektem Draw ist voll (ALL→ALL), daran liegt es nicht.
- `OQ_GPU=1` stürzte im Startmenü ab (Gerätefehler „write invalid at 0“, kurz nach `occlusion query slot 1 not available after its fence`). Erst die Diagnose ohne Schalter auswerten; OQ_GPU nutzt jetzt VK_MEM_PICK wie die anderen Kopien (Ursache des Absturzes nicht sicher).
- Neuer Verdacht: Der Culling-Compute-Shader liest die Samples-Passed-Ergebnisse direkt auf der GPU (auf der Switch schreibt die GPU sie der Reihe nach in den Speicher). Die Wiedergabe schreibt sie erst nach dem Fence von der CPU, für den Shader zu spät. Diagnose: `vk_exec: frames …: samples-passed reports span …; buffer bindings over them: compute N, draws M`. Test-Schalter `TOTK_OQ_GPU=1`: Berichte mit genau einer Abfrage schreibt zusätzlich die GPU (`vkCmdCopyQueryPoolResults`).

### 2026-11-01: Indirekte Argumente kommen von einem Compute-Shader → gl_BaseInstance
- Zählung: **alle** indirekten Argumente schreibt ein Shader im selben Befehlspuffer vor dem Draw (GPU-Culling mit Kompaktierung). Die erste Instanz ändert sich also je nach Sichtbarkeit pro Frame; der Wert, den die Wiedergabe beim Aufzeichnen aus dem Speicher liest, ist veraltet. Darum ploppen Objekte mit Abstand und Kamera.
- Fix im Übersetzer: `QueryHasConstantBufferDrawParameters() => true` (Ryujinx `DrawParametersReplace`): Vertex-Shader lesen c0+0x640/0x644/0x648 als `gl_BaseVertex`/`gl_BaseInstance`/`gl_DrawID`, die GPU nimmt den Wert direkt aus den Argumenten. Neuer Übersetzer = neue Übersetzer-ID = Shader-Cache wird neu aufgebaut. `shaderDrawParameters` ist eingeschaltet (vk_device.c schaltet alle gemeldeten Features ein).

### 2026-11-01: Verdeckungsabfragen sind nicht die Ursache des Ploppens
- `OCCLUSION=0`: Objekte ploppen weiter. Die Prüf-Draws sind Boxen (36 Indizes), Tiefentest GREATER (Reverse-Z) ohne Schreiben, halbe Auflösung 960x540: plausibel.
- Neue Verdachte: Distanz-/LOD-abhängiges Culling, eventuell auf der GPU (Compute schreibt Instanzzahlen in die indirekten Argumente) oder über eine CPU-Rücklesung (z. B. Tiefenpuffer → Puffer, deren Kopie übersprungen wird). Diagnose: Zähler „written by a shader“ in der Indirekt-Zeile; dazu alle `(logged once)`-Warnungen ansehen.

### 2026-11-01: Objekte ploppen je nach Abstand/Kamera (Verdeckungsabfragen verdächtig)
- `IND_CPU=1`, `IND_SHOW=1`, `MEMORY=import`: Objekte sind da, ploppen aber je nach Abstand und Kamerawinkel ein und aus (bei `IND_SHOW` am meisten sichtbar). ~95 % der Samples-Passed-Berichte sind 0. Typisch für falsche Verdeckungsabfragen; der frühere `OCCLUSION=0`-Test lief noch vor dem Basis-Instanz-Fix und zählt nicht.
- Diagnose: `vk_exec: occlusion-counted draw N: …` (Zustand der gezählten Prüf-Draws: Tiefentest, Ziel, Culling, Clipping, bis 12 verschiedene). Occlusion-Bericht nur noch die ersten drei Male, dann jedes 16. Mal.

### 2026-11-01: Weiterhin viele unsichtbare Objekte (Topf, Büsche, Zäune)
- Nach dem Basis-Instanz-Fix fehlen weiter Objekte mit Kollision. Fehlende NVN-Zeichenfunktionen sind ausgeschlossen (würden mit `rt_fatal` abbrechen).
- Die Hälfte der indirekten Draws hat in der CPU-Sicht Instanzzahl 0. A/B-Schalter: `TOTK_IND_CPU=1` zeichnet mit den Argumenten aus CPU-Sicht (vkCmdDrawIndexed statt Lesen aus dem GPU-Puffer, prüft veraltete GPU-Kopie/Spiegel), `TOTK_IND_SHOW=1` zeichnet zusätzlich Instanzzahl 0 als eine Instanz (prüft, ob die fehlenden Objekte vom Culling stammen). Dazu `MEMORY=import` (keine Spiegel).

### 2026-11-01: Basis-Instanz bei indirekten Draws (Verdacht für unsichtbare Objekte)
- `OCCLUSION=0`: Objekte bleiben unsichtbar, Verdeckungsabfragen sind nicht die Ursache (das Spiel nutzt sie stark, ~4096 Berichte in wenigen Sekunden, viele mit 0, das ist normal).
- Indirekte Draws: ~437 pro Frame, **alle** mit erster Instanz ≠ 0 (Argumente schreibt die CPU, die Hälfte mit Instanzzahl 0 = Culling des Spiels). Die Wiedergabe gab dem Shader aber immer Basis-Instanz 0 (c0+0x644). GLSLC-Shader addieren sie zum Instanzindex, um ihre Instanzdaten zu finden, also hat jedes Objekt die Daten eines anderen gelesen.
- Behoben: die erste Instanz wird beim Aufzeichnen aus den Argumenten gelesen und nach c0 geschrieben. A/B-Schalter `TOTK_IND_BASE=0` (altes Verhalten).
- Ergebnis: Gemüse im Beet vor dem Haus ist jetzt sichtbar, Töpfe an der Tür ebenfalls. Noch zu prüfen: ob die ursprünglich gemeldete Vase und das Blumenbeet da sind und ob weitere Objekte fehlen (Vergleich mit Konsole).

### 2026-11-01: Tiefen-Vorpass und GPL ausgeschlossen; Verdacht Verdeckungsabfragen
- `DEPTH_ONLY=0` und `GPL=0`: kein verstecktes Objekt taucht auf. Vorpass/EQUAL-Tiefe und der GPL-Weg sind nicht die Ursache. Auch keine `draws in frames`-Zeile, also schaltet auch kein `RENDER_COND` Draws ab.
- Neuer Verdacht 1: Das Spiel liest Samples-Passed-Zähler (Verdeckungsabfragen) auf der CPU zurück und blendet Objekte mit Zähler 0 aus (wie TotK). Bericht: `vk: first samples-passed report …` und alle 4096 Berichte `vk: occlusion: … of those 0`. A/B-Schalter `TOTK_OCCLUSION=0`: jeder Bericht meldet „sichtbar“ (wachsender großer Wert).
- Neuer Verdacht 2: Indirekte Draws (`DrawElementsIndirect`). Die Wiedergabe gibt dem Shader immer Basis-Instanz 0, die Argumente können eine andere erste Instanz haben; dann liest der Shader falsche Instanzdaten. Bericht alle 600 Frames: `vk_exec: indirect indexed draws …` (Instanzzahl 0, erste Instanz ≠ 0 usw.).

### 2026-11-01: Keine unbekannten Vertex-Formate; Verdacht Tiefen-Vorpass
- Keine `vertex attribute ... read as zeros`-Meldung: die Formate sind nicht die Ursache.
- Neuer Verdacht: Tiefen-Vorpass (Draws ohne Fragment-Shader, erst seit Binden nach Stufen gezeichnet) und Farbpass mit Tiefentest EQUAL. Vorpass-Pipelines werden ganz und mit Ein-Set-Layout gebaut, Farbpass-Pipelines mit Libraries/unabhängigen Layouts und Vertex-Ausgabe-Union – minimal andere Tiefe lässt EQUAL scheitern, das Objekt verschwindet (Kollision bleibt).
- A/B-Schalter `TOTK_DEPTH_ONLY=0`: Draws ohne Fragment-Shader wieder überspringen (außer TF). Test zusätzlich mit `GPL=0` (alle Pipelines auf demselben Weg).

### 2026-11-01: Fehlende Objekte werden nicht übersprungen
- Zählung: in mehreren Minuten kein einziger übersprungener Draw (keine `draws in frames`-Zeile). Die Draws laufen, die Objekte sind trotzdem unsichtbar.
- Verdacht: Vertex-Attribut in einem NVN-Format ohne Eintrag in `vtx_formats` → liest lautlos Nullen (Positionen in einem Punkt). Diagnose in `vk_pipeline.c vertex_input`: jedes solche Format einmal. Weitere Kandidaten danach: GPU-Culling (indirekte Draws mit Anzahl 0), Instanz-Divisor.

### 2026-11-01: UMGEBUNGSLICHT KORREKT; Zählung übersprungener Draws
- Mit Cube-kompatiblen 2D-Arrays: Schattenbereiche, Kleidung, Innenräume und Metall sehen normal aus; keine Ersatz-Textur-Meldung mehr.
- Offen: einzelne fehlende Objekte (Vase mit Pflanze vor dem Haus: Pflanze schwebt, Topf fehlt; Blumenbeet unsichtbar, blockiert aber).
- Diagnose `vk_exec_draw.c skip_tick/skip_note`: übersprungene Draws nach Grund (Render aus/Bedingung, leerer Scissor, Shader wartet, Shader fehlt, Pipeline wartet, Pipeline fehlgeschlagen), Summe über 600 Bilder, höchstens 30 Zeilen.

### 2026-11-01: Ursache des fehlenden Umgebungslichts: Cube-Sampler auf 2D-Array
- Diagnose: genau eine Kombination: Shader-Ressource Typ 0x5 (Cube) auf ein 2D-Array-Bild 32×32, 6 Ebenen, 1 Stufe, View mit Ebenen 0+6 → Ersatz gebunden. Die Umgebungslicht-Würfeltextur wird als 2D-Array angelegt und als Würfel gelesen.
- Fix: `vk_texture.c` legt quadratische 2D-Arrays mit 6n Ebenen zusätzlich CUBE_COMPATIBLE an (`VkTexImage.cube_ok`), außer auf AMD (TotK-Kommentar: Layoutfehler im AMD-Treiber); `get_view` erlaubt dann Cube-Views.
- Die fehlenden Vasen erklärt das vermutlich nicht (keine weitere Ersatz-Meldung); offen.

### 2026-11-01: Dunkle Schatten/schwarzes Metall seit Anfang; Diagnose für Ersatz-Texturen
- Maintainer: Die zu dunklen Schattenbereiche und die schwarze Dunstabzugshaube gibt es seit dem ersten Bild, unabhängig von `SPLIT_DRAWS`, `MEMORY`, `GPL` (A/B ohne Unterschied). Die gl_Layer-Ausnahme (whole_only 2) hat nichts geändert, bleibt aber (schadet nicht).
- Verdacht: Ein Shader bekommt statt einer Textur den Ersatz (`tex_info_full`, 1×1, Nullen) – lautlos, wenn `get_view` den Typ ablehnt (z. B. Cube-Sampler auf ein nicht als Cube angelegtes Bild) oder die Textur-ID nicht registriert ist. Nullen ergäben fehlendes Umgebungslicht/Spiegelungen (Cubemap) und mit Alpha 0 verworfene Objekte (fehlende Vasen, Blumenbeet).
- Diagnose `vk_exec_draw.c`: meldet jede neue Kombination (Shader-Ressourcentyp, Bildtyp, View-Typ, Ebenen) mit Größe/Ebenen/Stufen und View, sowie nicht registrierte Textur-IDs.

### 2026-11-01: gl_Layer-Draws sind Cubemap und Farbtabelle, nicht Schatten
- Diagnose: die Draws mit gl_Layer-Vertex-Shader sind Vollbild-Dreiecke in 64×64 × 6 Ebenen (Umgebungs-Cubemap) und 32×32 × 32 Ebenen (3D-Farbtabelle), mit Fragment-Shader, 8 Farbzielen; Gerät meldet shaderOutputLayer 1.
- Neue Deutung der „zu dunklen Schatten“: fehlendes Umgebungslicht (Cubemap schwarz/falsch) – alles ohne direkte Sonne dunkel, Metall (Dunstabzugshaube im Spielerhaus) schwarz. In einem früheren Lauf mit `TOTK_GPL=0` sahen Kleidung und Figuren normal aus.
- Umgesetzt: `whole_only()` gibt für Vertex-Shader mit gl_Layer 2 zurück: Ein-Set-Layout, keine Libraries, keine Vertex-Ausgabe-Union – wie mit `TOTK_GPL=0`.
- Maintainer: einige Vasen fehlen (nicht alle), dazu das unsichtbare Blumenbeet. Noch keine Spur.

### 2026-11-01: Wasser korrekt; Schatten nutzen gl_Layer
- Wasser sieht nach der Tiefen-Stencil-Kopie normal aus.
- Diagnose: mindestens 3 Vertex-Shader schreiben `gl_Layer` (Schatten-Kaskaden in eine Array-Textur in einem Durchgang); keine Meldung zu mehreren Viewports. Schatten weiter zu dunkel, unsichtbare Objekte weiter unsichtbar; keine „skipped“-Meldungen dazu.
- Neue Diagnose `vk_exec_draw.c`: die ersten 6 Draws mit gl_Layer-Shader melden Pass-Größe und -Ebenen, Tiefenziel (Ebenen, Format), Viewport, Scissor, Tiefenbereich, Polygon-Offset, ob ein Fragment-Shader gebunden ist; dazu die Gerätefeatures shaderOutputLayer/ViewportIndex.
- Andere Recomp-Projekte (alle MIT, in `refs/` zum Lesen): letsgo-recomp (raulzrrs, LAS-Recomp-Basis), KirbyRecomp, primerecomp. Keines setzt Transform Feedback um; letsgo zeichnet nur einlagig (layerCount 1). Für Schatten keine direkte Vorlage gefunden.

### 2026-11-01: Neue Bildfehler-Liste: Schatten, unsichtbare Objekte, Wasser
- Maintainer-Befund (Startgebiet): Schatten zu dunkel/falsch (Kleidung, Fassade des Anwesens, Figuren in Zwischensequenz fast schwarz), teils sehr pixelig; ein Blumenbeet blockiert, ist aber unsichtbar; Wasser dunkelblau ohne Durchsicht.
- Schatten: Seit dem Binden nach Stufen werden Draws ohne Fragment-Shader (Schattenkarten) gezeichnet statt übersprungen; vorher fehlten Schatten ganz. Verdacht: Kaskaden über mehrere Viewports/Scissors oder gl_Layer in einem Durchgang; die Wiedergabe setzt nur Viewport 0 und einen Scissor. Diagnose eingebaut: `vk_pipeline.c note_vs_builtins` (Vertex-Shader schreibt gl_Layer / gl_ViewportIndex), `vk_exec.c` meldet SetViewports mit unterschiedlichen Viewports.
- Wasser: Kopien von `DEPTH24_STENCIL8`-Texturen (Format 0x35) wurden übersprungen (`copy of format 0x35 texture ... skipped`), Wasser liest vermutlich die kopierte Szenentiefe. `vk_exec_xfer.c sub_resolve(..., image_only)`: Textur→Textur-Kopien ignorieren jetzt die Byte-Layout-Prüfung und kopieren Tiefe+Stencil zusammen.
- Unsichtbares Blumenbeet: noch offen; nächster Lauf sammelt alle „skipped/ignored/failed“-Meldungen.

### 2026-11-01: Baum-Flackern: Aufteilen der Command Buffer abgeschaltet
- A/B-Ergebnisse: ohne Schalter Bäume flackern stark; `MEMORY=import` kein Flackern; `VK_RECORDER=0` wie ohne; `TEX_ALIAS=0` noch stärker; Memo-Caches aus: gelegentlich Büsche/Blumen; `SPLIT_DRAWS=0` kein Flackern und auch der „Matsch-Baum“ korrekt. Schatten flackerten in diesem Lauf nicht mehr.
- Folgerung: Zusammenspiel von VRAM-Spiegeln und Teil-Submits. Vermutung: Pro Teil lädt der Spiegel-Prolog CPU-geschriebene Bereiche hoch, Rückschreiben von GPU-Ergebnissen erst am Ende der Submission – GPU-Schreibzugriffe (z. B. Compute für Vegetation) eines Teils werden vom Prolog des nächsten überschrieben.
- Umgesetzt: `TOTK_SPLIT_DRAWS` Standard 0 (vorher 64), Kommentar in `vk_exec.c`. Die eigentliche Ursache in `vk_memory.c` (Spiegel-Prolog pro Teil) ist nicht behoben; erst angehen, wenn die GPU-Startverzögerung messbar stört.
- `TEX_ALIAS=0` verschlimmert: Textur-Aliase („neues Bild übernimmt Inhalt“) sind nötig, bleiben an.

### 2026-11-01: Flackern – A/B-Tests mit vorhandenen Schaltern
- Schatten und Bäume flackerten schon vor den TF-Arbeiten (frühe Läufe). Kandidaten: Daten, die das Spiel für das nächste Bild überschreibt, während die (bei uns spätere) GPU-Ausführung sie noch liest (TotK hatte das bei Uniforms: `LOD_GUARD`, `vk_usnap`); VRAM-Spiegel (`MEMORY=mirror`), Recorder-Thread, Textur-Aliase, Memo-Caches, Aufteilung der Command Buffer.
- `tools/run_with.py NAME=WERT …`: startet karpur.exe mit Schaltern nur für diesen Lauf (TOTK_-Präfix wird ergänzt).
- Testreihe beim Maintainer: ohne Schalter; `MEMORY=import`; `VK_RECORDER=0`; `TEX_ALIAS=0`; `BIND_SKIP=0 SHADER_MEMO=0 PIPE_MEMO=0 TEX_MEMO=0 PUSH_SKIP=0`; `SPLIT_DRAWS=0`. Jeweils: flackern Schatten / Bäume?

### 2026-11-01: Stabil mit Figuren
- Mit Ein-Set-Layout für Pipelines ohne Fragment-Shader: kein Absturz mehr (GPL an), Spielerin, NPCs und Pokémon normal, 30 fps, Menüs, Karte, Speichern. Offen: Schatten und Bäume flackern.

### 2026-11-01: Treiberabsturz erklärt: leeres Fragment-Set im unabhängigen Layout
- Paketbericht: Absturz in Op 47 = `vkCmdPushDescriptorSetWithTemplateKHR` (davor SetStencilReference/WriteMask/CompareMask), für die reine Vertex-Pipeline des TF-Durchgangs. Mit GPL an nutzt jede Pipeline ein INDEPENDENT_SETS-Layout (Set 0 Vertex, Set 1 Fragment); ohne Fragment-Shader ist Set 1 leer. Der NVIDIA-Treiber liest dann beim Push von Set 0 Adresse 0.
- Fix (`vk_pipeline.c` `whole_only`): Pipelines ohne Fragment-Shader bekommen das einfache Ein-Set-Layout und werden immer ganz gebaut (keine Libraries, kein Fast-Link); XFB-Pipelines werden ebenfalls ganz gebaut. Reine Vertex-Draws gibt es erst seit dem Binden nach Stufen (vorher übersprungen) – betrifft evtl. auch Tiefen-/Schattendurchgänge.

### 2026-11-01: Absturz im Recorder-Thread
- `symbolize.py` (Map mit bildrelativen Adressen, Basis 0): karpur+0xc8a6207 = `rec_main+0x5e7` (der Recorder führt ein Paket aus; Aufruf eingebettet), darüber `thread_main`, darunter unser `crash_veh`.
- `vk_rec.c` merkt sich das laufende Paket (Op-Nummer laut Enum, erste drei Argumentwörter) und die sechs davor; `plat_fault_extra` gibt das bei einem Treiberfehler mit aus.

### 2026-11-01: Treiberfehler lokalisiert (Adressen), Bäume ohne TF
- Lauf mit GPL: `access violation at nvoglv64.dll+0xedca72, thread "vk recorder", reading address 0`, aufgerufen von karpur+0xc7a4607 ← +0xc8a6207 ← +0xc7a293e (Runtime-Code, nicht geliftet). Lesen von Adresse 0 im Treiber deutet auf ein NULL-Handle oder einen NULL-Zeiger in einem vkCmd-Aufruf.
- `tools/symbolize.py`: ordnet karpur+0x…-Adressen über `build/game/karpur.map` Symbolen zu.
- Lauf ohne GPL: nur zwei TF-Layouts (12/12/12 und 12/4/4), Bäume nutzen also kein TF. Der verformte Baum (Klumpen) steht neben einem korrekten: eher LOD/Impostor oder Compute-Ergebnis, eigenes Thema.

### 2026-11-01: Absturz trotz GPL-Ausnahme für XFB-Shader
- Mit `uses_xfb()` (keine Libraries für XFB-Pipelines) stürzt es weiter ab, diesmal **vor** „draw 1: binding the buffers“, also während `rec_drain` die vorher eingereihten Befehle (Pipeline binden, Deskriptoren, dynamischer Zustand) im Recorder-Thread ausführt. Mit `TOTK_GPL=0` lief es. Unterschiede bei GPL an außer den Libraries: unabhängige Set-Layouts (`P.indep`, INDEPENDENT_SETS, Vertex-Ausgabe-Union), Vertex-Set per Push. Neu ist dabei: Draws mit reinem Vertex-Programm (vorher ohne Programm übersprungen).
- `platform/win32.c`: Fehler in anderen Modulen (Treiber) werden jetzt (erste 4) auf stderr gemeldet: Modul+Offset, Thread-Name, Rücksprungadressen in karpur.exe. Nächster Lauf soll zeigen, welcher Aufruf in den Treiber führt.

### 2026-11-01: FIGUREN SICHTBAR – Transform Feedback läuft
- Lauf A (`TOTK_XFB=0`): Absturz nach dem Hauptmenü beim Laden, auch ohne TF-Aufnahme. Lauf B (`TOTK_GPL=0`, mit Aufnahme): läuft stabil, **Spielerin, NPCs und Pokémon mit Modellen und Animationen sichtbar**, 30 fps. Die Schrittlogs zeigen: Binden, Begin, Draw, End laufen durch.
- Folgerung: Der Treiber (NVIDIA 621.01) stürzt ab, wenn ein Vertex-Shader mit TransformFeedback-Capability durch Pipeline-Libraries (GPL) geht. `vk_pipeline.c`: `uses_xfb()` (Capability 53 im SPIR-V); solche Pipelines nehmen weder Stage-Libraries noch Fast-Link, nur `create_graphics`.
- Neu sichtbar: Bäume als verformte grüne Klumpen mit schwarzen Flächen (vorher flackernd). `vk_xfb.c` loggt jetzt jedes neue TF-Layout (bis 16) mit den Komponenten-Bytes, um zu sehen, ob Bäume ein anderes Layout haben.

### 2026-11-01: Absturz im Treiber beim ersten TF-Draw
- Erster Lauf mit echtem TF: Übersetzer baut, Smoke-Test ok, alle 490 Shader neu übersetzt. Layout dekodiert wie erwartet (`[0: stride 12, 3 components from 0x20] [1: … 0x24] [2: … 0x28]`). Direkt nach `tf: draw 1 captures into 3 buffers` endet der Prozess ohne Meldung: Der Absturz liegt außerhalb von karpur.exe (der Crash-Reporter meldet nur Fehler in unserem Modul), also im Treiber.
- Eingebaut zum Eingrenzen: `TOTK_XFB=0` schaltet die TF-Aufnahme ab (Binden nach Stufen und XFB-Shader bleiben), Schrittlogs für die ersten 2 TF-Draws (binden, beginnen, gezeichnet, beendet). Vorhandene Schalter: `TOTK_GPL=0` (keine Pipeline-Libraries), `TOTK_VK_RECORDER=0` (kein Recorder-Thread).

### 2026-11-01: Transform Feedback und Programm-Binden nach Stufen umgesetzt
- Layout-Bytes der ersten 4 TF-Shader: außer den bekannten Stellen nur `6e8..6ea = 01 01 01`, `6ec = 03`, `6ed = 01` (vermutlich Puffer-aktiv und Pufferanzahl 3) – sonst nichts. Bereiche: Attribut je Ausgabe bei 0x0dc + 0x80·b + j, (vermutlich) erste Komponente bei 0x2dc + …, Komponentenzahl bei 0x4dc + …, Strides u16 bei 0x6dc. Die drei 0x200-Bereiche schließen lückenlos an die Strides an. Annahme: Attributwert 0x20 + k = generisches Attribut k (Puffer 0: Position xyz, 1/2: Normale/Tangente).
- `runtime/nvn_xfb.h`: dekodiert das Layout in Maxwell-Attributwörter je Komponente (0x20 + 4k + c), wie Ryujinx sie erwartet.
- `gfx_shader.c`: Vertex-Shader mit Layout bekommen im Job `"xfb": [{"stride", "locs"}] × 4`. `tools/xlate/Program.cs` beantwortet damit `QueryTransformFeedbackEnabled/VaryingLocations/Stride` → SPIR-V mit Xfb-Decorations. Übersetzer muss neu gebaut werden.
- `vk_xfb.c` neu:
  - Programm-Binden nach Stufenmaske (`vk_prog_bind`); vor jedem Draw/Dispatch reicht `vk_prog_select` das passende Programm an `vk_exec_draw.c` weiter (Spielprogramm oder zusammengesetztes `prog_t`).
  - Compute-Binds (0x20) überschreiben die Grafikstufen nicht mehr.
  - Bei aktivem TF: Puffer binden (Offsets selbst mitgezählt, keine Zählerpuffer), `vkCmdBeginTransformFeedbackEXT`, Draw, `vkCmdEnd…`. Die Puffer gelten als GPU-geschrieben (Mirror-Rückschreiben).
  - Danach folgt beim TF-Ende eine volle Barriere.
  - Aufrufe über Funktionszeiger nach `rec_drain`.
- Die Diagnose (Layout-Bericht) ist entfernt.

### 2026-11-01: TF-Layout im Control-Teil gefunden (teilweise)
- Bestätigt: Purpur bindet für TF erst ein reines Vertex-Programm (Maske 0x1), dann `BindProgram(NULL, 0x2)` (Fragment lösen). Andere Masken: 0x1f (alle Grafikstufen), 0x20 (Compute). Das Back-End muss die Stufenmaske umsetzen (siehe nächste Schritte).
- Layout-Bericht (4 TF-Shader, 58 normale): nur in TF-Shadern gesetzt sind
  - +0x0dc / +0x15c / +0x1dc (Abstand 0x80): Werte 0x20 / 0x21 / 0x22 – vermutlich Varying-Location-Bytes je Puffer (Maxwell-Attributadresse/4: 0x20 = generisches Attribut 0, Komponente x). Noch unklar, wo die Bytes je Puffer beginnen.
  - +0x4dc / +0x55c / +0x5dc (Abstand 0x80): 3/3/3 bzw. 3/1/1 – Varying-Anzahl je Puffer.
  - +0x6dc: u16[4] Strides (0x000c000c, 0x0000000c → 12/12/12; beim 12/4/4-Shader 0x0004000c, 0x4). Passt genau zu den Puffergrößen.
  - +0x6e8 = 0x10101, +0x6ec = 0x103: noch unbekannt (Streams? Puffer-aktiv-Maske?).
- `vk_xfb.c` loggt jetzt für die ersten 4 TF-Shader alle Bytes ≠ 0 in 0x0c0–0x2e0 und 0x4c0–0x700, um den Beginn der Location-Arrays zu finden.

### 2026-11-01: TF-Draws ohne gebundenes Programm – Stufenmaske von BindProgram
- Dritter Lauf: Die TF-Draws laufen mit `ex->prog == NULL` und Rasterizer-Discard an. Vertexzahlen passen genau zu den Puffergrößen: 88 Vertices → 0x420 = 88 × 12 Bytes, 173 → 0x81c, 797 → 0x255c (hier alle drei Puffer je 12 Bytes pro Vertex; in einem früheren Lauf 12/4/4, das Layout hängt also vom Shader ab).
- Ursache (Vermutung, Lauf soll es bestätigen): `nvnCommandBufferBindProgram(cb, prog, stages)` bindet nur die Stufen in der Maske; Stufen in der Maske, die das Programm nicht hat, werden gelöst (prog darf NULL sein); andere Stufen bleiben. Purpur löst so für TF den Fragment-Shader und behält den Vertex-Shader. `vk_exec_draw.c` ersetzt bei jedem Bind alle Stufen (`ex->prog = b->prog`), ignoriert die Maske. Folge: TF-Draws werden übersprungen (`a draw without a bound program is skipped`). Möglicherweise trifft das auch andere Draws (z. B. Compute-Bind zwischen Grafik-Draws) und erklärt Bildfehler.
- `vk_xfb.c` verfolgt jetzt die Vertex-Stufe selbst und loggt jede neue Kombination (Programm ja/nein, Stufenmaske).
- Nächster Schritt nach dem Lauf: Stufenmaske im Back-End umsetzen. Idee ohne großen Umbau: bei Teil-Binds ein zusammengesetztes `prog_t` (Host-Objekt, Stufen aus mehreren Programmen, eigene Serial, zwischengespeichert nach Serial-Paar) als `ex->prog` setzen.

### 2026-11-01: TF-Diagnose ins Abspielen verlegt
- Zweiter Lauf: Bei jedem `BeginTransformFeedback` war im selben Command Buffer kein Vertex-Shader gebunden („without a known vertex shader“, 4 von 4). Das Programm kommt also aus einem früheren Command Buffer (NVN-Zustand gilt über Command Buffer hinweg) oder wird erst nach Begin gebunden. Auffällig im selben Lauf: `vk: a draw without a bound program is skipped`.
- Deshalb sind die TF-Aufrufe jetzt echte Records, und `vk_xfb.c` wertet beim Abspielen aus, wo der gebundene Shader sicher bekannt ist: Jeder Draw meldet seinen Vertex-Shader (`vk_xfb_note_draw`). Draws bei aktivem TF loggen die ersten 6 Male Programm, Stufen-IDs, Rasterizer-Discard, Vertexzahl und Puffergrößen. Der Layout-Bericht (bei 1, 4, 16 TF-Shadern) vergleicht jetzt den ganzen Control-Teil (0x880 Bytes).
- `nvn_probe.c` prüft die drei neuen Ops.
- Nächster Schritt: Maintainer-Lauf, `tf:`-Zeilen auswerten.

### 2026-11-01: Erster TF-Diagnoselauf: kein Layout-Bericht, aber Puffergrößen
- Kein `tf: layout report`: Der zuletzt gebundene Vertex-Shader wurde pro Host-Thread gemerkt (`_Thread_local`), Gastthreads/Fibers wechseln aber die Host-Threads. Jetzt pro Command Buffer gemerkt (Tabelle mit 256 Plätzen); ein `BeginTransformFeedback` ohne bekannten Vertex-Shader wird geloggt.
- Puffer je TF-Durchgang: drei Puffer, Größen immer im Verhältnis 3:1:1 (0x255c/0xc74/0xc74 = 797 Vertices × 12/4/4 Bytes; 0x81c/0x2b4/0x2b4 = 173 × 12/4/4). Vermutung: Puffer 0 Position (3 × float, Stride 12), Puffer 1 und 2 je ein 32-Bit-Wert (gepackte Normale/Tangente, Stride 4). Passt zu GPU-Skinning.
- **Kein `DrawTransformFeedback`**: Das Spiel liest die erfassten Puffer als normale Vertex-Puffer in späteren Draws. Weil TF nichts schreibt, stehen dort Nullen oder alte Daten: Figuren unsichtbar oder als „Stacheln“, je nach Speicherinhalt (Maintainer sieht jetzt Stachel-Figuren und stark flackernde Bäume, die vorher fehlten). Für die Umsetzung reicht also: Bind, Begin/End mit Zählerpuffer, XFB im Pipeline-Shader; die Draw-Variante ist zweitrangig.
- Begin/End laufen auf mehreren Command Buffern gleichzeitig (verschachtelte Control-Adressen).

### 2026-11-01: Diagnose für das Transform-Feedback-Layout
- `nvn_api_more.c`: Jedes `nvnCommandBufferBindProgram` merkt sich den Vertex-Shader-Control-Teil (Stufe 0 bei +0x714). Vertex-Shader, nach deren Bindung kein `BeginTransformFeedback` kommt, gelten als „normal“: ihre gesetzten 32-Bit-Wörter (unter +0x700) werden in eine Maske ODER-verknüpft. Die bei `BeginTransformFeedback` gebundenen werden kopiert (bis 16). Bei 1, 4 und 16 verschiedenen TF-Shadern loggt `tf: layout report` die Wörter, die in allen TF-Shadern und in keinem normalen gesetzt sind (mit Werten der ersten drei: kleine Zahlen wie Strides/Locations), dazu die, die nur in einigen TF-Shadern gesetzt sind.
- Die TF-Einstiegspunkte loggen ihre ersten Aufrufe (Puffer-Index/Adresse/Größe, Control-Adressen, Draw-Modus). Sie bleiben No-ops.
- Nächster Schritt: Maintainer lässt das Spiel bis zu sichtbaren Figuren laufen und schickt die `tf:`-Zeilen. Daraus Offsets für Varying-Locations und Strides ableiten (Maxwell: pro Puffer Stride, Anzahl, Stream; danach Location-Bytes je Puffer, 128 pro Puffer wäre plausibel).

### 2026-11-01: Ton läuft, Figuren unsichtbar
- AudioOut über SDL: **Musik und Geräusche sind zu hören** (`audout: default audio output opened (... to the default playback device)`). Purpur spielt seinen Ton also über AudioOut.
- Der Maintainer kann herumlaufen, NPCs ansprechen (Texte, Sprechblasen), durch das Haus (Kinderzimmer, Küche, Wohnzimmer) und nach draußen gehen (Weg zur Akademie, Minikarte). Stabil 30 fps, mehrfaches automatisches Speichern.
- Die Spielerfigur und Figuren sind jetzt unsichtbar (vorher verzerrt): passt zum Verdacht, dass sie über Transform Feedback gezeichnet werden, das ignoriert wird. Dazu kurze Stachel-Artefakte bei Effekten, Flackern/schwarze Flächen, Schattenfehler draußen.
- Stand für TF: `tools/xlate/Program.cs` beantwortet die TF-Anfragen des Ryujinx-Übersetzers noch nicht (Standard: aus). Das Vulkan-Back-End aktiviert VK_EXT_transform_feedback bereits und gibt Puffern die TF-Usage-Flags.

### 2026-11-01: M4 erledigt – das Spiel läuft bis ins Feld
- Mit den Transform-Feedback-No-ops kommt der Maintainer durch die ganze Einführung: Sprachwahl, Namenseingabe (Software-Tastatur: `swkbd ... -> "Melody"`), Charaktererstellung (Frisur, Gesicht; Figur stark verzerrt), Einschreibungsbildschirm (flackert, Figur fehlt), Zwischensequenzen in der Akademie-Bibliothek und vor der Akademie (Umgebung, Pikachu und einige Figuren sichtbar, Schatten und viele NPCs fehlen), dann ins Feld; Hauptmenü lässt sich öffnen. Spielstand wird gespeichert (`fs commit save: 1 files, 4.2 MiB`).
- Stabil mit 30 fps (Purpurs eigene Rate, `frame rate: no patch table, shipping 30 fps`) über ~26.000 Frames; kein Absturz, Fenster vom Maintainer geschlossen. Einzelne Ruckler beim ersten Übersetzen neuer Shader (bis 1,3 s bei Frame 12218).
- Probleme: Spielerfigur als riesige verzerrte Flächen, NPCs fehlen (Verdacht: GPU-Skinning über Transform Feedback, das noch ignoriert wird), Flackern, keine Schatten, **kein Ton** (`audout: ... silent`: AudioOut war aus TotK nur als stumme Ausgabe übernommen).
- Umgesetzt: AudioOut spielt jetzt über einen eigenen SDL-Stream auf dem Standardgerät (16 Bit stereo, 48 kHz); Datenzeiger über die SDK-Funktion `GetAudioOutBufferDataPointer`. Die Puffer-Taktung über die Host-Uhr bleibt.

### 2026-11-01: ERSTES BILD – Sprachauswahl wird gerendert
- Mit dem MapMemory-Fix aus .bss läuft der Start durch. **Das Fenster öffnet sich (1920x1080, Mailbox, 250-Hz-Monitor, Bildrate auf 60 begrenzt) und zeigt Purpurs Sprachauswahl** („In welcher Sprache möchtest du spielen?“, Deutsch … Traditionelles Chinesisch, Auswahlbalken, Farbverlauf). Alle Shared Fonts werden korrekt dargestellt. Der Controller bedient das Menü.
- Log: Startlogo beendet (`oe: the game finished its startup logo`), 600 Frames ohne Fehler, 33 Shader-Antworten aufgezeichnet, Pipelines über Graphics Pipeline Library. Lokalisierte Archive `appli/data/.../*_ger.arc` werden per `GetEntryType` gesucht und nicht gefunden (sie liegen in `data.trpfs`; das Spiel kommt damit zurecht). `save0:/main` und `save0:/poke_trade` fehlen (neuer Spielstand).
- Nach der Sprachwahl erscheint kurz das nächste Menü (Intro: `player_select`, `demo_intro_*`), dann Abbruch: `NVN nvnCommandBufferBindTransformFeedbackBuffer is not implemented` (auf zwei Threads gleichzeitig).
- Umgesetzt: die sieben Transform-Feedback-Funktionen als geloggte No-ops (`nvn_api_more.c`); `DrawTransformFeedback` zeichnet nichts. Was das Spiel aus erfassten Vertices zeichnet (vermutlich Partikel), fehlt dadurch vorerst.

### 2026-11-01: Ursache des CreateThread-Abbruchs: MapMemory aus .bss
- Mit dem entdoppelten svc-Log steht direkt vor dem Abbruch `svc 0x4 failed with 0xdc01 (x1=0x70046fd000 x2=0x2000)`: MapMemory des Vibrations-Thread-Stacks in die Stack-Region. Das SDK verschiebt jeden Thread-Stack dorthin (Schutzseiten) und bricht ab, wenn das scheitert.
- Ursache: `memory.c stack_move` nahm als Quelle nur Heap und Alias-Region. Purpurs Stack liegt in main-.bss. Behoben: der beschreibbare Teil (.data/.bss) jedes geladenen Moduls ist ebenfalls eine zulässige Quelle.

### 2026-11-01: svc-Fehlerlog, erste Ergebnisse
- `svc 0x2c` MapPhysicalMemory 0x10801 (bekannt, das Spiel macht weiter). Sehr häufig `svc 0x34` WaitForAddress und `svc 0x35` SignalToAddress mit 0xFA01 (InvalidState: Wert hat sich geändert, erwartet) und einmal `svc 0x17` ResetSignal mit 0xFA01 (nicht signalisiert, erwartet). Diese haben die 100 Zeilen aufgebraucht, bevor der CreateThread-Abbruch kam.
- Geändert: jede Kombination aus svc, Ergebnis und Ort wird nur einmal geloggt.

### 2026-11-01: Argumente des Vibrations-Threads gültig
- `nn::os::CreateThread(ThreadType 0x70046fc460 (main-.bss, genullt), Funktion main+0x783fc4, arg 0, Stack 0x70046fd000, Größe 0x2000, Priorität 16, x6 0x10)` vom Thread mit TLS `0x7010060000`. Stack 4-KiB-ausgerichtet, Größe Vielfaches von 4 KiB, Priorität im nn::os-Bereich: die Argumente sind in Ordnung.
- Also scheitert etwas im SDK-Inneren (Funktion direkt vor `sdk+0x3783c0`), vermutlich ein svc mit Fehler-Result. Neu in `svc.c`: jeder svc mit Fehler-Result in x0 wird geloggt (Nummer, Ort, Ergebnis, x1–x3; ohne Timeout/Cancelled und svcs, deren x0 kein Result ist; höchstens 100 Zeilen).

### 2026-11-01: CreateThread-Abbruch kommt vom Vibrations-Thread
- `calls.py`: `sdk+0x3783c0` ist der Anfang der Funktion *nach* `ThreadManager::CreateThread` (der Thread-Start: Thread-Funktion, TLS-Destruktoren, Broadcast). Die Rücksprungadresse zeigt dorthin, weil `CreateThread` mit einem nie zurückkehrenden Abbruch endet: eine Eingangsprüfung schlägt fehl. `sdk+0x16ba20` ruft `nn::diag::detail::VAbortImpl`.
- Aufrufer `main+0x7836fc` (216 Instruktionen) richtet die Vibration ein: `hid::GetNpadStyleSet`, `GetNpadJoyAssignment`, `GetVibrationDeviceHandles`, `InitializeVibrationDevice`, dann `nn::os::CreateThread` (+0x2e0), `InitializeTimerEvent`, `StartThread`, `StartPeriodicTimerEvent`.
- Kandidaten für die Prüfung: Stack-Ausrichtung (4 KiB), Stack-Größe, Priorität. `watch.c` loggt jetzt x0–x6.

### 2026-11-01: CreateThread-Abbruch ist eine SDK-Prüfung
- Mit erweitertem Prioritäts-/Kernbereich: keine `CreateThread refused`-Zeile, derselbe Abbruch. Die Break-Meldung trägt `result 0`: eine Bedingung im SDK (kein fehlgeschlagener svc mit Result). Verdacht: Zustand des `ThreadType`-Objekts (z. B. ein früherer Thread auf demselben Objekt nicht sauber beendet/zerstört) oder Stack-/Prioritätsprüfung.
- Zweiter Frame 1: ein Draw 960x540 mit Tiefenpuffer (fmt 130). `vk: copy of format 0x35 texture ... skipped` (einmal geloggt) später prüfen.
- `SetThreadPriority` nimmt jetzt ebenfalls 0–63.

### 2026-11-01: Erster Frame, Abbruch in nn::os::CreateThread
- Mit `csrng` läuft das Spiel deutlich weiter: `set`-Zufall, Performance-Konfiguration `0x92220008`, Bluetooth-LE offline (GattService, InitializeBle, BLE-Events), AudioOut (stumm), Xbox-Controller erkannt, Sechsachsensensor und Vibration angefordert, Grafik-Replay mit Recorder-Thread, Shader-Übersetzung mit 2 Arbeitern, **`vk_exec: frame 1`** (ein Draw 64x64, 3 Vertices, Pipeline nach 28 ms).
- Hinweis: `nvn: SetScissors: differing scissor 1 ignored (single-scissor replay)`. Purpur nutzt mehrere Scissors; später prüfen.
- Abbruch: `Break` (SDK-Abort) in `nn::os::detail::ThreadManager::CreateThread` (`sdk+0x3783c0`) aus `nn::os::CreateThread`, Aufrufer `main+0x7839e0` auf einem Spiel-Thread.
- Verdacht: Der Kernel lehnte svc CreateThread ab (nur Priorität 28–59 und Kern −3..2 erlaubt, TotK-Werte). Erweitert auf Priorität 0–63 und Kern −3..3 (Kern 3 läuft auf einem beliebigen Spielkern); Ablehnungen und Werte außerhalb des TotK-Bereichs werden geloggt.

### 2026-11-01: setjmp/longjmp läuft, Dienst csrng
- `lift.py` mit setjmp-Stellen: main 2, subsdk0 0, sdk 2 setjmp-Ziele; alle 404 Dateien kompilieren. Das Spiel kommt über `main+0x26ca4` hinaus, der longjmp-Weg funktioniert.
- Nächster Abbruch: `sm: service "csrng" is not implemented` aus `nn::crypto::GenerateCryptographicallyRandomBytes` (`InitializeCsrng`), Aufrufer `main+0x20f9ba0`.
- Neu `runtime/csrng_service.c`: cmd 0 GenerateRandomBytes füllt den Ausgabepuffer mit `plat_random` (Zufall vom Host-Betriebssystem).

### 2026-11-01: Kein C++-Exception, sondern setjmp/longjmp
- `calls.py main+0x26ca4`: liegt in `main+0x26c60` (32 Instruktionen) direkt hinter `bl _setjmp` (+0x40), danach ein indirekter Aufruf über x20. Ein klassischer setjmp-Rahmen um einen Funktionszeiger. `longjmp` kehrt mit `ret` an die gespeicherte Rücksprungadresse zurück, daher `lr == pc`. Keine `__cxa_*`- oder `_Unwind_*`-Aufrufe.
- Umgesetzt: `recomp/gen.py` findet setjmp-Ziele (eigene Definitionen und PLT-Stubs mit GOT-Slot auf `_setjmp`/`setjmp`/`sigsetjmp`) und hebt jeden Aufruf als `if (!RT_SETJMP(c)) <Aufruf>; <alle Register neu laden>`; Funktionen mit setjmp-Stelle haben kein `musttail`. `runtime/setjmp_host.c` sichert x19–x30, sp, v8–v15 und die Host-Aufruftiefe pro Thread und Puffer; der Ersatz für `longjmp`/`_longjmp`/`siglongjmp` stellt sie wieder her, setzt x0 und springt mit Host-`longjmp` zurück.
- Geprüft: Lifter-Differenztest unverändert grün; erzeugter Code für eine künstliche setjmp-Stelle im Testprogramm kompiliert. Nicht geprüft: ein echter longjmp (braucht das Spiel).

### 2026-11-01: Fonts angenommen, erster C++-Exception-Sprung
- Mit decodierten Fonts kommt das Spiel weiter: lokales Konto angelegt (`build/profile/account.uid`), Spielstand gemountet (`save0`, leer), `bcat::Initialize` (offline), `oe::SetPerformanceConfiguration` zweimal.
- Abbruch: `rt_dispatch: no function at guest pc main+0x26ca4: not a function start (lr main+0x26ca4)`. `lr == pc` ist das Muster von libunwinds `jumpto` (setzt lr auf die Zieladresse und springt mit `ret`): Eine C++-Exception wurde geworfen und soll im Landing Pad `main+0x26ca4` gefangen werden. Die Runtime kann nur Funktionsanfänge anspringen.
- Zwei Wege: (a) herausfinden, wer wirft, und die Ursache beheben (vielleicht eine Ersatzfunktion); (b) Exceptions allgemein unterstützen (Landing Pads aus `.gcc_except_table` als Einsprungpunkte im Lifter, Host-Stack bis zum Rahmen der fangenden Funktion abwickeln). (b) ist aufwendig; zuerst (a).
- `watch.c`: loggt jetzt vor dem Aufruf (für Funktionen ohne Rückkehr wie `__cxa_throw`) und benennt x1, wenn es eine Moduladresse ist (type_info, vtable).

### 2026-11-01: Shared Fonts geladen, Absturz in der Font-Verarbeitung
- `extract_fonts.py` aus der Basis-XCI: alle sechs Fonts bestanden die Prüfungen (Standard 2.053.628, Chinesisch vereinfacht 7.814.744, Erweiterung 106.320, traditionell 3.580.260, Koreanisch 3.672.868, Nintendo-Erweiterung 180.236 Bytes Font).
- Das Spiel lädt die Typen 4, 2, 1, 3 und stürzt dann ab: Schreibzugriff auf `0x38` in `main+0x2151b80` (x0 = 0), Aufruf von `sdk+0x19dc00`, Kette `main+0x120f7d8` → `0x120d3fc` → `0x11ba8ec` → `0x11acd04` → nnMain.
- Ursache (vermutlich): Die Runtime gab dem Spiel die noch XOR-codierten BFTTF-Daten. Auf der Konsole decodiert `pl` die Fonts in den Shared Memory (Header mit festem Schlüssel 0x49621806, so liest libnx `plGetSharedFont`); das Spiel bekommt ein normales sfnt. Behoben in `ov_system.c font_data`: Nutzdaten werden beim Laden decodiert. `--probe-*` für Fonts (`ov_probe.c`) erwartet noch die codierte Form; Probes sind für Purpur ohnehin nicht gedacht.

### 2026-11-01: Wwise lädt, nächster Abbruch an den Shared Fonts
- Mit der Ordner-Korrektur läuft die Audio-Initialisierung durch. Wwise versucht jede Bank zuerst unter `audio/wwise/bin/Switch/English(US)/` (0x202, normal) und nimmt dann die lose Datei in `audio/wwise/bin/Switch/`. Init, BGM, ME, COMMON, DEMO, UI, VIB, ENV, UI_ADD, PM, PL, BATTLE_SYSTEM, PROPS, NPC, PM_SKILLS, PM_VOICE werden so geladen.
- RomFS-Dateiliste (30 Dateien): `arc/data.trpfd`, `arc/data.trpfs`; zwei `audio/motion_detector/common/*.trmdd`; in `audio/wwise/bin/Switch/` 17 `.bnk` und vier `.pck` (`LooseMedia.pck` 56 B, `Patch00.pck` 56 B, `SDC0.pck` 188 MiB, `SDC2.pck` 379 MiB, `Stream.pck` 930 MiB); zwei `demo/movie/*/*.bk2`; `event/trevt/bin/event.trevt`; zwei `system/prefetch/data/global-resident_*.trpre`.
- Zwischendurch: Grafik-Replay startet (`vk: replay on the submitting thread`), hid wird ein zweites Mal initialisiert.
- Abbruch: `nn::pl::GetSharedFontAddress(4)` aus `main+0x120e46c`, keine Datei `4-korean.bfttf`. Anders als TotK fragt Purpur alle Shared Fonts beim Start ab.
- Neu: `tools/extract_fonts.py` (eigener Aufrufer) mit MacNativeGames' `extract_shared_fonts.py` (unverändert, `tools/vendor/macnativegames/`), holt die Fonts aus der Update-Partition der Basis-XCI nach `game/firmware/shared-fonts`. Die Runtime sucht dort (`<romfs>/../firmware`, vorher `<romfs>/../../firmware`, das läge außerhalb von `game/`).

### 2026-11-01: Ursache des LockMutex-Absturzes gefunden (vermutlich)
- Fehlgeschlagene Dateizugriffe beim Start: `OpenFile data:/config/trinity_config/data.trowc` (0x202, wohl optional), `GetEntryType data:/audio/wwise/bin/Switch/` (0x202) aus `main+0x157145c`, dann `OpenFile data:/audio/wwise/bin/Switch/English(US)/PM.bnk` (0x202) aus `main+0x10b4ae4`, direkt danach der Absturz.
- Ursache: `romfs_entry_type` hängt für die Ordnerprüfung ein `/` an; mit dem `/`, den das Spiel schon mitgibt, entsteht `Switch//`, das zu keiner Datei passt. Das Spiel hält den Wwise-Ordner für nicht vorhanden, lädt keine Pakete, und das Bank-Objekt bleibt leer.
- Behoben in `romfs.c`: abschließende `/` werden vor der Prüfung entfernt, die Wurzel selbst ist ein Ordner.
- `PM.bnk` liegt vermutlich nicht lose in der RomFS, sondern in einem `.pck`-Paket (Wwise sucht zuerst in geladenen Paketen, dann lose). Prüfen mit der Dateiliste.

### 2026-11-01: bsearch in der Audio-Initialisierung
- Alle acht `bsearch`-Suchen in `main+0x14fd2d8` finden ihren Schlüssel. Die Vermutung „Tabelleneintrag fehlt“ ist falsch.
- Die Funktion baut mit `snprintf` die Schlüssel `packageName0`, `packageName5` und `bankName0` (Wwise-Soundpakete `.pck` und Soundbanks `.bnk`). Das leere Objekt ist vermutlich ein Soundpaket oder eine Bank, die nicht geladen wurde.
- Hinweis: Beobachter auf sehr häufig gerufenen Funktionen (`strlen`, `snprintf`) machen den Lauf sehr langsam (30–45 s bis zum Absturz statt wenige Sekunden): Jeder Aufruf läuft durch den Beobachter.
- Neu in `fs_host.c`: fehlgeschlagene `OpenFile`, `GetEntryType` und `ReadFile` werden immer geloggt (Pfad, Ergebnis, Aufrufer; höchstens 200 Zeilen).

### 2026-11-01: KARPUR_WATCH_FROM auf die Audio-Initialisierung
- Im Bereich `main+0x14fd2d8-0x14ff98c` kommen nur zwei beobachtete Aufrufe an: `LockMutex` bei +0xd04 (gültiger Mutex) und `AcquireAudioDeviceNotificationForOutput` (Ergebnis 0 = Erfolg). Dann der Absturz im beobachteten `LockMutex` bei +0x1528 (deshalb `lr host`).
- **`GetTlsValue` und `__cxa_guard_acquire` in `main+0x14ff4dc` werden nie aufgerufen**: Die Funktion kehrt vor +0x150 mit 0 zurück. TLS ist damit als Ursache raus.
- Neuer Verdacht: Ihr Argument kommt aus einer der `bsearch`-Suchen davor (Tabelle, Schlüssel nicht gefunden). Der Beobachter loggt jetzt x0–x4 und die ersten 16 Bytes an x0 (bei `bsearch` der Schlüssel).

### 2026-11-01: Erster KARPUR_WATCH-Lauf
- Der Beobachter läuft unter Windows. TLS ist in Ordnung: Slot 0 (`nn::mem::StandardAllocator`, ein Cache pro Thread) und Slot 1 (vom Spiel in `nninitStartup` gesetzt, Speicherkontext) werden auf jedem Thread gesetzt und konsistent gelesen; Slot 2 legt `main+0xb4e600` beim ersten Gebrauch an.
- Slot 1 wird an Hunderten Stellen gelesen; nach 600 Zeilen war Schluss, bevor die Audio-Initialisierung kam. Neu `KARPUR_WATCH_FROM=modul+0xLO-0xHI,...`: nur Aufrufer in diesen Bereichen.

### 2026-11-01: main+0x14ff4dc, Beobachter KARPUR_WATCH
- `main+0x14ff4dc` (300 Instruktionen): `nn::os::GetTlsValue` (+0x150), ein Funktionsaufruf `main+0x106d150`, zwei Hilfsfunktionen, virtuelle Aufrufe, eine statische Variable mit `__cxa_guard_*` und Abbruchpfade (`nn::diag::detail::AbortImpl`). Ein Abbruch kam nicht, also liefert die Funktion ein leeres Ergebnis ohne Fehler.
- Stärkster Verdacht: Der Zeiger kommt aus einem TLS-Slot, der auf diesem Thread (Hauptthread) nie gesetzt wurde. Die Runtime ersetzt die TLS-Funktionen nicht; es läuft der SDK-Code.
- Neu `runtime/watch.c`: `KARPUR_WATCH=<Symbol>,<Symbol>` hängt an jeden JUMP_SLOT-Import dieser Symbole einen Beobachter, der Aufrufer, x0–x3, Ergebnis und TLS-Adresse des Threads loggt (jede neue Kombination aus Symbol, Aufrufer und Ergebnis einmal, höchstens 600 Zeilen). `hostfn_observer` fragt ihn nach den fest eingebauten Beobachtern.

### 2026-11-01: calls.py auf den LockMutex-Absturz
- `main+0x11bcec8` ist ein virtueller Aufruf (`indirect x8`, +0x240) in der Startfunktion `main+0x11bcc84` (607 Instruktionen), aufgerufen aus `main+0x11acb4c`.
- Ziel ist `main+0x14fd2d8` (2160 Instruktionen), offenbar die Audio-Initialisierung des Spiels: ruft `nn::audio::AcquireAudioDeviceNotificationForOutput` (+0xe4c), viermal `bsearch` (+0x10b4…+0x13c0), dann `main+0x14ff4dc` (300 Instruktionen, +0x150c) und direkt danach den abstürzenden `LockMutex` (+0x1528) auf Offset 0xe0 eines Objekts, das 0 ist.
- Vermutung: `main+0x14ff4dc` schlägt etwas nach (Audiogerät per Name? Eintrag einer Tabelle?) und findet es nicht. Kandidaten: der Gerätename (`AudioTvOutput`), den `IAudioDevice` cmd 10 meldet, oder ein Wert aus einer TotK-Ersatzfunktion mit SDK-20-Annahmen.
- PLT-Auflösung in `calls.py` funktioniert am echten Spiel (`nn::os::*`, `bsearch`, `snprintf`, `nn::audio::*`).

### 2026-11-01: Trace vor dem LockMutex-Absturz
- `TOTK_TRACE=ipc,host`: Kein IPC-Befehl liefert einen Fehler. Reihenfolge vor dem Absturz: Audio-Renderer offen (`audren:u` cmd 0, IAudioRenderer 7/4/5), `GetDesiredLanguage`, `set` cmd 5 (GetAvailableLanguageCodes2), einige NVN-State-Objekte, dann vier `IAudioDevice`-Sitzungen (cmd 5 GetActiveChannelCount, cmd 10 GetActiveAudioDeviceNameAuto zweimal, cmd 12 QueryAudioDeviceOutputEvent) und `nn::codec::GetHardwareOpusDecoderWorkBufferSize`. Die Antworten sehen plausibel aus (`AudioTvOutput`, 2 Kanäle).
- Neues Werkzeug `tools/calls.py`: listet für eine Adresse aus dem Log die Aufrufe der umgebenden Spielfunktion (Offset, Ziel als SDK-Symbol oder `main+…`, Größe), nur Adressen und Namen. Mit dem Lifter-Testprogramm geprüft; die Auflösung von PLT-Stubs zu Importnamen ist erst am echten Spiel geprüft.

### 2026-11-01: Dienst `set` läuft, Absturz in nn::os::LockMutex
- `set` wird geöffnet und beantwortet. Danach: ein Präemptions-Interrupt (harmlos), viermal `audren:u` geöffnet, dann Zugriffsverletzung beim Lesen von `0xe1` in `nn::os::LockMutex` mit `x0 = 0xe0`: ein Mutex bei Offset `0xe0` eines Objekts, dessen Zeiger 0 ist.
- Aufrufer `main+0x14fe804`, Kette `main+0x11bcec8` → `0x11acbb0` → `0x92bdc0` → `0x165e87c` → `nnMain+0xac`. Dieselbe Kette wie beim Sprachvergleich (`main+0x11bcd04`), also dieselbe Startfunktion ein Stück weiter.
- Vermutung: Ein Initialisierungsschritt davor (Audio über die vier `audren:u`-Sitzungen? eine TotK-Ersatzfunktion mit SDK-20-Annahmen?) ist still gescheitert und hat kein Objekt angelegt. Nächster Lauf mit Trace.

### 2026-11-01: Grafik-Initialisierung durch, Abbruch am Dienst `set`
- Nach den 65 neuen NVN-Funktionen läuft die Grafik-Initialisierung durch; danach öffnet das Spiel `audren:u` zweimal (Ton über SDL auf dem Standardgerät, 48 kHz) und fragt die Sprache ab.
- Das Spiel fordert beim Start 36 der 39 fehlenden NVN-Funktionen an (alle außer GL-/NvRm-Interop-Resten). Es lädt also pauschal fast die ganze Tabelle; die Liste sagt nichts darüber, was wirklich aufgerufen wird. Diese Funktionen bleiben Platzhalter mit Abbruch beim Aufruf.
- Abbruch: `sm: service "set" is not implemented`, aus `nn::settings::operator==(LanguageCode, Language)` → `MakeLanguageCode` → `CreateSettingsServerProxy` (Aufrufer `main+0x16cf42c`, aus `nnMain`). Die frühere Annahme, die settings-LanguageCode-Funktionen seien reine Datenfunktionen, war falsch: Das SDK fragt dafür den Dienst.
- Neu `runtime/settings_service.c`: Dienst `set` mit GetLanguageCode, GetAvailableLanguageCodes(2), MakeLanguageCode, GetAvailableLanguageCodeCount(2), GetRegionCode (Europa), GetQuestFlag. Die Sprache folgt `ov_language()` (wie `nn::oe::GetDesiredLanguage`).

### 2026-11-01: Erster Fensterstart, NVN-Lücken
- `karpur.exe` ohne `--boot`: Vulkan auf der RTX 5080 (Treiber 621.01, API 1.4.357), Übersetzer gefunden (Identität `9a14dc0c`), Spielcode läuft wie bei M3 bis in die Grafik-Initialisierung (ASTC-Decoder, erste Speicherblöcke). Kein Fenster: Abbruch vorher mit `FATAL: NVN nvnTexturePoolGetSize is not implemented`.
- Bestandsaufnahme: Von 537 NVN-Funktionen in `nvn_procs.inc` hatte die TotK-Runtime 432 umgesetzt, 105 waren schwache Platzhalter, die beim Aufruf abbrechen.
- Neu `runtime/nvn_api_more.c`: 65 davon, die nur gespeicherte Werte melden oder nichts am Bild ändern (Getter für Texture-/Sampler-Pool, Builder, Events, Queue-Builder, Multisample- und Blend-State; Debug-/Checkpoint-Schalter; Queue-Speicherzähler; `TexturePoolRegisterImage` und `SamplerPoolRegisterSamplerBuilder` über dieselben Back-End-Tabellen; `QueueWaitSync` als No-op, weil es nur eine Host-Queue in Reihenfolge gibt).
- Dafür erweitert: `ev_t` merkt sich Pool und Offset, `queue_builder_t` die Priorität (im bisherigen Padding, die 64 Bytes bleiben).
- Neu: `NVN_API` definiert zusätzlich `nvn_has_<name> = 1`; `nvn_procs.c` hat schwache Nullen dazu. Fordert das Spiel eine Funktion ohne Umsetzung an, steht einmal `nvn: missing proc <name> resolved by the game (N so far)` im Log. So zeigt ein Lauf alle Lücken statt nur die erste aufgerufene.
- Bleiben offen (39, Platzhalter mit Abbruch beim Aufruf): Transform Feedback (Begin/End/Pause/Resume/Bind/Draw), `ClearColori`/`ClearColorui`/`ClearTexturei`, `DrawArraysIndirect`, `MultiDraw*IndirectCount`, `DrawTexture`, `DownsampleTextureView`, `TiledDownsample*`, `ResolveDepthBuffer`, `CommandBufferFenceSync`, `Get/SetCopyImageStride`, `GetCopyRowStride`, Tessellation-Level, `SetViewportSwizzles`, `SetProgramSubroutines`, `ProgramSetSampleShading`, `ProgramSetSubroutineLinkage`, GL-/NvRm-Interop (`Sync*GLSync`, `*NvRm*`, `*GLTextureName`), `TextureBuilderGetPackagedTextureLayout`/`GetRawStorageClass`/`GetSparseTileLayout`, `TextureGetSparseTileLayout`.
- Nebenbefund: `MapPhysicalMemory 0x341b00000+0x80000 failed 0x10801` direkt nach `nnMain`; das Spiel läuft danach weiter. Beobachten.

### 2026-11-01: Shader-Übersetzer läuft beim Maintainer
- `tools/build_xlate.py`: alle neun Patches angewendet, `dotnet publish` (SDK 10.0.403) in 16 s, `totk_xlate.exe` 3,3 MB, Identität `9a14dc0cac30561e`. Einzige Warnung IL2075 in `Ryujinx.Common` (Log-Formatierer per Reflection, vom Übersetzer nicht benutzt, harmlos).
- `tools/xlate_smoke.py`: `ok const_ps`, Unsinnszeile mit `fail ?` beantwortet, danach `ok again`; `spirv-val exit 0` (1044 Bytes SPIR-V).

### 2026-11-01: Shader-Übersetzer übernommen
- Aus TotK Recomp (f469c5a, MIT) übernommen: `tools/vendor/ryujinx/` (Ryujinx 1.3.2, Shader-Übersetzer, unverändert, 363 Dateien), `tools/xlate/` (`Program.cs`, `totk_xlate.csproj`, neun Patches 0001–0009), `tools/build_xlate.py` (MacNativeGames-Pin `b75be8cf` als Konstante statt aus `fetch_refs.sh`), `tools/xlate_smoke.py` (Mini-Fragment-Shader durch `--serve`, Prüfung mit `spirv-val`).
- Der Name `totk_xlate.exe` bleibt vorerst, weil `runtime/gfx_shader.c` ihn unter `build/xlate/` bzw. neben `karpur.exe` sucht (oder über `TOTK_XLATE`). Der Übersetzer läuft als Kindprozess (`--serve`), die Runtime übersetzt Shader erst bei Bedarf und legt sie in einem Cache ab.
- Einrichtung (.NET 10 SDK, VS Build Tools mit VCTools-Workload, `spirv-tools`) und erster Fensterstart stehen in `docs/setup-windows.md`, Abschnitte 12 und 13.
- Nicht in der Agent-Umgebung getestet: Bauen und Ausführen (kein .NET, Ausführen von fremdem Code blockiert). Die Patches sind byte-gleich mit TotK und passen dort auf denselben Vendor-Stand.

### 2026-11-01: M3 erledigt
- `karpur.exe --boot` endet mit `boot: passed (920 SVCs, 11 threads, 622 host calls)`. Erster NVN-Aufruf: `nvnBootstrapLoader("nvnDeviceInitialize")` aus `main+0x14c0060`; `SetFocusHandlingMode` kommt bei Purpur erst danach.

### 2026-11-01: Dritter Start erreicht die Grafik
- Liste der Lücken von 331 auf 99 geschrumpft. Übrig sind fast nur reine Rechen-/Datenfunktionen (bleiben Spielcode), Audio-Renderer und Audio-Device (laufen per IPC zu `audren:u`) und `codec::HardwareOpusDecoder`.
- `--boot` läuft durch `nnMain`, oe, hid, RomFS-Mount und `nv::InitializeGraphics` („graphics work memory recorded“) bis zum **ersten NVN-Aufruf**. Keine `offline:`-Zeile bis dahin: Die Online-Funktionen werden beim Start nicht berührt.
- Abbruch nur an einer TotK-Annahme in `boot.c` (SetFocusHandlingMode vor NVN). Purpur startet die Grafik zuerst; die Reihenfolge wird jetzt nur geloggt.

### 2026-11-01: Zweiter Start bis nnMain, 331 Lücken sortiert
- Zweiter `--boot`: `nninitStartup` läuft durch (SetAllocator-Ersatz greift), alle DT_INITs laufen (main 2133, subsdk0 45, sdk 112 init_array-Einträge), `nnMain` startet, oe/hid/RomFS-Mount werden genutzt. Absturz in `nn::aoc::CountAddOnContent` (SDK-Code mit nicht initialisiertem aoc-Dienst, weil TotKs `aoc::Initialize`-Ersatz nichts anlegt).
- `uncovered-imports.txt`: 331 Importe ohne Ersatz (account 19, album 3, aoc 2, audio 51, bcat 22, bluetooth 34, codec 1, ec 1, err 19, fs 7, hid 43, ldn 27, ngc 3, nifm 8, nsd 2, oe 8, pl 1, prepo 8, settings 3, socket 29, ssl 8, swkbd 10, time 11, vi 3, web 8).
- Sortiert und umgesetzt:
  - **Bleiben Spielcode** (reine Rechen-/Datenfunktionen): swkbd-Konfiguration, prepo-PlayReport-Puffer, settings-LanguageCode, err-Argumentobjekte, time IsLeapYear/IsValidDate, socket Inet*/Hton*, ldn-Adresshelfer, web-Argumentobjekte, AudioOutBuffer-Zugriffe, Resampler, alle Audio-Renderer-Funktionen (laufen per IPC zu TotKs `audren:u`), hid-Datenobjekte `NpadId::No1`/`Handheld`/`VibrationNode::DefaultVibrationSampleInterval`, `SixAxisSensorState::GetQuaternion`.
  - **hid** (`ov_input.c`): SDK-13-Namen (`StartSixAxisSensor`, `GetSixAxisSensorStates` mit Rückgabewert, `SendVibrationValue`, `InitializeVibrationDevice` ohne `detail::`) auf TotKs Umsetzung, `GetNpadState` (Einzahl), Joy-Con-Zuweisung, Farben, LED, Style-Event, Fusion-Parameter.
  - **System** (`ov_system.c`): `aoc::CountAddOnContent` = 0 und Changed-Event, oe-Aufnahme/Screenshot/PseudoDeviceId/ExitRequest, `pl::RequestSharedFontLoad`, err-Dialoge (geloggt), Zeitzone (Host-Zone), Netzwerkuhr nicht verfügbar, Online-Konto nicht verfügbar.
  - **fs** (`fs_host.c`, `romfs.c`): `EnsureSaveData`, `IsSaveDataExisting(uid)`, `MountRom(name)`, FsContext, AddOnContent (nicht vorhanden).
  - **Offline** (`ov_offline.c`, neu): nifm, socket, ssl, nsd, bcat, web, ec, album, ngc, vi, ldn, bluetooth. Jeder dieser Ersatzaufrufe loggt seinen ersten Aufruf.
  - **AudioOut** (`ov_offline.c`): stumme Ausgabe, gibt Puffer nach ihrer Spieldauer zurück.

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
