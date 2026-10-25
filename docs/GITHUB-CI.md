# GitHub Actions CI for 0.2.1 Preview

Repository: https://github.com/sgennadi/TC-Symbolic-Links

## Verified results

[Full VS2026, Linux WDX, and ASan/UBSan run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503):
all three required jobs **passed** on October 25, 2026.

- `windows-vs2026` compiled **SymlinkInspector.wdx64 only** (no
  `TCSymlinkMaker.exe`) using VS2026/MSVC v145 and passed all **six CTest
  suites**, including the real Windows rundll32 Unicode entrypoint test.
- `linux-wdx-unicode` passed all **four CTest suites** and checked WDX exports.
- `linux-sanitizers` passed all **four CTest suites** with ASan and UBSan.
- Total Commander 11.58 ran, and the embedded maker window created a
  Hebrew/niqqud hardlink through `rundll32.exe`.
- The TC main GUI window was not found in GitHub-hosted CI. WDX custom-column
  rendering **inside TC** is unverified, not silently counted as passing.

## Download

- [Windows x64 one-WDX installer package](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12599843692)
- [Linux x64 WDX](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12600028345)
- [Real Total Commander smoke-test evidence](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12600063342)

The Windows artifact contains an outer Actions ZIP and an inner
`TC-Symbolic-Links-win64.zip` installable by Total Commander. The inner ZIP
includes `pluginst.inf`, `SymlinkInspector.wdx64`, and documentation only.

## Important installer limitation

The `pluginst.inf` installer only registers the WDX; it cannot directly create
button-bar buttons or edit custom columns. A first-load WDX initialization
prompt requests consent, then clones the toolbar and creates a dedicated custom
column preset for both panels. The plugin may not be loaded immediately when
installed. An interactive Total Commander test with restart is required before
promoting the Preview build to stable. See [manual tests](MANUAL-TESTS.md).
