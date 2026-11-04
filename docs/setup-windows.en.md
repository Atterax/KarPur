# Setup on Windows

A step-by-step guide that assumes no prior knowledge. If something looks different from what is described here, or an error message appears: **copy the text and report it**, don't guess.

Deutsche Fassung: [setup-windows.md](setup-windows.md).

## 1. Open a terminal

Start menu → type **"Terminal"** → open *Terminal* (the tab at the top should say "PowerShell").

Type (or copy) all the commands below there and confirm with Enter. Paste with **Ctrl+V** or a right click.

## 2. Install Git and Python

```
winget install --id Git.Git -e
```

```
winget install --id Python.Python.3.14 -e
```

Answer any prompts with `Y`. **Then close the terminal and open it again**, so Windows finds the new programs.

Check that everything is there:

```
git --version
```

```
python --version
```

Both should print a version number. Python must be **3.14 or newer**, because the game data is compressed with zstd and Python supports that out of the box only from 3.14. If you already have Python 3.14, just skip its installation.

## 3. Create the project folder and get the repo

We keep everything under `C:\KarPur`. Paths without spaces save a lot of trouble later.

```
mkdir C:\KarPur
```

```
cd C:\KarPur
```

```
git clone https://github.com/Grunttanamo/KarPur.git repo
```

```
cd C:\KarPur\repo
```

Turn on the protection that blocks commits containing game files:

```
git config core.hooksPath .githooks
```

Test that the check runs:

```
python tools/check_content.py
```

Expected output: `check_content: OK`

## 4. Enter your game files

The dumps and `prod.keys` stay **outside** the repo, for example in `C:\KarPur\dumps\`. They are never copied, uploaded or shared.

1. Copy `config.example.toml` to `config.local.toml`:

   ```
   copy config.example.toml config.local.toml
   ```

2. Open it in the editor:

   ```
   notepad config.local.toml
   ```

3. Enter the **paths** to your files (base game, update, `prod.keys`) and save. Git ignores `config.local.toml`, so it stays on your machine.

## 5. Get the latest state

Whenever there is something new in the repo:

```
cd C:\KarPur\repo
```

```
git pull
```

## 6. Set up the Python environment (once)

The tools need two extra packages (`cryptography` for decrypting, `lz4` for unpacking). So that they don't get mixed up with other Python programs on your PC, they get their own separate environment in the `.venv` folder:

```
cd C:\KarPur\repo
```

```
python -m venv .venv
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

It should end with `Successfully installed ...`.

From now on, always start the tools with `.venv\Scripts\python` instead of just `python`.

## 7. Analyze the game (milestone M1)

```
.venv\Scripts\python tools\extract.py
```

The tool:
- opens the base game, update and DLC with your keys,
- unpacks the program files to `C:\KarPur\repo\game\` (Git ignores this folder, it is never uploaded),
- checks every checksum along the way,
- prints a **report** between two lines of `=====`.

It takes about a minute, depending on your drive. The report contains only identifiers, sizes, checksums and function names, so no game data and no keys. **You can copy and share this report.** It is also saved to `game\report\m1-report.txt`.

If a line with `error:` appears instead of the report, just report that line.

## 8. Install the C compiler (milestone M2)

The translated game code is C code, and it has to be compiled into a Windows program. For that we need **Clang** from **MSYS2**, a collection of developer tools for Windows. The installation downloads about 1–2 GB.

Install MSYS2 (it goes to `C:\msys64`):

```
winget install --id MSYS2.MSYS2 -e
```

Update MSYS2. **Run this command twice in a row**, because the first run only renews the base packages and then exits:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
```

Install Clang, the LLD linker and the Ninja build tool:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-ninja"
```

Check:

```
C:\msys64\ucrt64\bin\clang.exe --version
```

Expected output: a line like `clang version 2x.x.x` and below it `Target: x86_64-w64-windows-gnu`.

## 9. Translate and compile the game code (milestone M2)

Get the latest state and install a new Python package (`numpy`):

```
cd C:\KarPur\repo
```

```
git pull
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

Then start the lifter:

```
.venv\Scripts\python tools\lift.py
```

