# TC Symbolic Links 0.2.4 Preview — One DLL / Toolbar Recovery

One Unicode-aware `.wdx64` module for **Total Commander x64**. It both inspects links in WDX custom columns and creates Windows symbolic links, NTFS junctions, and hardlinks using an integrated Win32 dialog.

**No `TCSymlinkMaker.exe` and no extra runtime dependencies.** The Create Links button uses the standard Windows `rundll32.exe` to load an exported function from the **same** `SymlinkInspector.wdx64` module. Requires Windows 10/11 x64 and Total Commander x64.

The Linux build is a native ELF `.wdx` for Double Commander. It provides inspection only, using the same field names; Windows link creation is unavailable on Linux.

## Installation fix in Preview 0.2.4 (missing Buttonbar=)

Some normal Total Commander 11.58 profiles have no explicit
`[Buttonbar] Buttonbar=` in `wincmd.ini`: Total Commander loads
`%COMMANDER_PATH%\DEFAULT.BAR` as its startup toolbar. Version 0.2.3
wrongly refused such profiles, even when the default toolbar was visible.

Version 0.2.4 handles this case **without touching the shared DEFAULT.BAR**:

- Copies the complete default bar, with all icons, commands and subbars, to
  `%COMMANDER_INI_PATH%\TC-Symbolic-Links-Default.bar`.
- Appends one Create Links button to that per-user copy, then switches the
  previously unspecified startup bar to that verified copy. It never edits
  the installation's original DEFAULT.BAR or `usercmd.ini`.
- Saves `wincmd.ini.tcslinks.bak` and rolls back the changed INI and cloned
  toolbar if any operation fails; refuses name collisions.
- Leaves explicitly configured personal `.bar` paths untouched (adds one
  button in place as in 0.2.3). Existing column sets stay intact.
- The new Windows regression test exercises a missing `[Buttonbar]` section,
  a read-only DEFAULT.BAR, custom subbar buttons, Unicode profile directory,
  idempotence, collision safety and missing-source refusal.

**Important:** TC can display a temporary bar switched with `OPENBAR`.
A WDX cannot reliably identify that temporary bar through `wincmd.ini`, so
0.2.4 integrates the **startup/default** bar. This means no previously saved
button is removed, but an unsaved temporary bar may not be the one edited.
Close and restart Total Commander after approving integration.

## Installation and automatic configuration

1. Open the **installer ZIP** in Total Commander by double-clicking or pressing Enter (do not use Ctrl+PgDn). `pluginst.inf` offers installation and registers `SymlinkInspector.wdx64` as a content plugin.
2. **When TC first loads the WDX**, the plugin offers one-time configuration (English, Russian, Hebrew). Choose **Yes** to have it create a toolbar button and custom-column preset, then activate the preset for **both panels**. Restart Total Commander so its toolbar and panel layouts reload.
3. If your profile specifies a `.bar`, its path remains unchanged and the plugin adds a button there. If **no Buttonbar= setting exists**, TC's default startup bar is copied intact into your personal configuration folder and one button is added to the copy; only this verified copy becomes the new startup bar. Existing buttons, column presets and `usercmd.ini` are preserved. Repeating the setup does not duplicate buttons or columns. Profiles with unknown redirects are rejected safely.

**Important Total Commander limitation:** the built-in `pluginst.inf` installer registers WDX plugins only; it does **not** offer a supported post-install hook for button bars and custom-column presets. The optional setup runs from the WDX when TC loads it, not from the installer itself. WDX loading can be deferred until the plugin's fields are requested. This means we **cannot guarantee** that the button and columns appear immediately after installing the ZIP. The automatic first-load setup and real TC GUI still require final Windows acceptance testing. We do not modify settings during `DllMain` or silently insert code into the running TC window.

For an explicit setup call if the first-load prompt has not appeared, use the same installed WDX with the Windows built-in rundll32 (there is still no separate application):

`rundll32.exe "C:\Path\To\SymlinkInspector.wdx64",TCLinkSetup "C:\Path\To\wincmd.ini"`

Close TC before executing that command to avoid its shutdown overwriting INI changes. The setup reports any unsupported profile redirects, never knowingly overwrites existing user commands, and backs up settings.

## Recover your original buttons after the 0.2.1 defect

Version 0.2.1 could replace the active button bar with `TCSymbolicLinks.bar` and convert an existing `usercmd.ini` into UTF-16LE. If custom buttons disappeared or the created button did nothing, **stop using 0.2.1 and restore the backups first**:

