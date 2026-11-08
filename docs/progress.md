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

### 2026-11-08: the scene camera for a temporal anti-aliasing (tests BU, BV)
- ZA Recomp (Pokémon Legends: Z-A, MIT, github.com/Hexveil404/PokeZ-ARecomp, read in `refs/`) runs NVIDIA DLAA and AMD FSR 2/3 on a recompiled Game Freak game: it jitters the scene passes' viewport, makes camera motion vectors from the depth with this and last frame's view-projection (vertex uniform block c5 of the G-buffer draws), replaces the game's final copy of its tone-mapped image with the resolve, and loads NGX at run time from the driver (`nvngx_dlss.dll` supplied by the player). Its other quality extras: GTAO, higher shadow map resolution, 16x anisotropic filtering, water reflections. Its frame rate comes mainly from a device-local cache of all vertex/index ranges refreshed from `GetWriteWatch` pages at each submission, with GPU-written pages tracked per 64-byte line and cached again after 120 frames.
- `TOTK_CAM_PROBE=1` (diagnostic, off): per draw with depth, the perspective matrices in the vertex shader's uniform blocks, every 600 frames in the log.
- Result (BV): Violet draws a G-buffer too (4 colour targets, 1920×1080). The camera's view-projection is **c5 (NVN uniform slot 2) +0, stored as columns**: 1 distinct per frame, changes in every frame while the camera moves and in none while it stands, fov 46°, aspect 1.778. c5 +0x180 behaves the same (a copy or last frame's: not yet told apart), +0x80 is the projection alone (constant), +0x140 its inverse. c10 (slot 7) holds per-object matrices (260+ per frame), as in Z-A. The shadow passes (1024×1024) use other c5 offsets.
- BW: c5 +0x180 is neither this frame's +0 nor last frame's (in every frame the camera moved): the last frame's camera has to be kept by us. The presented image (1920×1080, `R8G8B8A8_UNORM`) is filled by one draw (fragment shader `77a9f872`) from a 1920×1080 `R8G8B8A8_SRGB` image, into which the HUD is drawn last (a 20×20 BC4 icon, a 2048×2048 R8 font atlas). What fills that image first is the next question (BX: the probe now lists every run into each image, from the first).
- BX, the end of Violet's frame (Vulkan formats: 122 `B10G11R11_UFLOAT`, 130 `D32_SFLOAT_S8_UINT`, 64 `A2B10G10R10_UNORM`, 43 `R8G8B8A8_SRGB`, 37 `R8G8B8A8_UNORM`):
  1. `4042fad2` (one full-screen draw, 5 textures) writes a 1920×1080 HDR image (122) from the scene depth (130), a 1920×1080 HDR image and two 480×270 HDR images: a post pass that reads depth (fog / depth of field / bloom composite); some frames `48ffa881` adds a 960×540 HDR image into it.
  2. `333e928a` (one draw) tone-maps that HDR image with a 32×32 LUT (64) into the 1920×1080 sRGB image: the first run into it.
  3. The HUD is drawn over it (`0caa4920`, `efcb19c8`, `6db4269e`, `7e8532e8`: BC textures up to 700×700).
  4. `77a9f872` copies it into the presented image.
  No separate anti-aliasing pass shows between them, and the projection at c5 +0x80 never changes, so the game has no temporal anti-aliasing of its own.
