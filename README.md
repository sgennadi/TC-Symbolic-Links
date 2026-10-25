# TC Symbolic Links

[![Windows VS2026 + Linux WDX + Sanitizers](https://github.com/sgennadi/TC-Symbolic-Links/actions/workflows/windows-build.yml/badge.svg)](https://github.com/sgennadi/TC-Symbolic-Links/actions/workflows/windows-build.yml)

**Native Total Commander x64 utilities for symbolic links, directory junctions and hardlinks.**

[Windows x64 build / tested artifact](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192370971/artifacts/12598343525) · [CI run and test logs](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192370971) · [Manual acceptance checklist](docs/MANUAL-TESTS.md)

> **Preview 0.1.5**. Full GitHub Actions matrix passed: Windows Server 2025 / Visual Studio 2026 compilation, all four native Windows tests, Linux WDX build/tests and ASan/UBSan. The *real* Total Commander 11.58 executable started, and the GUI companion created a Hebrew/niqqud hardlink. Loading the WDX into TC's own window and rendering its custom columns remain **unverified**, because the main TC window was not available to the headless runner.

## What it does

- **Create links:** `TCSymlinkMaker.exe` makes file symlinks, directory symlinks, NTFS directory junctions and file hardlinks using native Unicode Win32 APIs — no shell, `cmd.exe` or extra runtime DLLs.
- **Inspect links:** `SymlinkInspector.wdx64` adds content fields `Link type`, `Link target`, `Target exists` and `Hardlink count` to Total Commander custom columns.
- **Bulk operation:** accepts files selected in one TC panel, creates links in the other panel, shows progress and offers Stop.
- **Languages:** English, Russian and Hebrew UI; UTF-16 filenames with Hebrew niqqud, mixed RTL/LTR, Cyrillic, spaces, emoji and long paths.
- **Double Commander on Linux:** build the same WDX source as a native ELF `SymlinkInspector.wdx` with a POSIX backend; it inspects Linux symlinks and hardlinks.

The WDX is an inspector, **not** a context-menu plugin. Link creation is provided by a separate TC button-bar application.

## Install in Total Commander (Windows x64)

1. Download [TC-Symbolic-Links-win64-VS2026](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192370971/artifacts/12598343525) from the successful Windows Actions run. Extract `TC-Symbolic-Links-win64.zip` from the downloaded artifact.
2. Open the inner ZIP in Total Commander x64; use its plugin installer for `SymlinkInspector.wdx64`. Ensure `TCSymlinkMaker.exe` is alongside the installed WDX.
3. Add a new button to Total Commander's button bar with these settings:

   | TC button field | Value |
   |---|---|
   | **Command** | Full path to installed `TCSymlinkMaker.exe` |
   | **Parameters** | `--list "%WL" --dest "%T"` |
   | **Start path** | `%P` |

4. Select files and/or folders in the source panel and navigate to an existing destination directory in the opposite panel. Click the button, choose the link type and create links.
5. In **Configuration → Options → Custom columns**, add fields from the WDX plugin to display link metadata.

The tool does not overwrite existing destinations. Windows symbolic links need appropriate privileges or Developer Mode; junction targets must be local directories; hardlinks must be files on the same volume.

## Build and test

**Windows:** Visual Studio 2026, MSVC v145, CMake 4.2+, Windows SDK, x64.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T v145
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Or run `build-windows.cmd`.

**Linux:**

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

## Verification (Preview 0.1.5)

| Test | Status |
|---|---|
| VS 2026 Windows x64 build: EXE and WDX64 | **Passed** |
| Win32 symlinks, junctions, hardlinks with Hebrew paths | **Passed** |
| WDX64 API exports and UTF-16 fields via direct Windows loader | **Passed** |
| Unicode file-selection parsing, including niqqud | **Passed** |
| NTFS reparse decoder, malformed buffers and UTF-16 validation | **Passed** |
| Native Linux WDX tests (including real POSIX links) | **Passed** |
| Linux ASan + UBSan | **Passed** |
| Total Commander 11.58 x64 process launch | **Passed** |
| GUI companion creates Hebrew hardlink | **Passed** |
| WDX loaded and custom columns displayed **inside Total Commander** | **Not yet verified** |

Full evidence: [GitHub Actions verified VS2026/Linux/sanitizer run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192370971) and [TC smoke report/screenshots](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192370971/artifacts/12598288654). The smoke-test job records this limitation explicitly rather than treating process startup as a complete GUI pass.

## Documentation

- [Windows GitHub Actions and build](docs/GITHUB-CI.md)
- [Real Total Commander CI test](docs/REAL-TC-CI.md)
- [Manual Windows / Hebrew acceptance tests](docs/MANUAL-TESTS.md)
- [Native Linux WDX](README-LINUX.md)
- [Changelog](CHANGELOG.md)
- [GitHub repository About description and topic suggestions](docs/ABOUT.md)

## License

No open-source license has been selected. All rights reserved unless an explicit license is later added.
