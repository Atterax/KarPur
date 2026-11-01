# KarPur

Static recompilation of *Pokémon Scarlet & Violet* (Nintendo Switch, ARM64) into a native Windows x86-64 executable with a Vulkan renderer. Roadmap: [docs/roadmap.md](docs/roadmap.md). Current state and next steps: [docs/progress.md](docs/progress.md). Read both at the start of every session.

## How this project works

- The maintainer owns the game and a console, runs everything on a Windows PC (Ryzen 7 9800X3D, RTX 5080, 96 GB RAM) and is a beginner at programming. Explain steps to them in German, one command per code block, with the expected output, and assume nothing is installed that `docs/setup-windows.md` does not set up.
- Agents never see game files. Tools we write run on the maintainer's PC and print **reports** (versions, hashes, sizes, counts, addresses, symbol names, error messages) that the maintainer pastes back. Design every tool so that its output is useful as such a report and contains no game bytes or key values.
- Agent sessions start without memory. Record decisions, findings and the next steps in `docs/progress.md` before ending a piece of work.

## Game content boundary

This repository is public. Nothing from the game may enter it.

- The maintainer's game and key paths are in `config.local.toml` (ignored). Tools read the keys file themselves; key values never appear in output, logs, commands or files.
- Everything extracted or generated goes under `game/`, `build/` or `generated/`, all ignored.
- Every file the code generator emits carries the marker `GENERATED_MARKER` from `tools/check_content.py`.
- Run `python tools/check_content.py --staged` before every commit. CI runs the same check.

## Reusing code

- Planned base: TotK Recomp (MIT) for the lifter, kernel model, host services and NVN to Vulkan back end; LAS Recomp (MIT); the Ryujinx shader translator (MIT).
- Copy only MIT, BSD, ISC, zlib or Apache-2.0 code. Keep its header and add a row to `THIRD_PARTY_NOTICES.md` with the upstream commit.
- GPL code (yuzu, suyu and others): read for behaviour, write our own.
- Upstream checkouts for reference go in `refs/` (ignored), never committed.

## Code

- Generated C is never edited by hand: change the generator.
- Generated C needs Clang (GNU extensions such as `__int128` and `musttail`); MSVC `cl.exe` cannot build it.
- Comments say what the code does and why.

## Writing

Code, comments, commit messages and `CLAUDE.md` are in English. Documents the maintainer reads (`README.md`, `docs/*.md`) are in German. Commit messages are plain sentences without type prefixes.