The tool:
- translates `main`, `subsdk0` and `sdk` into C code (folder `generated\`, ignored by Git),
- compiles every C file with Clang, on all processor cores at once (folder `build\lift\`),
- prints a **report** between two `=====` lines at the end.

**This takes a while, expect 10 to 40 minutes.** Your PC will be fully loaded and may get loud, which is normal. It needs about 5–8 GB of free space on C:. In between you will see progress lines like `125/400 files, 300s`.

Please copy and report the report at the end. It contains only numbers, instruction kinds and file names, no game code.

## 10. Unpack the game data (milestone M3)

The game data (graphics, texts, world) is in the base game, and the update changes part of it. The tool **hactool** merges the two. hactool is built once on your PC.

Get the latest state:

```
cd C:\KarPur\repo
```

```
git pull
```

Install the `make` build tool for MSYS2:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make"
```

Download and build hactool (takes under a minute):

```
.venv\Scripts\python tools\build_hactool.py
```

Expected output: a line ending with `...\toolchain\hactool\hactool.exe: 1d64a83 + portable.patch, unicode.patch, mbedtls-asm-clobbers.patch`.

Then merge the game data:

```
.venv\Scripts\python tools\extract_romfs.py
```

The tool copies the two large data packages from your dumps, has hactool merge them while checking every checksum, and saves the result as `game\romfs\romfs.bin`. **This takes about 5–20 minutes** and temporarily needs around 30 GB of free space; about 6–7 GB remain afterwards. At the end there is again a report between `=====` lines; please report it.

## 11. Build karpur.exe and start it for the first time (milestone M3)

Get the latest state:

```
cd C:\KarPur\repo
```

```
git pull
```

Install the libraries the runtime needs (window and input, sound, video, compression, Vulkan):

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-libvpx mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-vulkan-memory-allocator"
```

Rewrite the game data's table of contents once (takes about half a minute; the data itself stays unchanged):

```
.venv\Scripts\python tools\extract_romfs.py --index-only
```

Build `karpur.exe` (translated game code + runtime, about 5 minutes):

```
.venv\Scripts\python tools\build_game.py
```

It should end with a line like `build\game\karpur.exe: file ... MiB`.

Self-test, which needs no game data and takes only seconds:

```
build\game\karpur.exe --selftest
```

And then the first real start. It runs without a window: the game only works in the background and logs how far it gets:

```
cmd /c "build\game\karpur.exe --boot > game\report\boot.log 2>&1"
```

If nothing happens for 2 minutes, stop it with **Ctrl+C**. Then show the last 80 lines of the log:

```
Get-Content game\report\boot.log -Tail 80
```

Please report these lines. A crash is perfectly normal here: it shows us which part of the Switch is missing next.

## 12. Set up the shader translator (milestone M4)

The Switch's graphics chip has its own shader language (small programs that compute pixels and vertices). A translator from the Ryujinx emulator turns these into SPIR-V, the language Vulkan understands on your graphics card. The translator is written in C# and is built with **.NET** into its own `.exe`. To link this `.exe`, .NET needs Microsoft's linker from the **Visual Studio Build Tools**.

Install the .NET 10 SDK:

```
winget install --id Microsoft.DotNet.SDK.10 -e
```

Install the Visual Studio Build Tools with the C++ package. **This downloads about 3–5 GB and takes 10–20 minutes.** A Visual Studio Installer window shows the progress and closes by itself at the end:

```
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Install the SPIR-V checking tool from MSYS2:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-spirv-tools"
```

**Then close the terminal and open it again.** Check:

```
dotnet --version
```

Expected output: a number starting with `10.`

```
Test-Path "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
```

Expected output: `True`

Get the latest state:

```
cd C:\KarPur\repo
```

```
git pull
```

Build the translator. The first time, .NET still downloads packages, so **expect 3–10 minutes**:

```
.venv\Scripts\python tools\build_xlate.py
```

Expected output: first ten lines from `applied 0001-...` to `applied 0010-...`, then many lines from .NET, and at the end `translator identity ...` and `built ...\build\xlate\totk_xlate.exe (... MB) in ... s`. (The name `totk_xlate` comes from the project we took the translator from; it will be renamed later.)

Quick test with a small self-made shader, which needs no game data:

```
.venv\Scripts\python tools\xlate_smoke.py
```

Expected output:

```
replies: ['ok const_ps', 'fail ? ...', 'ok again']
spirv-val exit 0 (... bytes)
```

The `fail` reply in the middle is intended: the test sends nonsense once and checks that the translator keeps working afterwards.

If `build_xlate.py` ends with an error message: report the last 30 lines.

## 13. First start with a window (milestone M4)

So far `karpur.exe` ran with `--boot`, without a picture, and stopped at the first graphics call. Now we start it without `--boot`: it opens a window and tries to draw. **A black window or a crash is normal here**; the log shows us which parts of the graphics are still missing.

```
cd C:\KarPur\repo
```

```
build\game\karpur.exe
```

Wait about 2 minutes (or until the window closes by itself). If it is still open then, close it with the **X** at the top right or press **Ctrl+C** in the terminal.

The log is in `build\game\logs\karpur.log`. Show the last 80 lines:

```
Get-Content build\game\logs\karpur.log -Tail 80
```

And the lines about graphics and shaders (at most 150):

```
Select-String -Path build\game\logs\karpur.log -Pattern "nvn|gfx|vk_|shader|fatal|error|missing|unimpl" | Select-Object -Last 150
```

Please report both outputs. They contain function names, addresses and error messages, no game data.

## 14. Get the system fonts (milestone M4)

At start the game uses the Switch's fonts (the "shared fonts"). They are not in the game itself but in the system update that comes on every game card. The tool gets them from your base game (the XCI from `config.local.toml`) and checks every checksum along the way. It takes only a few seconds.

```
cd C:\KarPur\repo
```

```
.venv\Scripts\python tools\extract_fonts.py
```

Expected output between two `=====` lines: six lines from `font 0 standard: ... bytes` to `font 5 nintendo-extension: ... bytes` and `written to ...\game\firmware\shared-fonts`. The files stay in `game\` and are never uploaded.

## Set up PC Settings in the main menu (once)

For the "PC Settings" entry to appear in the game's main menu (X), you need a list that matches your game files. It stays under `game/` and does not go into the repo. Meant for Violet 4.0.0.

1. Create the list:

   ```
   .venv\Scripts\python tools\pc_menu.py
   ```

   Expected last line: `done: …\game\ui_patch.txt`

2. If the tool reports that files are missing from `game/dump`: start the game once like this, open the main menu (X) and Options, close the game, and run step 1 again.

   ```
   .venv\Scripts\python tools\run_with.py DATA_WATCH=1
   ```

Without this list the game starts as usual, without the entry and with the whole F1 menu. With the list F1 stays closed; the controls are then under PC Settings → Controls. To get the whole F1 menu for one run (for troubleshooting, for example):

```
.venv\Scripts\python tools\run_with.py MENU_LOCK=0
```
