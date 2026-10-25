# TC Symbolic Links 0.2.1 Preview — One DLL

One Unicode-aware `.wdx64` module for **Total Commander x64**. It both inspects links in WDX custom columns and creates Windows symbolic links, NTFS junctions, and hardlinks using an integrated Win32 dialog.

**No `TCSymlinkMaker.exe` and no extra runtime dependencies.** The Create Links button uses the standard Windows `rundll32.exe` to load an exported function from the **same** `SymlinkInspector.wdx64` module. Requires Windows 10/11 x64 and Total Commander x64.

The Linux build is a native ELF `.wdx` for Double Commander. It provides inspection only, using the same field names; Windows link creation is unavailable on Linux.

## Installation and automatic configuration

1. Open the **installer ZIP** in Total Commander by double-clicking or pressing Enter (do not use Ctrl+PgDn). `pluginst.inf` offers installation and registers `SymlinkInspector.wdx64` as a content plugin.
2. **When TC first loads the WDX**, the plugin offers one-time configuration (English, Russian, Hebrew). Choose **Yes** to have it create a toolbar button and custom-column preset, then activate the preset for **both panels**. Restart Total Commander so its toolbar and panel layouts reload.
3. Existing column presets are retained, and the existing toolbar is **cloned**, not overwritten. Changed INI files have `.tcslinks.bak` backup copies. Repeated configuration does not duplicate the button or columns. No hidden modifications occur after choosing No.

**Important Total Commander limitation:** the built-in `pluginst.inf` installer registers WDX plugins only; it does **not** offer a supported post-install hook for button bars and custom-column presets. The optional setup runs from the WDX when TC loads it, not from the installer itself. WDX loading can be deferred until the plugin's fields are requested. This means we **cannot guarantee** that the button and columns appear immediately after installing the ZIP. The automatic first-load setup and real TC GUI still require final Windows acceptance testing. We do not modify settings during `DllMain` or silently insert code into the running TC window.

For an explicit setup call if the first-load prompt has not appeared, use the same installed WDX with the Windows built-in rundll32 (there is still no separate application):

`rundll32.exe "C:\Path\To\SymlinkInspector.wdx64",TCLinkSetup "C:\Path\To\wincmd.ini"`

Close TC before executing that command to avoid its shutdown overwriting INI changes. The setup reports any unsupported profile redirects, never knowingly overwrites existing user commands, and backs up settings.

## Button and columns

After successful setup, a **Create Links** button is added via `em_TCSymbolicLinks_Create` in `usercmd.ini`. It calls:

- Command: full absolute path to Windows system `rundll32.exe` (built into Windows, not found by searching the current directory).
- Parameters: `"<installed-plugin-path>\SymlinkInspector.wdx64",TCLinkCreate --list "%WL" --dest "%T."`

The `%T.` form is intentional: TC's destination placeholder has a trailing backslash; appending a dot prevents Windows command-line parsing from escaping the closing quote. The plugin normalizes the target path before creating links.

The WDX columns added automatically are **Link type**, **Link target**, **Target exists**, and **Hardlink count**. Column view `TC Symbolic Links` is appended to existing custom views and activated in both panels, rather than replacing another view definition. The correct TC `SpecialView` value is preset number + 2 (the first custom preset is `SpecialView=3`).

The integrated dialog supports English, Russian, and Hebrew, Unicode filenames (including Hebrew niqqud), multiple selection, HiDPI, relative symbolic targets, progress, and Stop. An existing destination is never silently overwritten.

## Windows build and tests

Use **Visual Studio 2026**, MSVC v145, and CMake 4.2 or newer:

    cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64 -T v145
    cmake --build build-win --config Release --parallel
    ctest --test-dir build-win -C Release --output-on-failure

Or run `build-windows.cmd`. This builds only **one** plugin binary `SymlinkInspector.wdx64`, creates the installer ZIP, and runs: NTFS reparse tests, UTF-16 selection parsing, native hardlink/junction/symlink tests, WDX ABI tests, and the single-DLL INI integration and `rundll32` export test.

The GitHub Actions workflow also attempts the real Total Commander 11.58 x64 smoke test and records whether its window loaded WDX custom columns. A passing DLL/API test **does not** mean custom columns were visually verified inside TC.

## Linux WDX build

    cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
    cmake --build build-linux --parallel
    ctest --test-dir build-linux --output-on-failure

Linux `.wdx` is compatible with the Double Commander WDX ABI, including its 16-bit UTF-16 `WCHAR` representation. Linux tests cover real POSIX links, Unicode files and malformed NTFS reparse data. This does not replace Windows GUI acceptance.

## Verified Preview status — October 25, 2026

**0.2.1 Preview is available as a single Windows `.wdx64` file.** The
[GitHub Actions VS2026 / Linux / sanitizer run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503)
passed all three required jobs:

- Windows Server 2025 + VS2026 (MSVC v145): **6/6 tests passed**, including
  real Win32 hardlinks, WDX UTF-16 exports, configuration of the TC toolbar
  and both custom-column presets with backups, and Unicode entrypoints called
  through the actual Windows `rundll32.exe`.
- Linux WDX build and Unicode/reparse tests: **4/4 passed**.
- Linux AddressSanitizer + UndefinedBehaviorSanitizer: **4/4 passed**.
- Real Total Commander 11.58 launched in CI. The one-DLL creator window
  successfully created a hardlink for a Hebrew file with niqqud.

**Full TC GUI integration is not yet verified.** GitHub-hosted CI reported
`tc_window_found=false`, `tc_wdx_loaded_in_process=false`, and
`interactive_gui_verified=false`: the TC main window could not be found in
its non-interactive desktop. This does *not* prove the WDX integration is broken,
but it also does *not* prove that the installed columns or button appeared in
Total Commander's real GUI. The first-load setup prompt is not a post-install
hook of `pluginst.inf`; manual interactive confirmation is required for stable.

Download verified CI artifacts:

- [Windows plugin installer ZIP (one WDX DLL, no separate EXE)](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12599843692)
- [Linux native WDX](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12600028345)
- [Real TC smoke-test JSON and screenshots](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503/artifacts/12600063342)
- [Preview PR #1](https://github.com/sgennadi/TC-Symbolic-Links/pull/1)

No open-source license has yet been selected.
