# FHD_GalaxyZoom 0.1.5 ALPHA — source for Nexus Mods review

Complete source and a verified build for the uploaded `FHD_GalaxyZoom_v0.1.5_ALPHA_AModLite_MarkerFix.zip`. **The original C source is unchanged.** All three runtime files build without a game installation or prebuilt mod binaries. Configuration and ModuleInfo reproduce the upload exactly; **the rebuilt DLL is not byte-identical to the uploaded DLL**. Both DLLs passed the same 125 native emulation checks, including checks against one previously supplied game EXE. This does not establish full binary equivalence or in-game compatibility.

ModuleInfo retains author `Prolog_D / OpenAI draft`, priority 2, empty Dependence/Conflict fields and Russian/English descriptions. This remains an **alpha**.

## Mod behavior

On the Galaxy map, wheel up increases zoom by 1.25 per step, to 3.814697265625×; wheel down returns toward 1×. Zoom centers on the cursor inside the map. Right-button dragging pans a zoomed map; a right click without movement forwards the original callbacks. Map-line segments are clipped to the frame; small icons crossing the frame are hidden as whole icons.

The native code also tracks named search/quest markers, unnamed battle icons and AModGalaxyLite sibling overlays such as `RedRing0`, `BlackHole152` and `ldacp5`. AModGalaxyLite is optional. Its assets and game files are not bundled.

The DLL loads through the standard `Data.ScriptLibs` mechanism, initialized from both OnStart and OnLoad. It patches the running process's projection code and Galaxy VMT slots, dynamically resolves Windows APIs through the PEB, and pins itself in memory. It does not write Rangers.exe on disk. No Xeno loader, SCR or RSON project is required; all native behavior is visible in C.

## Files

| Path | Purpose |
| --- | --- |
| `SOURCE/Native/galaxy_zoom.c` | Complete, unchanged uploaded C source |
| `SOURCE/Native/build.sh`, `elf_to_coff.py`, `galaxy_zoom.def` | GCC compile, ELF-to-COFF conversion and PE link/export definition |
| `SOURCE/CFG/Main.txt` | Full ScriptLibs and initialization hooks |
| `SOURCE/build_dat.py` | Uploaded DAT serializer/reader, with an unused external-kit path removed |
| `SOURCE/ModuleInfo.txt` | Editable UTF-8 metadata, emitted as original CP1251 bytes |
| `build.py` | Complete source-only builder and installable ZIP writer |
| `scripts/` | Independent DAT checker and PE32 inspector |
| `tests/` | Source-only build tests and execution of actual DLL machine code under Unicorn |
| `original-file-sha256.json` | Hashes of all three uploaded runtime files |
| `prepared-file-sha256.json` | Hashes of the verified build with the available toolchain |
| `build-verification.json` | Completed results, provenance and limits |
| `.github/workflows/build.yml` | GitHub build/check workflow |

This review archive excludes compiled mod binaries, compiler intermediates, game EXEs and redundant documentation. The three supplied formula/model test scripts are replaced by checks that execute the actual original/rebuilt DLL code. The DAT helper no longer accesses an unused path derived from `THIS.parents[4]`, which failed when the source was placed in a shallow directory. Its serialized output is unchanged.

## Build and tests

Verified environment: Ubuntu tools, Python 3.12.14, zlib 1.3.2, GCC 13.3.0 and GNU binutils 2.42. Building needs Python 3.10+, bash, GCC with freestanding `-m32` code generation, and GNU ld with the `i386pe` target. No Win32 SDK, Windows runtime, multilib C runtime or game executable is needed. Tests additionally use `unicorn==2.1.4`.

```sh
python3 build.py --strict-prepared
python3 -m pip install -r requirements-tests.txt
python3 -m unittest discover -s tests -v
```

Output: `dist/FHD_GalaxyZoom_v0.1.5_FROM_SOURCE.zip` and `dist/build-report.json`. The generated ZIP installs at `Mods/FHD_MODS/FHD_GalaxyZoom`. Enable the mod and restart the game after changing the native library. Removing the mod also requires restarting to unload the pinned DLL. The delivered GitHub review archive itself contains sources rather than an installable mod.

