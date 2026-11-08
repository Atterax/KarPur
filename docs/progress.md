# Progress

The current state, the decisions that shaped it, and what comes next. Newest log entries on top.

The detailed German log up to 2026-11-08 (every test, A to BT, with its numbers) is in [progress-archive-de.md](progress-archive-de.md). Code comments that cite `docs/progress.md` with a date up to 2026-11-08 refer to that archive.

## Current milestone: M5, playable

*Pokémon Violet* 4.0.0 runs from the title screen into the open world, with battles, picnics, fast travel, saving and loading. On the maintainer's PC (Ryzen 7 9800X3D, RTX 5080) it holds 56–60 FPS almost everywhere, towns included, with no tree flicker (test BT, 2026-11-08). The game's main menu has a PC Settings page; the older full settings menu on F1 is locked (`TOTK_MENU_LOCK=0` opens it). The KarPur Launcher 0.1.0 builds the game from a player's own dumps without any setup ([launcher.md](launcher.md), German).

Scarlet and other game versions are untested.

## Defaults that matter

Runtime switches are environment variables `TOTK_<NAME>` or lines in `karpur.ini`; `tools/run_with.py NAME=VALUE` sets them for one run. These defaults are the result of the frame rate and flicker work up to test BT:

| Switch | Default | What it does |
|---|---|---|
| `SUBMIT_SYNC` | 0 | Submits do not wait for the GPU (CPU and GPU overlap). |
| `SUBMIT_DEFER`, `SUBMIT_DEFER_US`, `DEFER_PRESENT` | 1, 6000, 1 | A recorded submission reaches the GPU when the next one begins, before a present/upload/wait, or after 6 ms: late CPU writes reach the VRAM mirrors first. |
| `HOT_REFRESH` | 0 | Hot chunks (memory the CPU writes late) are bound from imported guest memory, so the GPU reads them when it runs, as on the Switch. |
| `HOT_LATE_SUBS`, `RACE_SUBS` | 1, 1 | A chunk turns hot after one late write, or one write while the GPU ran a submission that read it from the mirror. Needed for split submissions without flicker. |
| `HOT_BIND_MAX`, `HOT_EXACT`, `HOT_COOL` | 512 KiB, 1, 300 | Only bindings up to 512 KiB read hot chunks from guest memory; index/indirect bindings use their real size; hot chunks cool after 300 quiet submissions. |
| `COMPUTE_VRAM` | 1 | A dispatch's storage blocks in guest memory are copied into a VRAM ring around the dispatch. |
| `VB_VRAM` | 4096 | For draws with at least 4096 vertex invocations, vertex streams up to 4 KiB in guest memory are copied into a VRAM ring right before the draw. |
| `XFB_MIRROR` | 1 | Transform feedback writes into the mirror (written back at the submission's end). |
| `SPLIT_DRAWS`, `SPLIT_US` | 64, 1000 | A submission goes to the GPU in parts, so the GPU starts while the replay still records. |
| `MIRROR_DEMOTE` | 1 | Pools the CPU writes while submissions are recorded give up their mirror. |

## What we learned (summary of the archive)

**Memory model.** The Switch has one memory for CPU and GPU; a discrete PC GPU has its own. CPU-visible pools are imported host memory with a VRAM mirror. The game writes some data late, even while the GPU already runs the work that reads it. Reading everything from imported memory is correct but slow (PCIe); reading stale mirrors makes trees flicker. The working combination: mirrors synced before each submit ("late sync"), deferred submits, and "hot" chunks that are read from imported memory once the game was seen writing them late or while the GPU ran.

**Frame rate: small data read very often over PCIe.** The cost of imported memory is not its bytes but how often the GPU reads it:
- *Snow and sandstorm:* the particle compute shader `1821327f` read and wrote six storage blocks in guest memory: 11 ms per frame, plus system-wide audio dropouts. In VRAM it takes 0.02 ms. Fixed by `COMPUTE_VRAM`.
- *Ground grass:* fragment shader `e963d67a`; every vertex of ~150,000 per frame read four stride-0 vertex streams of 16–192 bytes from guest memory: about 10 ms. Most of the cost was in the vertex stage, not the pixels (`SKIP_MODE=raster`). Fixed by `VB_VRAM`, which helped well beyond grass.

**Frame rate: towns.** In towns the GPU needed ~15 ms per frame but frames took ~24 ms. The game waits once per frame at `main+0xcd0eb0` (`nvnSyncWait`) for the previous frame, as on the Switch; here the GPU only started after the replay had recorded the whole submission. `SPLIT_DRAWS=64` starts the GPU earlier (towns went from 41–47 to 52–60 FPS); its old tree flicker disappears with `HOT_LATE_SUBS=1 RACE_SUBS=1`.

**What did not help.** `HOT_PASS` (copying the hot chunks a pass used last frame: the data moves every frame), `VS_VRAM` (vertex-stage uniform and storage buffers are not in guest memory), `MIRROR_DEMOTE=0` (fixed snow, but every other place lost frames), removing the submit deferral (no gain, short flicker), `HOT_REFRESH=1/2` (copies made too early: flicker).

**View distance.** The LOD table (`trinity_ModelLodSetting`, `MOD_FILES`) sets terrain and rock detail, not when objects appear. Trees and fences, Pokémon (the spawn table and the LOD table both limit them) and grass have limits of their own. Raising spawn distances made objects flicker or vanish. The Oodle data hook stays active for the whole session because the UI list waits for files that never load; it is a suspect for flicker after loading.

**PC Settings menu.** `game/ui_patch.txt` holds only sizes, hashes, offsets and KarPur's own values and texts, the same for every Violet 4.0.0, so it ships as `tools/data/ui_patch_violet_4.0.0.txt`.

## Diagnostics

All off by default except the abort line.

| Tool | Shows |
|---|---|
| `TRACE=perf` | Frame times, render thread waits, mirror and hot chunk statistics, PCIe bindings by kind, and every 10 s the `nvnSyncWait` callers that blocked |
| `PASS_TIME=1/2/3` | GPU time by pass, fragment shader and compute shader; 2 adds a full barrier after each pass, 3 times every draw alone |
| `SKIP_FS=<id>` + F9 | Switches one shader's draws or dispatches off and on; describes its first draws (textures, buffers, where each lies, pipeline state) |
| `SKIP_MODE=raster` | With `SKIP_FS`: F9 keeps the vertex work and drops only the rasterization |
| `PCIE_RANK=1` | Every 600 frames, the shader pairs that read guest memory most often (vertex invocations × bindings) |
| `AUDIO_QUEUE_MAX_MS` | Caps the audio queue (diagnostic) |
| `guest abort in …` (always) | The SDK function, file and line before the game aborts |

## Launcher and build pipeline

- `launcher/` (C# WinForms, self-contained, one exe) carries the repository's files, the shader translator and hactool; `tools/make_launcher.py` builds it.
- `tools/setup_build.py` runs the steps of `docs/setup-windows.md` with Python's embeddable package and MSYS2's clang unpacked from pinned packages (`tools/toolchain_msys2.py`, `KARPUR_UCRT`). It runs only steps whose outputs are missing or whose inputs changed, which makes it the resume, Repair and Update path.
- Releases: tag `vX.Y.Z` with the asset `KarPur-Launcher.exe`; raise `<Version>` in `launcher/KarPurLauncher.csproj` first. The Feedback tab opens `.github/ISSUE_TEMPLATE/feedback.yml` filled in; issues get the label `feedback`.

## Next steps

1. **Long sessions.** At the end of test BT, 106,395 chunks (415.6 MiB) were hot. Watch whether the frame rate drops over hours of play.
2. **Stutter** when opening the map (single frames of 75–282 ms) and a spike of 65–80 ms about every 70 s.
3. **View distance** for grass, bushes and NPCs, and a view distance setting in the launcher and the PC Settings page.
4. **Higher internal resolution** than 1920×1080.
5. **Test the launcher's update path** with a release v0.1.1; test on other PCs (feedback issues).
6. **Scarlet:** test with a Scarlet dump.
7. The rare abort after a fast travel (`AbortImpl` called from `main+0xb5d930`, seen once); the abort line now names the place.
8. Leftovers: transform feedback pause/resume and `DrawTransformFeedback`; NVN functions as they get called; `nn::aoc` does not report DLC; `codec::HardwareOpusDecoder`.

## Open questions

- Same names do not mean same structures: between SDK 13 and 20, struct layouts and revisions can change (for example `NpadFullKeyState`, the `AudioRendererParameter` revision). Every replacement function taken over must be checked against SDK 13.
- Spin-wait patches: TotK routes ~860 wait loops through `rt_spin_gate`. We have no patch file; asynchronous preemption (`TOTK_ASYNC_PREEMPT`, on) should catch hangs but may cost speed. Look here first for hangs at start.
- Internal names and environment variables are still called `TOTK_*`. Rename once the runtime has settled.
- `--seconds S` ends a run through TotK's world clock; with `rt_game_version = 0` it is unclear whether that runs. Timed runs with `--shot` need an end condition of their own (a frame count).
- `--probe-shader` needs a shader census (TotK's `tools/shader_census.py`, not taken over); only worth it once we know where Violet keeps its shader packs (likely in `data.trpfs`).
- Are `nn::movie` functions called at all? The videos are Bink 2 (`.bk2`), decoded by the game's own code, so TotK's `ov_movie.c` (VP9) is probably not needed.

## Log

### 2026-11-08: progress log in English
- The log is in English from now on, so contributors who do not read German can follow it. The German log up to this point moved to [progress-archive-de.md](progress-archive-de.md), unchanged.
- Released KarPur Launcher 0.1.0 (commit `c3bab43`, README `61ac70f`): https://github.com/Grunttanamo/KarPur/releases/tag/v0.1.0.
