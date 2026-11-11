# Roadmap

Every milestone has a clear goal that can be checked. A milestone counts as done only once it has been checked on the maintainer's PC with the real game.

| | Goal | Done when … |
|---|---|---|
| ✅ **M0** | Repository foundation | Rules, licences, protection against game files and CI are in place |
| ✅ **M1** | Tools & extraction | The toolchain runs on Windows, and a script unpacks the player's own dump (base + update) into `game/` and prints a report: game version, hashes, modules, SDK version |
| ✅ **M2** | Lifter works | The C code generated from `main`, `sdk` and `subsdk*` compiles completely; a report lists unsupported instructions (goal: 0) |
| ✅ **M3** | First sign of life | `karpur.exe` starts the game code, reads files from the RomFS and writes logs, still without a picture |
| ✅ **M4** | First picture | The title screen is rendered through Vulkan |
| ✅ **M5** | Playable | Walking around, battles, saving and loading work |
| 🔄 **M6** | PC extras | Higher frame rate, a better picture, settings, an installer, smooth play on other PCs |

## M6 in detail

| | Part | State |
|---|---|---|
| ✅ | 60 FPS (90 / 120 / 144 experimental) | Done |
| ✅ | KarPur Launcher (build, repair, update, settings, controls, feedback) | Done, 0.2.0 released |
| ✅ | Settings and controls in the game (F1 / Esc) | Done, with a Quit game button (#25) and the performance overlay choice |
| ✅ | More wild Pokémon (four levels) | Done |
| ✅ | NVIDIA DLAA, DLSS, DLSS Frame Generation | Done |
| ✅ | All shaders prepared during the build | Done (about 36,000; crash-safe against driver faults) |
| 🔄 | Stability on other PCs | Older CPUs without AVX2: builds and starts, faster math since 0.3.2, a test run that needs reports; a crash after the character creation on some PCs (compatibility mode as a workaround) |
| 🔄 | AMD alternatives | A real performance option now: AMD FSR 1 (present-stage upscaling) plus the launcher's new Handheld mode (the console's own 1280x720, confirmed working) together give a genuine speed gain on AMD and other non-NVIDIA cards, no reverse engineering needed. A finer render-scale percentage is paused (see progress.md: traced into a persistent display-config object, not a simple patch). Later: temporal Vulkan FSR upscaling, then the AMD Vulkan frame-generation swapchain. |
| ✅ | DLC | Maintainer confirmed the DLC unlock works and that the DLC areas play correctly on a save that had already visited them. Reaches players with the next release (`dlc` build step). |
| ⬜ | Draw distance | Real distance settings for trees, bushes, grass, Pokémon and NPCs inside and outside towns |
| ✅ | Launcher extras | A look per edition, the current team with icons from the player's own game, a Saves tab, a Mods tab (experimental; files a mod adds and code mods not yet), the launcher itself playable with a controller (#30), and a Graphics card / Docked-Handheld Mode setting |
| ⬜ | Higher internal resolution | Beyond DLSS's output size |
| ⬜ | Linux and Steam Deck | Planned, after M6's other parts (feature request #23: native Linux, an AppImage launcher). The runtime already has a Linux port (SDL3 window and input, Wayland and X11) and the lifter and build scripts have Linux branches, all untested for Violet. Stage 1: `setup_build.py` with the system's clang, ninja, SDL3, Vulkan and Opus instead of MSYS2, Linux build instructions, checked on the maintainer's Steam Deck. Stage 2: the launcher on a cross-platform UI (Avalonia, C#) as an AppImage. |

## How we work

- Only the maintainer sees game files. Tools run on their PC and produce **reports** (numbers, addresses, function names, error messages) that can be shared.
- No bytes from the game, no keys and no generated code belong in reports, issues, commits or the chat.