`--strict-prepared` requires the hashes of the verified toolchain build. Omit that option for deliberate source changes or a different compiler; the builder still requires the uploaded config/metadata hashes and lists the DLL's original and rebuilt hashes. To intentionally change config or metadata, update their reference hashes as part of the change. The GitHub workflow is supplied; hosted execution was not performed during preparation.

## Native binary reproducibility

| Item | Uploaded DLL | Rebuilt DLL |
| --- | --- | --- |
| Bytes | 22,979 | 21,922 |
| PE linker version | 2.44 | 2.42 |
| SHA-256 | `5f87900fa653455f7e0a485fbdda57ec83a366dc7c4ff5dfea44d008129156c4` | `36cd0d8547a0aff514e09453ad9d1bcaed0deddb8da4890a754976aef629e570` |

The original compiler version was not supplied. Differences include the generated `.text`, PE section layout and linker metadata; they are not only a timestamp difference. Exact original DLL reproduction and exhaustive semantic equivalence have not been established. No original binary data is used as a build input, and no C changes were made to force a matching hash. The two other runtime files match their original SHA-256 hashes exactly.

## Executable guards and tests

The original README declares three supported EXE hashes. The source guard does **not** hash the whole EXE: it checks PE metadata, Galaxy VMT targets, a projection signature and the clear-call prefix. The version string 2.1.2500 alone is insufficient.

| Declared EXE SHA-256 | Timestamp | SizeOfImage | Verification in this preparation |
| --- | --- | --- | --- |
| `83300344af802bc51e64389c58f047e5afdf195c133048098be3881fae29ed98` | `0x68eccc46` | `0x4d1000` | Synthetic profile/guard tests only |
| `f00343363043cbb9c2d10c1216e9f4c37b5eaa5e4d58f37a45284cae528fdc1d` | `0x6a7afc5c` | `0x4d2000` | Synthetic profile/guard tests only |
| `e43a4b15503c705eebe2d8ce5778c2a2fb090db515a3fd86d319e5c5aa692ebf` | `0x6ac53b97` | `0x461000` | Actual previously supplied EXE bytes plus a synthetic live Galaxy instance |

Five automated tests passed. The native suite executes PE32 DLL code at its preferred and relocated addresses. It checks unsupported builds and changed signatures, successful hook installation, repeated initialization, module-pin failure and rollback after a single injected protection failure at each of five installation stages. It also checks projection, cursor anchoring, zoom bounds, drag/capture messages, line clipping, Lite overlay capture, star-relative movement and visibility restoration. OS methods, UI position/activation methods and live objects are controlled fixtures.

The default native suite makes 119 assertions. With the optional EXE argument it makes 125. Both the uploaded DLL and the source-built DLL passed all 125 during preparation. The optional check can be repeated with an authorized copy of the listed EXE:

```sh
python3 tests/verify_native.py --exe /path/to/Rangers.exe
```

One precise source behavior is retained: the native Lite-name parser accepts numeric suffixes of **1–4 digits**. The uploaded Python model allowed five digits; the actual-code checks now record the native behavior. The known supplied overlay names fit the native range. This was a test-model correction, not a runtime change.

## Limits and provenance

No live Windows game, full map OnClose/OnOpen rebuild, real Windows page protection/module pinning, complete mixed-mod installation or AModGalaxyLite runtime was tested. Rollback tests assume later restoration writes succeed after one injected failure; repeated OS failures are not covered. The original README specifically leaves the 0.1.5 Lite-marker fix and full mod-stack compatibility unverified in-game. Keep that alpha status when distributing. Emulation results are not an antivirus verdict or Nexus moderation approval.

Input archive SHA-256: `0c6a9aa41972825c6cbc190e0ff8bd0b5988f76264c6af6e0ed617b9811f832a`. All 13 files extracted successfully. Uploaded C source SHA-256: `8624a8c477df6de5e49c6a661e08d54f73464a3ce3cf579e15704065b0369adc`.

The original source documentation cites [SpaceRangersHD_decomp](https://github.com/pakompom/SpaceRangersHD_decomp), MIT-licensed [rangers-utils](https://github.com/murgesku/rangers-utils) for BlockPar formats, and [SRHD-XenoModKit](https://github.com/Xenomorphchyma/SRHD-XenoModKit). Existing authorship is preserved; no new license or redistribution permission is inferred from this preparation.
