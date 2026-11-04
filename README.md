# KarPur

**KarPur** is a fan project that statically recompiles *Pokémon Scarlet & Violet* (Nintendo Switch, 2022) into a native Windows x86-64 application with a Vulkan renderer: real PC code, not emulation.

> [!IMPORTANT]
> **This repository contains no game files and never will.** You need your own legally obtained copy of the game and the keys of your own console. Instructions for obtaining those will not be provided.
>
> Everything built from the game is built from your files on your machine, for your own use. Do not share it.

## Status

**Playable, still in development (milestone M5 of 6).** *Pokémon Violet* 4.0.0 runs from the title screen into the open world, with battles, picnics and fast travel, at a stable 60 FPS on the maintainer's PC (Ryzen 7 9800X3D, RTX 5080). Scarlet and other game versions are not tested. The roadmap is in [docs/roadmap.md](docs/roadmap.md), the current state in [docs/progress.md](docs/progress.md).

## Features

- **60 FPS** with the game's speed kept right, smooth effects, and smooth walking indoors. 90, 120 and 144 FPS are available as experimental limits: walking indoors follows them, effects can stutter.
- **PC Settings inside the game.** A new entry in the main menu, under Options, opens the game's own options page with PC settings, in every language the game offers:
  - fullscreen, window size, frame rate limit, frame sync (Mailbox, VSync, off)
  - upscaling (AMD FSR 1 or bilinear), sharpness, performance overlay
  - Pokémon population, see below
- **Pokémon population in four levels**, for how many wild Pokémon roam at once. It takes effect after a restart:

  | Level | At once | Good for |
  |---|---|---|
  | Switch (default) | 15 | the game as it ships |
  | Switch 2 | 40 | playing |
  | Enhanced | 80 | playing |
  | Extreme | 120 | screenshots; can drop to about 35 FPS |

  Switch 2 uses the spawn values of the fan mod [Pokémon Compass](https://compass.seri.studio) by Aria Seri and Kas Seri; the higher levels raise only the count. KarPur contains these numbers, none of the mod's files.
- **Controls page** in the look of the game's menus, opened from PC Settings: key and button remapping, button layout, motion controls, vibration, stick dead zone and mouse sensitivity. Its texts are in English, German and Russian, following the Windows language.
- **Mouse and keyboard or controller**, with the mouse turning the camera.

## Known limits

- The game image stays at 1920 x 1080; the window scales it. A higher internal resolution is not done yet.
- Online features are not available, and downloadable content is not reported to the game yet.
- Some graphics details are still open, see [docs/progress.md](docs/progress.md).

## Getting started

You build everything yourself from your own copy of the game: [docs/setup-windows.en.md](docs/setup-windows.en.md) ([Deutsch](docs/setup-windows.md)) goes through it step by step, including the one-time step that makes PC Settings appear in the main menu. Without that step the game still runs, with the older full settings menu on F1. That menu stays available for troubleshooting with:

```
.venv\Scripts\python tools\run_with.py MENU_LOCK=0
```

## How it works

1. The game's ARM64 executables are translated into C code automatically ("lifting").
2. A runtime of our own stands in for the Switch operating system: threads, files, input, audio.
3. The Switch graphics API (NVN) is mapped to Vulkan, and the game's shaders are translated with the Ryujinx shader translator.
4. Everything is compiled with Clang into an `.exe`, on the player's own machine.
5. The game's own menus are extended at run time: a script runs in the game's Lua state, and data files are changed as the game unpacks them. Your game files on disk stay untouched.

The foundation is the MIT-licensed [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) and [LAS Recomp](https://github.com/peiikai/LASRecomp). Code taken from other projects is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Built with AI

This project would not exist without AI. Most of the code and documentation is written by an AI coding agent ([Claude Code](https://claude.com/claude-code) by Anthropic), which also plans the work, explains every step and analyzes the reports and crash logs. The maintainer owns the game and the console, runs and tests every build on real hardware, and decides what goes in. The rules the agent works under are in [CLAUDE.md](CLAUDE.md).

## Contributing

Read [CLAUDE.md](CLAUDE.md) first for the project rules. Before every commit, run:

```
python tools/check_content.py --staged
```

Enable the protective hook once per clone:

```
git config core.hooksPath .githooks
```

## License

The project's own code is under the [MIT License](LICENSE); third-party code keeps its own license. The license grants nothing for the game.

KarPur is an independent, non-commercial fan project and is not affiliated with or endorsed by Nintendo, The Pokémon Company, Game Freak or Creatures Inc. *Pokémon*, *Pokémon Scarlet*, *Pokémon Violet* and *Nintendo Switch* are trademarks of their respective owners.
