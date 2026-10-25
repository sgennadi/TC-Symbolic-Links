# Changelog

## 0.1.5 (preview)

- Add a GitHub Actions stage that downloads **official** Total Commander
  11.58 x64, pins its installer SHA256, validates Authenticode and extracts
  the real `TOTALCMD64.EXE` to a disposable test environment.
- Launch Total Commander with Hebrew paths in both panels and a WDX plugin
  configured in a private TC INI, and attempt to load custom WDX columns.
- Add an optional genuine GUI test of `TCSymlinkMaker.exe` that feeds a TC
  UTF-16LE selection list and requests a hardlink via the window's controls.
- Upload a machine-readable smoke-test report and optional screenshots as
  the `TC-RealApp-Smoke-Windows` GitHub Actions artifact.
- Pin Windows CI runner to `windows-2025-vs2026`, check CMake 4.2+ before
  trying the Visual Studio 18 2026/v145 generator.
- Keep process-start, WDX-load and full GUI test results separate; don't
  claim GUI integration is verified when a non-interactive runner blocks it.

## 0.1.4 (preview)

- Add chunked non-blocking GUI link creation, progress and Stop.
- Adopt the Visual Studio 2026 generator and MSVC v145 in Windows CI.

## 0.1.3 (preview)

- Portable bounded NTFS reparse point parsing, Windows integration and Linux
  tests for Hebrew/niqqud, malformed inputs, and real file system links.
