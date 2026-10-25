# TC Symbolic Links

**Unicode-aware symbolic link utilities for Total Commander x64.** Create Windows file and directory symlinks, NTFS junctions and hardlinks from the active panel to the opposite panel, and inspect existing links through a WDX content plugin.

> **Preview v0.1.5** — Linux tests passed; Windows/Total Commander integration must pass CI before a stable release.

## Features

- Native Windows C++23 link creation through Win32 APIs; no `cmd.exe`, scripts or third-party DLLs required at runtime.
- `TCSymlinkMaker.exe` for batch operations with a responsive UI, progress and stop button.
- `SymlinkInspector.wdx64` with custom fields: link type, target, target existence and hardlink count.
- Full UTF-16 path support: Hebrew (עברית), Russian, niqqud and mixed RTL/LTR names.
- Native Linux `SymlinkInspector.wdx` for Double Commander; this is **not** a Windows DLL.
- Automated build and unit/integration checks on GitHub Actions, including a real Total Commander smoke-test attempt.

## Install (after Windows CI succeeds)

1. Download the `TC-Symbolic-Links-win64-VS2026` artifact from [Actions](../../actions).
2. Open the inner `TC-Symbolic-Links-win64.zip` in **Total Commander x64** and accept the WDX plugin installation.
3. Add a TC button pointing to the installed `TCSymlinkMaker.exe` with parameters `--list "%WL" --dest "%T"` and start path `%P`.
4. Select files in one panel and navigate to a destination folder in the other panel. Click the button to choose the link type.

The creator never silently overwrites an existing file. Junctions require a local directory target; hardlinks require a file on the same volume; symbolic link creation requires suitable Windows privileges or Developer Mode.

## Building

Use Visual Studio 2026 (MSVC v145) and CMake 4.2+ on Windows.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T v145
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux, use CMake + GCC or Clang, then `ctest` to exercise the native WDX loader, Unicode paths, POSIX links and portable NTFS reparse decoder.

## Project status

This repository is being initialized from the tested 0.1.5 source snapshot. Check [Actions](../../actions) for actual Windows build results; **a passing Linux build is not proof of successful Windows/Total Commander GUI integration**.

## Project metadata

Suggested GitHub About description: **Total Commander plugin for symbolic links, NTFS junctions and hardlinks. Native C++23, Unicode (Hebrew/Russian), Windows x64 and Linux WDX.**

Suggested topics: `total-commander`, `wdx`, `symbolic-links`, `symlink`, `junction`, `hardlink`, `c-plus-plus`, `windows`, `unicode`, `double-commander`.

## License

No open-source license has been selected yet. Until one is added, all rights are reserved.
