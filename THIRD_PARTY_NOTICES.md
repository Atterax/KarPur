# Third-party notices

Code taken from other projects is listed here with its source, the upstream commit and its license.
Only MIT, BSD, ISC, zlib or Apache-2.0 code may be copied. GPL projects (yuzu, suyu and others) may be read
to understand behaviour, but never copied.

| What | From | Upstream commit | License / where |
|---|---|---|---|
| XCI/NSP/NCA3/ticket reading and ExeFS extraction (`extract_exefs.py`), Copyright (c) 2026 MacNativeGames contributors | [MacNativeGames](https://github.com/ly05010419/MacNativeGames), as vendored unmodified by [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) | `b75be8cfa704df3701e86130c8ced268e79d6b11` | MIT, adapted in `tools/switchfs.py` |
| List of SDK symbol names the TotK runtime handles, with the runtime file of each | [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) `runtime/` | `f469c5a7745502357fc1664c04e02522fe92c42a` | MIT, `tools/data/totk_sdk_symbols.txt` |
| ARM64 lifter, C code generator, FP/SIMD lowering, NSO loader, module model, CPU headers (Copyright (c) 2026 Peiikai), as adapted by TotK Recomp | [LAS Recomp](https://github.com/peiikai/LASRecomp) @ `da35b83a9611d046a9a681aae668d7085954d37f`, via [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) @ `f469c5a7745502357fc1664c04e02522fe92c42a` | MIT, `recomp/lifter.py`, `recomp/simd_c.py`, `recomp/nso.py`, `recomp/gen.py`, `recomp/module.py`, `runtime/cpu.h`, `runtime/simd.h`, `runtime/simd_fp.h` (unmodified copies) |
| Jump-table resolution, function-local flags and registers, module layout, code patch table, free-memory query (Copyright (c) 2026 Valiantsin Dzerakh) | [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) | `f469c5a7745502357fc1664c04e02522fe92c42a` | MIT, `recomp/jumptables.py`, `recomp/flags.py`, `recomp/regs.py`, `recomp/layout.py`, `recomp/patches.py`, `tools/hostmem.py` (unmodified copies) |
| hactool build script and patches: `portable.patch` (title-key file and BKTR fixes from MacNativeGames, Copyright (c) 2026 MacNativeGames contributors), `unicode.patch`, `mbedtls-asm-clobbers.patch` and `build_hactool.py` (Copyright (c) 2026 Valiantsin Dzerakh) | [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) | `f469c5a7745502357fc1664c04e02522fe92c42a` | MIT, `tools/build_hactool.py`, `tools/hactool/*.patch` (unmodified copies) |
| BKTR merge flow and RomFS index (`extract_update_romfs.py`, `extract_romfs.py`), Copyright (c) 2026 MacNativeGames contributors | [MacNativeGames](https://github.com/ly05010419/MacNativeGames), as vendored by TotK Recomp | `b75be8cfa704df3701e86130c8ced268e79d6b11` | MIT, adapted in `tools/extract_romfs.py` |
| hactool (fetched and built on the maintainer's machine by `tools/build_hactool.py`, not in this repository) | [SciresM/hactool](https://github.com/SciresM/hactool) | `1d64a83450e025622f3468c28fc4164dad2c5ef6` | ISC; its license is copied next to the built exe |

The full license texts are in [licenses/](licenses/).

## Planned sources

- [TotK Recomp](https://github.com/valentderah/totk-pc-recomp): ARM64 lifter, Switch kernel model, NVN to Vulkan back end (MIT)
- [LAS Recomp](https://github.com/peiikai/LASRecomp): original lifter and differential tests (MIT)
- [Ryujinx](https://github.com/Ryujinx/Ryujinx): Maxwell shader translator (MIT)
- [hactool](https://github.com/SciresM/hactool): NCA/NSP extraction (ISC)
