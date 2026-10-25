# TC Symbolic Links 0.2.2 Preview — Toolbar Safety Fix (One DLL)

One Unicode-aware `.wdx64` module for **Total Commander x64**. It both inspects links in WDX custom columns and creates Windows symbolic links, NTFS junctions, and hardlinks using an integrated Win32 dialog.

**No `TCSymlinkMaker.exe` and no extra runtime dependencies.** The Create Links button uses the standard Windows `rundll32.exe` to load an exported function from the **same** `SymlinkInspector.wdx64` module. Requires Windows 10/11 x64 and Total Commander x64.

The Linux build is a native ELF `.wdx` for Double Commander. It provides inspection only, using the same field names; Windows link creation is unavailable on Linux.

## Installation and automatic configuration

1. Open the **installer ZIP** in Total Commander by double-clicking or pressing Enter (do not use Ctrl+PgDn). `pluginst.inf` offers installation and registers `SymlinkInspector.wdx64` as a content plugin.
2. **When TC first loads the WDX**, the plugin offers one-time configuration (English, Russian, Hebrew). Choose **Yes** to have it create a toolbar button and custom-column preset, then activate the preset for **both panels**. Restart Total Commander so its toolbar and panel layouts reload.
3. **The existing toolbar remains active and is never replaced.** The plugin appends its button directly to the explicitly configured active `.bar` and makes a `.tcslinks.bak` copy first. Existing column presets and `usercmd.ini` are left intact; columns are configured in both panels. Repeated configuration does not duplicate the button or columns. If TC uses an unknown/redirected bar, the installer refuses to guess instead of altering another toolbar.

**Important Total Commander limitation:** the built-in `pluginst.inf` installer registers WDX plugins only; it does **not** offer a supported post-install hook for button bars and custom-column presets. The optional setup runs from the WDX when TC loads it, not from the installer itself. WDX loading can be deferred until the plugin's fields are requested. This means we **cannot guarantee** that the button and columns appear immediately after installing the ZIP. The automatic first-load setup and real TC GUI still require final Windows acceptance testing. We do not modify settings during `DllMain` or silently insert code into the running TC window.

For an explicit setup call if the first-load prompt has not appeared, use the same installed WDX with the Windows built-in rundll32 (there is still no separate application):

`rundll32.exe "C:\Path\To\SymlinkInspector.wdx64",TCLinkSetup "C:\Path\To\wincmd.ini"`

Close TC before executing that command to avoid its shutdown overwriting INI changes. The setup reports any unsupported profile redirects, never knowingly overwrites existing user commands, and backs up settings.

## Recover your original buttons after the 0.2.1 defect

Version 0.2.1 could replace the active button bar with `TCSymbolicLinks.bar` and convert an existing `usercmd.ini` into UTF-16LE. If custom buttons disappeared or the created button did nothing, **stop using 0.2.1 and restore the backups first**:

1. In Total Commander, select **Help > About Total Commander** and note the exact active `wincmd.ini` path. Close all Total Commander instances.
2. Run `scripts/Restore-TC-Buttons-0.2.1.cmd` and choose that `wincmd.ini` in the file picker. Confirm `RESTORE`. The script saves the current INIs to timestamped files and restores any available `wincmd.ini.tcslinks.bak` and `usercmd.ini.tcslinks.bak`.
3. Reopen TC. Your original button bar reference and user commands should be back. Do not delete existing `.bar` files.
4. Version 0.2.2 offers safe integration again. It refuses to touch a legacy `TCSymbolicLinks.bar` shadow; **restore first**. A TC restart is required before the new button is usable.

**Important:** restoring the full pre-0.2.1 `wincmd.ini` also reverses other configuration edits made after that backup. The recovery script saves your current INIs first.

## Button and columns

After successful setup, a **Create Links** button is appended directly to the active `.bar`, keeping its original path unchanged. **`usercmd.ini` is never modified, re-encoded or required.** The button calls:

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

## CI status and outstanding acceptance — October 25, 2026

**0.2.2 is a safety fix awaiting Windows CI and interactive user acceptance.**
The previous [0.2.1 GitHub Actions run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40197680503)
passed Windows unit tests, and the real WDX maker GUI created a Hebrew hardlink.
However, these tests missed a live-user regression: 0.2.1 overwrote the active toolbar
reference, hid custom buttons, and routed its own button through `usercmd.ini`.

The new Windows integration regression explicitly requires:

- The original `Buttonbar` path never changes and every existing bar button remains.
- A new button is appended **directly** with a system `rundll32.exe` command and a valid `TCLinkCreate` Unicode parameter.
- `usercmd.ini` remains **byte-for-byte identical** (including unrelated `em_` entries).
- Original `.bar` and INI files are backed up, configuration remains repeatable, and unrelated buttons are never modified.
- Both panels use the intended WDX preset while existing presets are preserved.
- Unsafe ambiguous configurations and legacy broken shadow bars fail without modification.

**Full live TC button clicks and WDX column rendering still need interactive testing.**

[Preview PR #1](https://github.com/sgennadi/TC-Symbolic-Links/pull/1)

No open-source license has yet been selected.
