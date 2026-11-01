# KarPur

**KarPur** is a fan project that aims to statically recompile *Pokémon Scarlet & Violet* (Nintendo Switch, 2022) into a native Windows x86-64 application with a Vulkan renderer: real PC code, not emulation.

> [!IMPORTANT]
> **This repository contains no game files and never will.** You need your own legally obtained copy of the game and the keys of your own console. Instructions for obtaining those will not be provided.
>
> Everything built from the game is built from your files on your machine, for your own use. Do not share it.

## Status

**Very early (milestone M0).** There is nothing to play yet. The roadmap is in [docs/roadmap.md](docs/roadmap.md), the current state in [docs/progress.md](docs/progress.md).

## How it is meant to work

1. The game's ARM64 executables are translated into C code automatically ("lifting").
2. A runtime of our own stands in for the Switch operating system: threads, files, input, audio.
3. The Switch graphics API (NVN) is mapped to Vulkan, and the game's shaders are translated with the Ryujinx shader translator.
4. Everything is compiled with Clang into an `.exe`, on the player's own machine.

The planned foundation is the MIT-licensed [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) and [LAS Recomp](https://github.com/peiikai/LASRecomp). Code taken from other projects is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

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
