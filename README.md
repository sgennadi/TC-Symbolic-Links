# TC Symbolic Links 0.2.10 Preview

**All-in-one Total Commander x64 WDX for inspecting and creating Windows links.**
The installer package contains `SymlinkInspector.wdx64` (link inspector and
Unicode link-creation UI). **No companion EXE is needed.** The button calls
the installed DLL through the system `rundll32.exe`.

> **Safety update for people affected by 0.2.1 and 0.2.4**: Both versions
> could switch the active button bar and hide existing icons. Version 0.2.4
> also wrote `%COMMANDER_PATH%` in a TC button parameter; TC expanded `%C`
> as the selected filename, so the DLL could not load. Do not keep using
> those broken generated buttons.

## 0.2.10 Preview: faithful Nipkg-Creator form controls

This iteration copies not only the palette but also the original
[Nipkg-Creator](https://github.com/sgennadi/Nipkg-Creator)'s
**form-control language**, still entirely within native
`SymlinkInspector.wdx64`:

- **Navy-to-blue banner**, clipped wave graphics and embedded chain logo,
  two white rounded source/target cards and a soft-blue help/preview card.
- **Green Create** with a native vector Play icon; white/blue **Close**
  with an X, Stop with a square, and fully **labelled Browse...** with
  a vector folder icon. Icons are anti-aliased GDI+ paths instead of OS
  font glyphs, including at 150% and 200% DPI.
- Path inputs now sit inside pale `#F9FBFF` rounded fields with a
  visible blue focus border, with the actual Win32 EDIT controls retained
  for Unicode/IME/keyboard accessibility.
- Button widths are measured from their translated text plus fixed icon
  padding, preventing truncated labels; Hebrew action text is right
  aligned. Status text stops before the action buttons instead of
  disappearing underneath them.
- The whole parent client area is double-buffered to avoid GDI resize
  flicker; source files and results keep a familiar TC-style two-pane
  view, colored OK/ERR messages and native file icons.
- Regression coverage checks the actual Maker window has enough room
  for Browse and action buttons, plus offscreen rendering of the
  rounded inputs, focus border and vector folder icon.

The installer/toolbar repair logic is unchanged; existing buttons,
`wincmd.ini`, `usercmd.ini` and personalized `.bar` layouts are
not replaced by these visual improvements.

## 0.2.9: NiPackageCreator-style GUI and safe icon recovery

**Visuals:** the built-in Win32 WDX maker now uses the same visual palette
as the author's [Nipkg-Creator](https://github.com/sgennadi/Nipkg-Creator):
navy-to-electric-blue rounded header, layered blue/green wave graphics,
white rounded source/target cards, clean blue-bordered path/mode controls,
a green primary F5 Create button, blue secondary actions, and a tinted
blue/green/red status card. Native GDI+ antialiasing avoids the ragged edges
of the old GDI RoundRect buttons at 150% / 200% scaling. The WDX remains a
single native `.wdx64`: **no .NET runtime, companion EXE or third-party UI DLL**.

**Recovered toolbar icons:** `Restore-TC-Toolbar-Icons.cmd` is now part of
the separate recovery ZIP. It compares the **real** active `.bar` with
the immutable `.tcslinks.bak` and per-update snapshots, matches each
button's command + parameters + caption, and restores **only missing**
`buttonN` icon references when the original is unambiguous.
It saves a dated, verified rollback copy first. Existing icons, other buttons,
`wincmd.ini`, `usercmd.ini` and the toolbar selection are not edited.
If the entire toolbar was switched by 0.2.1/0.2.4, use
`Restore-TC-Toolbar-Only.cmd` first. The older user-provided
`Install_TC_Directory_Symlink_v10.ps1` intentionally leaves button bars
untouched; it is **not itself a lost-icon repair tool**.

**Preflight borrowed from v10:** all selected sources are verified before
link creation. Explicit file-only or directory-only modes reject incompatible
and inaccessible selections with localized guidance and create nothing.
`Automatic` still supports mixtures of files and folders. This does not
bypass Samba permissions; UNC directory symlink denials remain visible.

## 0.2.8: clear icons, colour and safety feedback

- **No self-links:** the maker detects identical source/destination folders
  (including paths ending in \`\\.\`, mixed-case spellings, junction aliases and
  NTFS 8.3 directory aliases) and disables **F5 Create**. The destination field
  stays editable; choosing another existing directory restores the action.
- **No recursive links:** prevents creating a selected directory's link inside
  that same selected directory or one of its subdirectories.
- **Purpose-built link icon** embedded as 16/32px resources in the WDX DLL;
  the Total Commander toolbar uses that icon instead of an unrelated
  WCMICONS image. One plugin module remains; no external EXE.
- In the Maker window, familiar **folder / link / warning / stop / close icons**
  accompany text labels, with tooltips in English, Russian and Hebrew.
  Icons use Windows Segoe MDL2 Assets, without bundled fonts.
- Clear blue **F5 Create** and subdued **Esc Close** action buttons, hover
  highlighting, and a compact top accent. Batch execution has an **animated
  progress strip**.
- Built-in Windows RichEdit colours actual **OK** lines green and **ERR**
  lines red; safe preview and helpful error text appear before any operation.
  The old Windows EDIT is retained as a fallback.
- Retains the previous source/target split panes, Unicode, RTL/Hebrew,
  Junction/Hardlink and all installer backup safeguards.

**Preview acceptance:** test the new dialog at 100%/150%/200% scaling,
including the same-folder selection from the user screenshot, and check
the embedded icon renders in a real TC toolbar. The GitHub runner's headless
environment cannot fully confirm a user's personalised toolbar layout.

## 0.2.7 Preview: familiar Total Commander-style Maker window

The native link maker now looks and behaves like a compact Total Commander
operation dialog, while remaining entirely inside **SymlinkInspector.wdx64**:

- **Side-by-side source and destination panels**, with path strips that resemble
  TC's directory bars. The source remains read-only; the opposite panel path is
  editable before creation.
- **Details-view file table** with small Windows icons, Name / Type / Full path,
  full-row selection, and a flexible width when the window is resized.
- **Live destination preview** on the right before creation, changing into
  individual success / Windows-error results when you start the batch.
- Native Windows system colours, compact Segoe UI 9-point controls and a
  traditional bottom command strip instead of large modern cards.
- **F5 = Create Links**, **Esc = Stop / Close**, also visible on the two buttons.
- Keeps automatic/file/folder symlink, junction, hardlink and relative-path
  options, multiline contextual help, Hebrew/Russian/English, and DPI-aware
  resizing including 4K/200% Windows display scaling.
- Keeps the WDX, toolbar integration and existing user configuration handling
  independent of these window layout changes.

The CI verifies that the dialog actually contains two non-overlapping panels,
a native \`SysListView32\` table and source/destination path strips, then
creates an NTFS Junction from a Hebrew folder with the **F5** accelerator.
The real Total Commander GUI shell/custom-column rendering still needs
interactive user validation.

## 0.2.6: folder links and improved maker window

- **Fixed actual user screenshot:** Total Commander sends selected folders with a trailing
  `\` in `%WL`. The old `BaseName()` returned an empty name and falsely reported
  "Invalid source name". The plugin now strips only trailing separators from
  non-root paths, including Unicode folder names, and preserves drive roots.
- Directory Symlinks and NTFS Junctions work for those folder selections.
  A Windows integration test **launches the DLL's GUI** with a UTF-16LE folder
  list ending in `\`, chooses **Directory Junction**, creates the link, checks
  its NTFS type, and confirms that the source content is accessible.
- Refreshed native Windows UI with a clear source and output preview, helpful
  explanations for all five link types, and a permanent explanation of **Relative
  target**. That option is disabled for Junction and Hardlink.
- Fixed misleading errors: "Windows Error 1314 / Developer Mode" now appears
  only for an actual insufficient-symlink-privilege error, not for bad names or
  pre-existing targets. The result pane displays Windows error codes per item.
- **Previously lost custom icons are not reconstructed by reinstalling the WDX.**
  First close TC, restore the original toolbar with the included recovery script,
  and verify the icons. The new `Inspect-TC-Toolbars.cmd` only examines candidate
  `.bar` files and the original INI backup; it does not modify anything.
- Version 0.2.6 does **not** change the `[Buttonbar] Buttonbar=` selection.

## Preserving and recovering your own Total Commander commands

Recent recovery hardening does **not** replace or re-encode usercmd.ini.
The toolbar installer preserves the original `.tcslinks.bak` file, and now
also creates a uniquely named `.tcslinks.preupdate.*.bak` snapshot before
each later configuration write. This protects buttons added after the first
plugin install.

If custom `em_*` definitions are missing, use the recovery artifact's
`Restore-TC-Missing-Commands.cmd` (or its PowerShell script). After you
close Total Commander and select your **actual** `wincmd.ini` from
Help > About, the tool compares `usercmd.ini` against
`usercmd.ini.tcslinks.bak`. It restores **only missing** `em_*` sections,
never replaces existing definitions, creates a rollback snapshot, and verifies
each recovered section. It refuses to guess commands when the old backup is
absent. On error it rolls back the command file.

Recovery is distinct from plugin installation. Restoring a toolbar selection
does not necessarily restore missing `usercmd.ini` definitions, and
restoring commands does not recreate an erased `.bar` layout. Check both.

## First: restore your existing toolbar if its icons disappeared

1. In Total Commander open **Help > About** and note the exact `wincmd.ini` path.
2. **Close every Total Commander window.**
3. Download/extract the recovery package and run
   `Restore-TC-Toolbar-Only.cmd`. Choose the recorded `wincmd.ini`,
   examine the displayed current and backup toolbar references, and confirm
   with `RESTORE`.
4. This changes **only** `[Buttonbar] Buttonbar` when it currently points to
   either `TCSymbolicLinks.bar` (0.2.1) or
   `TC-Symbolic-Links-Default.bar` (0.2.4). It uses the original
   `wincmd.ini.tcslinks.bak`, saves the current INI to a timestamped file,
   keeps all other settings and `.bar` files, and refuses uncertain backups.
5. Start TC and confirm your custom buttons are present **before** installing
   another preview. If the helper refuses or the old bar does not return,
   do not retry the plugin: run `Inspect-TC-Toolbars.cmd` (read-only),
   preserve the candidate `.bar` files, and select the real original bar
   through **Configuration > Change button bar** if needed.

## What changes in 0.2.5

- **Never change or create a `[Buttonbar] Buttonbar=` selection.**
- If a personal .bar is explicitly configured, append/repair **one** Create
  Links button in that file, keeping every existing button and icon.
- If there is no `Buttonbar=` setting, use TC's actual startup `DEFAULT.BAR`
  **only if it is writable**, with a byte-for-byte pre-change backup. If
  protected (e.g., under Program Files), the plugin refuses the toolbar
  operation without changing INI, other buttons, or columns. This is
  intentionally safer than copying/replacing the bar automatically.
- If a legacy plugin-created shadow bar is still selected, refuse to change
  it. Restore the original bar first (see above).
- Write an **absolute DLL filename** into the button's parameters. Never use
  `%COMMANDER_PATH%` or `%COMMANDER_INI_PATH%` there: TC parses `%C` as
  selected-file expansion and breaks the plugin path.
- Verify persisted INI quotes, preserve `usercmd.ini`, prevent duplicate
  buttons and reject unrepresentable/ambiguous plugin paths. Roll back all
  edited files on setup errors.
- Set up a custom-column view `TC Symbolic Links` in **both** panels, with:
  `Link type`, `Link target`, `Target exists`, `Hardlink count`. Existing
  column definitions stay intact.

The WDX plugin is an inspector (WDX content API) and includes the link maker
window as a **Unicode `rundll32.exe` export** called `TCLinkCreateW` (invoked
with base name `TCLinkCreate`). Supported creations: file symlinks,
directory symlinks, NTFS junctions and same-volume file hardlinks. English,
Russian and Hebrew UI, mixed RTL/LTR and Unicode paths are supported.

## Installation

1. Download a **Windows x64 CI artifact** from [GitHub Actions](https://github.com/sgennadi/TC-Symbolic-Links/actions)
   for a passing 0.2.9 preview build. Inside the artifact there is the Total Commander
   installer ZIP; open that ZIP in TC, not with Ctrl+PgDn.
2. When the WDX is first initialized by TC, it offers to configure the
   **startup** toolbar button and the two custom-column views. Confirm only
   after restoring the original toolbar. The TC installer itself has no
   supported post-install toolbar-configuration hook.
3. If your implicit `DEFAULT.BAR` is protected, no toolbar will be modified.
   To use safe automatic setup, first select a **writable personal `.bar`**
   through TC's built-in Configuration > Change button bar interface. Then
   use the plugin's explicit setup export, or reinstall/initialize it.
4. Close/restart TC after setup. Confirm your original buttons and the new
   button. Test the link maker on disposable files before using it in production.

The generated button uses:

- Command: `%SystemRoot%\System32\rundll32.exe` resolved to its real
  **absolute** system path during installation.
- Parameters: `"C:\absolute\path\SymlinkInspector.wdx64",TCLinkCreate --list "%WL" --dest "%T." --tc-launch`

`%WL` and `%T` are **TC button placeholders** that must remain. `%T.` avoids
trailing-backslash quote escaping. The ignored marker prevents the Windows
INI reader from stripping the first DLL-path quote. The WDX's installed path
must be representable in the existing .bar encoding and must not contain a
literal `%` character; otherwise setup refuses safely.

## Building and testing

Windows: Visual Studio **2026**, CMake 4.2+, MSVC v145:

    cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T v145
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

Linux (native `.wdx` for Double Commander):

    cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
    cmake --build build-linux --parallel
    ctest --test-dir build-linux --output-on-failure

Native Windows CI tests simulate a DLL installed **under the actual TC
installation directory**, inspect the persisted .bar, refuse read-only
DEFAULT.BAR, and use the real system rundll32 to load the saved DLL. They
also test preservation of existing buttons, Unicode paths, `.ini` backups and
recovery from both broken 0.2.1 and 0.2.4 shadow bars.

**Preview acceptance remains:** the hosted Windows runner cannot verify
Total Commander's live toolbar/custom columns inside its main window. Do not
publish stable until the user verifies it on a real customized TC profile.
See [PR #1](https://github.com/sgennadi/TC-Symbolic-Links/pull/1) and
[manual acceptance checklist](docs/MANUAL-TESTS.md).

## Source, security and license

Native C++23 Win32/NTFS filesystem APIs, no third-party runtime DLLs.
The Linux WDX uses POSIX system APIs. Existing destination paths are not
silently replaced; symlink creation needs the corresponding Windows privilege
or Developer Mode. No open-source license has been selected (all rights
reserved until licensed).
