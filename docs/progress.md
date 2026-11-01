# Stand

Kurzes Protokoll: was erledigt ist, was als Nächstes kommt und was offen ist. Neueste Einträge oben.

## Aktueller Meilenstein: M4, erstes Bild

### Nächste Schritte
1. Maintainer: Lauf mit der Ordner-Korrektur; Dateifehler und Log-Ende schicken, dazu die Liste der RomFS-Dateinamen. Erwartung: `GetEntryType data:/audio/wwise/bin/Switch/` klappt, Wwise lädt seine Pakete, und `PM.bnk` wird im Paket gefunden statt als lose Datei.
2. Die 36 angeforderten NVN-Funktionen erst umsetzen, wenn sie aufgerufen werden.
3. Offen aus der Importliste: `codec::HardwareOpusDecoder` (Klassen-API), Ton der AudioOut-Ausgabe (stumm), DLC-Freischaltung (`aoc` meldet keine DLC).

### Offene Fragen
- Gleiche Namen heißen nicht gleiche Strukturen: Bei SDK 13 vs. 20 können sich Strukturlayouts und Revisionen ändern (z. B. `NpadFullKeyState`, `AudioRendererParameter`-Revision). Jede übernommene Ersatzfunktion muss gegen SDK 13 geprüft werden.
- Spin-Wait-Patches: TotK leitet ~860 Warteschleifen über `rt_spin_gate` (Patch-Set `spin`, `tools/spin_waits.py`). Wir haben keine Patch-Datei; die asynchrone Präemption (`TOTK_ASYNC_PREEMPT`, Standard an) sollte Hänger abfangen, aber das Spiel kann dadurch langsamer sein. Bei Hängern beim Start zuerst hier ansetzen.
- `nn::aoc::ListAddOnContent` meldet noch keine DLC (TotK-Verhalten). Für Purpur später die vorhandene DLC melden.
- Interne Namen und Umgebungsvariablen heißen noch `TOTK_*` (z. B. `TOTK_GPU=null`, `TOTK_TRACE`). Umbenennen erst, wenn die Runtime läuft.
- `--seconds S` beendet den Lauf über die TotK-Weltuhr (`frame_step_world_us`); mit `rt_game_version = 0` ist unklar, ob sie läuft. Für zeitbegrenzte Läufe mit Bildschirmfoto (`--shot`) später eine eigene Abbruchbedingung (z. B. Anzahl Frames) einbauen.
- `--probe-shader` braucht einen Shader-Census (`tools/shader_census.py` aus TotK, nicht übernommen). Erst übernehmen, wenn wir wissen, wo Purpur seine Shader-Pakete hat (wahrscheinlich in `data.trpfs`, nicht als lose `.bnsh`).
- Werden `nn::movie`-Funktionen überhaupt aufgerufen? Die Videos sind Bink 2 (`.bk2`), das der Spielcode selbst dekodiert. TotKs `ov_movie.c` (VP9 über libvpx) wird dann wohl nicht gebraucht.

## Protokoll

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
