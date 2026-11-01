# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M5, spielbar

### Nächste Schritte
1. **Transform Feedback fertig** (stabil, Figuren/Pokémon/NPCs korrekt, mit GPL an). Aufräumen bei Gelegenheit: Schrittlogs der ersten 2 TF-Draws in `vk_xfb.c`; Pause/Resume und DrawTransformFeedback sind nicht umgesetzt (Purpur nutzt sie bisher nicht).
2. **Bildfehler.** Unsichtbare Objekte: Basis-Instanz indirekter Draws behoben (Gemüse sichtbar), Rest prüfen. Baum-Flackern behoben durch `SPLIT_DRAWS=0` als Standard (Ursache dahinter offen, siehe Protokoll). Schatten prüfen. Außerdem: Flackern, fehlende/fehlerhafte Schatten, schwarze Flächen (Dunstabzug, Fensterscheiben), kurze „Stachel“-Artefakte bei Effekten, `SetScissors: differing scissor ignored`, `copy of format 0x35 texture ... skipped`, `conservative rasterisation requested: drawn normally`.
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