- Where a DLAA / FSR 2 resolve fits: right after `333e928a` and before the first HUD draw, on the tone-mapped sRGB image (as ZA Recomp resolves Z-A's tone-mapped picture), with the depth `4042fad2` samples and motion vectors from c5 +0 of this frame and the last.
- DLAA step 1: `runtime/vk_dlaa.c` (adapted from ZA Recomp) loads NGX from the driver when `TOTK_DLAA=1` (default off), adds the extensions it asks for at instance and device creation, and shuts it down before `vkDestroyDevice` (without that, the driver faulted in `nvcuda64.dll` at exit). `karpur.exe --probe-vkmem` with `TOTK_DLAA=1` on the RTX 5080 with `nvngx_dlss.dll` 310.9.1 next to the exe: "NGX up; DLSS available", exit 0. Not yet called for frames. Of the files the maintainer collected (`C:\KarPur\AMD_NVIDIA_DLLs`, all validly signed): `nvngx_dlss.dll` is the one needed; `nvngx_dlssg.dll` and `sl.*` (Streamline 2.14: frame generation, Reflex) would need Streamline's interposer; `nvngx_dlssd.dll` (ray reconstruction) needs ray-traced inputs; the AMD files are DirectX 12 builds (FSR on Vulkan needs `amd_fidelityfx_vk.dll` or FSR 2 from source).
- DLAA step 2, **works (test BY)**: `runtime/vk_taa.c` with `TOTK_DLAA=1`. The scene passes (viewports of passes with the 1920×1080 scene depth) are jittered (Halton 2, 3, 8 frames) once a resolve has run; right after the tone mapping draw `333e928a` (`TOTK_DLAA_AT`) the sRGB image is copied to RGBA8, `shaders/taa_mv.comp` makes the camera's motion vectors from the depth (last frame's view-projection × this frame's inverse; NDC +y at row 0), DLSS resolves, and the result is copied back before the HUD is drawn. Violet's depth is reversed (clip depth 0.32 at 1 unit, 0.0003 at 1000; found from c5 +0x80 at run time). The maintainer saw smoother edges (stairs, trees) at 60 FPS; GPU busy per frame 5.3 → 6.4 ms. Switches for checks: `TOTK_DLAA_JX` / `_JY` (jitter sign), `TOTK_DLAA_JITTER_BIAS`, `TOTK_DLAA_DEPTH_INV`, `TOTK_DLAA_PRESET` (default 11, K). Building shaders needs MSYS2's `mingw-w64-ucrt-x86_64-shaderc` (installed 2026-11-08).
- Open for DLAA: moving characters and effects get the camera's motion only (ZA Recomp redraws skinned characters into the motion vectors for that); a setting in the launcher and the PC Settings page; the launcher has to bring or ask for `nvngx_dlss.dll`; AMD and Intel GPUs could get FSR 2 (MIT source) on the same inputs.
- DLAA setting: `dlaa = 0|1` in `[display]` of `karpur.ini` (`runtime/cfg.c`, restart). The launcher's Settings page has "Anti-aliasing" (Game's own / NVIDIA DLAA) and "Add nvngx_dlss.dll", which copies the player's own copy next to `karpur.exe` after checking the certificate name and file description. The in-game PC Settings page has no row for it: no checked free labels are left in the message file `option` (archive 2026-11-04); a row would need labels of another message file and a redirect in `fC8CEF9EF`.
- DLSS upscaling, **works (tests BZ-CB)**: `dlss_out = game|window|<w>x<h>` (`TOTK_DLSS_OUT`, live). DLSS resolves the 1080p scene to that size; the picture is scaled down into the game's image (so the HUD and the rest of the frame go on at 1080p) and kept. At the present, `shaders/taa_compose.comp` builds the shown picture at the output size: DLSS's picture where the game's image still equals the scaled-down copy, the presented image (bilinear) within one game pixel of anything the HUD drew. Found on the way: the last draw (`77a9f872`) reads the game's image sRGB-decoded (shown against it decoded: 0.7/1000 difference, as is: 160-220/1000), so DLSS's picture is decoded too (`TOTK_DLSS_CONVERT`, default 1); comparing against the presented image instead of the game's image marked everything as HUD (BZ). About 3% of the picture is HUD in the field. `TOTK_DLSS_DEBUG=1` tints the HUD part red. The maintainer: 1440p and 4K both "look fantastic"; slight smearing (ghosting) remains. The launcher has "DLSS output".
- Ghosting (test CC): trails behind what moves on its own (idle arms, freckles, leaves in front of the camera), which our camera-only motion vectors do not cover. Jitter flipped on both axes removed the trails but made the character tremble (history barely used: the default sign is right); Y alone: unchanged; preset E (CNN) instead of K (transformer): no trails, "everything looks good". Default preset now E (`dlaa_preset`, launcher "DLSS model"). Per-object motion vectors (ZA Recomp's objmv) would let K be used without trails.
- Frame generation, checked (no code yet): the maintainer's Streamline 2.14 files support it on Vulkan (`sl.interposer.dll` exports 229 `vk*` hooks and `slSetVulkanInfo`; `nvngx_dlssg.dll` has `NVSDK_NGX_VULKAN_*`). DLSS-G needs depth, motion vectors and a HUD-less colour image at present time (all available here), ideally UI colour and alpha, Reflex markers (FG depends on them), and the swapchain going through Streamline (its interposer as the Vulkan loader, or manual hooking of swapchain creation and present plus `slSetVulkanInfo` with the queues it asks for); the swapchain is recreated whenever FG turns on or off. Alternative for every GPU: AMD FSR 3.1 frame generation on Vulkan (`amd_fidelityfx_vk.dll` from the FidelityFX SDK; not among the collected files, which are DirectX 12 builds).
- Frame generation stage 1 (`runtime/vk_slfg.cpp`, `TOTK_FRAMEGEN=1`, Streamline headers in `runtime/third_party/streamline/`): Streamline is loaded at run time in manual hooking mode (no over-the-air downloads); KarPur creates instance and device itself with the extensions and extra queues `slGetFeatureRequirements` lists and calls `slSetVulkanInfo`; surface, swapchain, acquire, present and `vkDeviceWaitIdle` go through Streamline's proxies. Streamline's own `vkCreateInstance`/`vkCreateDevice` proxies crashed in `sl.common.dll` (a call through a null pointer while its plugins initialised, even with no feature loaded); the Windows crash note now lists the stack words that point into modules when the fault address is 0. `sl.common.dll` is mandatory besides `sl.interposer.dll`. On the maintainer's PC (`karpur.exe --probe-vkmem`, RTX 5080, driver 621.12): Reflex supported, DLSS-G **not**: "requires GPU hardware scheduling to be enabled in the OS" (HAGS was off, `HwSchMode = 1`).
- Next for DLAA: find the game's final copy of the tone-mapped image and its own anti-aliasing, then the jitter, the motion vector pass and NGX.

### 2026-11-08: progress log in English
- The log is in English from now on, so contributors who do not read German can follow it. The German log up to this point moved to [progress-archive-de.md](progress-archive-de.md), unchanged.
- Released KarPur Launcher 0.1.0 (commit `c3bab43`, README `61ac70f`): https://github.com/Grunttanamo/KarPur/releases/tag/v0.1.0.
