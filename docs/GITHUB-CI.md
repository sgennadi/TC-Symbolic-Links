# GitHub Actions build and test guide

Repository: [sgennadi/TC-Symbolic-Links](https://github.com/sgennadi/TC-Symbolic-Links).

## Status

The full source tree, CMake project, GitHub Actions workflows, localized GUI,
portable WDX implementation and unit tests were imported into `main` on
October 25, 2026.

- [Verified initial Windows + Linux run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40192126587): **passed**.
- Windows Server 2025, Visual Studio 2026 (v145): **4/4 native CTest tests passed**.
- Linux Unicode / WDX loader tests: **3/3 passed**.
- Total Commander 11.58 and the link maker both launched; Hebrew/niqqud
  hardlink creation through the maker GUI: **passed**.
- Total Commander internal WDX custom columns: **not yet fully GUI-verified**.
- The normal workflow also exercises Linux ASan/UBSan and packages both Windows
  and Linux plugins.

## Start CI

1. Open [Actions](https://github.com/sgennadi/TC-Symbolic-Links/actions).
2. Select **TC Symbolic Links - Visual Studio 2026 x64 and Unicode tests**.
3. Click **Run workflow** and select `main`.
4. Inspect three jobs: `windows-vs2026`, `linux-wdx-unicode`, `linux-sanitizers`.
5. Download Windows artifact `TC-Symbolic-Links-win64-VS2026` containing
   `TC-Symbolic-Links-win64.zip` and `.sha256`; the Linux job produces
   `TC-Symbolic-Links-Linux-WDX-x64`.
6. Inspect `TC-RealApp-Smoke-Windows` for `tc-smoke.json`, screenshots, and
   specifically `interactive_gui_verified`. Process launch alone does not
   count as successful WDX column rendering.

The runner is `windows-2025` with the **Visual Studio 2026**
toolchain; CMake generator `Visual Studio 18 2026`, MSVC v145.

## Release policy

Only use a `v*` Git tag to publish a GitHub **prerelease** after Windows,
Linux and sanitizer workflows have passed. Keep the project in Preview while
`interactive_gui_verified` is false. Do not claim a stable release from a
passing compile or DLL ABI test alone.

See [REAL-TC-CI.md](REAL-TC-CI.md) for detailed, licensing-respecting
Total Commander GUI testing.