1. In Total Commander, select **Help > About Total Commander** and note the exact active `wincmd.ini` path. Close all Total Commander instances.
2. Run `scripts/Restore-TC-Buttons-0.2.1.cmd` and choose that `wincmd.ini` in the file picker. Confirm `RESTORE`. The script saves the current INIs to timestamped files and restores any available `wincmd.ini.tcslinks.bak` and `usercmd.ini.tcslinks.bak`.
3. Reopen TC. Your original button bar reference and user commands should be back. Do not delete existing `.bar` files.
4. Version 0.2.4 offers safe integration again. It refuses to touch a legacy `TCSymbolicLinks.bar` shadow; **restore first**. A TC restart is required before the new button is usable.

**Important:** restoring the full pre-0.2.1 `wincmd.ini` also reverses other configuration edits made after that backup. The recovery script saves your current INIs first.

## Non-destructive recovery of buttons hidden by 0.2.1 (recommended)

**Do not run 0.2.1 setup again.** Close every Total Commander instance and
run `scripts/Restore-TC-Toolbar-Only.cmd`. It prompts you to select the actual
`wincmd.ini` from TC Help → About, checks that the active bar is the broken
`TCSymbolicLinks.bar` clone, then reads the **original toolbar reference** from
`wincmd.ini.tcslinks.bak`. It changes **only** `[Buttonbar] Buttonbar` and
verifies the result. A timestamped snapshot of the current INI is created first.

Other TC settings (including newly edited custom columns/panel views),
`usercmd.ini`, and all existing `.bar` files are left untouched. If the active
bar is not the broken clone, it refuses to modify anything. If the old backup
is missing, it refuses to guess the right toolbar. Use the older
`Restore-TC-Buttons-0.2.1.cmd` only when you explicitly want to roll the full
INI and user-defined commands back to their pre-plugin state.

The toolbar-only recovery helper is a support script; **it is not a separate
runtime executable and is not needed to create links with the installed WDX**.

## Button and columns

After successful setup, a **Create Links** button is appended directly to the active `.bar`, keeping its original path unchanged. **`usercmd.ini` is never modified, re-encoded or required.** The button calls:

- Command: full absolute path to Windows system `rundll32.exe` (built into Windows, not found by searching the current directory).
- Parameters: `"<installed-plugin-path>\SymlinkInspector.wdx64",TCLinkCreate --list "%WL" --dest "%T." --tc-launch`

The final `--tc-launch` token is an ignored marker that prevents the Windows INI reader from stripping the leading and trailing quotes around the entire parameter string. The `%T.` form is intentional: TC's destination placeholder has a trailing backslash; appending a dot prevents Windows command-line parsing from escaping the closing quote. The plugin normalizes the target path before creating links.

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

## Verified CI status — October 25, 2026

Version **0.2.4 Preview** passed the complete [GitHub Actions run](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40206502697):

- **Visual Studio 2026 x64 / Windows Server 2025: 8/8 native CTest checks passed.**
- The new `tc_implicit_default_toolbar_install` test reproduces a profile with **no `Buttonbar=` setting**, a *read-only* global `DEFAULT.BAR`, existing buttons, icon and subbar entries, and a Hebrew-named profile. It verifies the global original is unchanged, all buttons survive in a per-user copy, the copied bar's WDX command launches through actual `rundll32.exe`, backups exist, and repeat installs add no duplicates. Collision and missing-original safety checks also passed.
- The explicit custom-toolbar test executes the *persisted* button command, opens the one-WDX Maker GUI and creates an actual Hebrew hardlink.
- **Native Linux WDX: 4/4 passed. Linux ASan/UBSan: 4/4 passed.**
- Real Total Commander 11.58 x64 process launch and Hebrew/niqqud hardlink creation via the DLL's Maker GUI succeeded.

**Remaining acceptance:** the GitHub-hosted Windows environment did not expose
Total Commander's main window (`tc_window_found=false`) for a final visual test.
Confirm the toolbar, click-to-open and WDX columns in both TC panels on a real
interactive profile before declaring stable. The official plugin installer
still has no supported post-install hook for WDX button/column customization;
our one-time setup executes when Total Commander first initializes the WDX.

### Build artifacts

- [Windows x64, one SymlinkInspector.wdx64 installer package](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40206502697/artifacts/12603903368)
- [Native Linux WDX](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40206502697/artifacts/12603833622)
- [Restore original toolbar reference after v0.2.1](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40206502697/artifacts/12603329738)
- [Actual Total Commander smoke report/screenshots](https://github.com/sgennadi/TC-Symbolic-Links/actions/runs/40206502697/artifacts/12603569224)
- [Preview PR #1](https://github.com/sgennadi/TC-Symbolic-Links/pull/1)

No open-source license has yet been selected.
