# Roadmap

Every milestone has a clear goal that can be checked. A milestone counts as done only once it has been checked on the maintainer's PC with the real game.

| | Goal | Done when … |
|---|---|---|
| ✅ **M0** | Repository foundation | Rules, licences, protection against game files and CI are in place |
| ✅ **M1** | Tools & extraction | The toolchain runs on Windows, and a script unpacks the player's own dump (base + update) into `game/` and prints a report: game version, hashes, modules, SDK version |
| ✅ **M2** | Lifter works | The C code generated from `main`, `sdk` and `subsdk*` compiles completely; a report lists unsupported instructions (goal: 0) |
| ✅ **M3** | First sign of life | `karpur.exe` starts the game code, reads files from the RomFS and writes logs, still without a picture |
| ✅ **M4** | First picture | The title screen is rendered through Vulkan |
| **M5** | Playable | Walking around, battles, saving and loading work |
| **M6** | PC extras | Higher resolution, higher frame rate, settings menu, installer |

## How we work

- Only the maintainer sees game files. Tools run on their PC and produce **reports** (numbers, addresses, function names, error messages) that can be shared.
- No bytes from the game, no keys and no generated code belong in reports, issues, commits or the chat.
