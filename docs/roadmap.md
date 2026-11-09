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
| ✅ | Settings and controls in the game (F1 / Esc) | Done |
| ✅ | More wild Pokémon (four levels) | Done |
| ✅ | NVIDIA DLAA, DLSS, DLSS Frame Generation | Done |
| ✅ | All shaders prepared during the build | Done (about 36,000; crash-safe against driver faults) |
| 🔄 | Stability on other PCs | Older CPUs without AVX2; a crash after the character creation on some PCs (compatibility mode as a workaround) |
| ⬜ | AMD alternatives | FSR 2/3 upscaling and frame generation for AMD and other cards |
| ⬜ | DLC | Costumes work; the story areas need testing and fixes |
| ⬜ | Draw distance | Real distance settings for trees, bushes, grass, Pokémon and NPCs inside and outside towns |
| ⬜ | Launcher extras | A livelier look per game, a Saves tab (export, import, open folder), the current team shown from the save |
| ⬜ | Higher internal resolution | Beyond DLSS's output size |

## How we work

- Only the maintainer sees game files. Tools run on their PC and produce **reports** (numbers, addresses, function names, error messages) that can be shared.
- No bytes from the game, no keys and no generated code belong in reports, issues, commits or the chat.
