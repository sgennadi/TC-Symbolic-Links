# TC Symbolic Links 0.2.16 Preview

**All-in-one Total Commander x86/x64 WDX for inspecting and creating Windows links.**
The single **TC-Symbolic-Links-Installer.zip** contains a selective
PowerShell installer and separate x86/x64 WDX payload directories. It installs
**only the DLL matching the running Total Commander**; no permanent companion
EXE is needed. Native buttons call that WDX through system `rundll32.exe`.

> **Safety update for people affected by 0.2.1 and 0.2.4**: Both versions
> could switch the active button bar and hide existing icons. Version 0.2.4
> also wrote `%COMMANDER_PATH%` in a TC button parameter; TC expanded `%C`
> as the selected filename, so the DLL could not load. Do not keep using
> those broken generated buttons.

## 0.2.16 Preview — one ZIP, installation matches Total Commander architecture

**Resolved:** 0.2.15 shipped a `pluginst.inf` root ZIP containing both
`SymlinkInspector.wdx` and `SymlinkInspector.wdx64`. Total Commander's
native ZIP installer extracts **every** file, even when running only x64 TC.

Download **`TC-Symbolic-Links-Installer.zip`**, extract it **completely**,
then launch **`Install_TC_Symbolic_Links.cmd`** (preferably from TC itself).
The installer detects the **invoking Total Commander process**, not whether
the operating system is 64-bit. When both TC architectures are open, or
the invoking TC cannot be identified, it asks the user rather than guessing.

- TC x64 -> install and register **only** `SymlinkInspector.wdx64`.
- TC x86 -> install and register **only** `SymlinkInspector.wdx`.
- Explicit `-Architecture x64` / `x86`, `-IniPath`, `-Destination`
  overrides are supported for portable/multi-profile environments.
- Separate architecture-specific directories prevent the old wrong-bitness
  DLL from being copied by the new installer. Existing unrelated modules
  are never deleted. An old pair of DLL files can safely remain in its
  original, now **unregistered**, directory until the user removes it.
- The bootstrapper identifies the target TC architecture *before*
  requesting closure, then **waits for every TC process to exit** before
  modifying wincmd.ini (bounded timeout; no forced termination). This
  avoids TC's normal shutdown save overwriting the new registration.
- Existing WDX registration is reused; otherwise only the first free
  `[ContentPlugins]` slot is added. x64 sets the matching
  `[ContentPlugins64]` flag; x86 clears that flag for its own slot.
- The installer creates an **immutable timestamped backup** of
  `wincmd.ini` before writing, backs up any replaced WDX, verifies
  SHA-256 after copying and rolls back on an error. It does **not**
  overwrite custom buttons, `.bar` files, `usercmd.ini`, custom
  columns, or unrelated plugin registration.
- Restart TC, then allow the native WDX to safely rebind its three
  existing buttons to the selected architecture. The first-load setup
  marker moves to 216 so 0.2.15 profiles are offered this rebind.

**Important:** the outer selective-installer archive has deliberately
**no root `pluginst.inf`**. Total Commander has no built-in file filter
by architecture in that format. The older
`TC-Symbolic-Links-win32-win64.zip` still exists ONLY as a
backward-compatible **Update Manager transport** for versions 0.2.15+;
**do not manually install this legacy ZIP** through TC. The new WDX
Update Manager verifies the complete transport ZIP but replaces
**only the installed DLL architecture** after Total Commander closes,
leaving the other module and `pluginst.inf` untouched.

Windows CI verifies clean x64-only / x86-only installation, repeated
installation, correct TC INI registration, preserving personal
toolbars/columns/commands and architecture-only embedded updates.

## 0.2.15 Preview — built-in Update Manager (no companion EXE)

**Inside the existing native Maker**, use **Check for Updates** to open
the Update Manager. There are no extra installation scripts, helper EXEs,
toolbar replacements, or changes to the existing three symlink buttons.
The WDX embeds the trusted Update Manager script as a Win32 RCDATA resource,
and runs it through the Windows-provided PowerShell only when needed.

The NI-inspired Update Manager has English/Russian/Hebrew UI, a channel
selector (**Preview / Stable**), **Check for Updates**, **Install Update**,
and **Never check automatically**. The latter disables startup checks
without preventing manual checks. Automatic checks run at most once per
24 hours in the current Windows profile. The check happens outside the
Total Commander WDX startup callback: it does not block TC's UI.

**Release security:** The updater uses the exact official repository
`sgennadi/TC-Symbolic-Links` GitHub Releases API; only known-version
releases with exactly one `TC-Symbolic-Links-win32-win64.zip` asset and a
valid GitHub-reported **SHA-256 digest** are eligible. It rejects
untrusted download hosts, missing/mismatched SHA-256, duplicate or missing
ZIP payloads, wrong x86/x64 PE architectures and incorrect `pluginst.inf`.
Only the expected root DLLs and `pluginst.inf` are extracted; downloaded
ZIP contents are **never executed**.

After explicit confirmation the ZIP is downloaded, verified and staged in
`%LOCALAPPDATA%\TCSymbolicLinks\Updates`. A temporary separate
Windows PowerShell process waits for the user to close **all** x86 and x64
Total Commander windows; it never terminates them. The installer backs up
the existing x86/x64 WDX files and pluginst.inf, then replaces only those
files in the **same installed plugin directory**. Every installed payload
is hashed again. Errors trigger best-effort rollback from the preserved
backups with an explicit error report; rollback failures are never hidden.
`wincmd.ini`, `usercmd.ini`, personal .bar files, custom columns and
other plugins are **not modified** by the update operation.

For direct access from Total Commander, the same DLL exports
`TCLinkUpdateW` through `rundll32` as `TCLinkUpdate`; normally it is
unnecessary because the Maker already contains the update button.

**Preview releases** are published automatically only after successful
Windows x86/x64, Linux and SMB CI on the preview branch. Each GitHub
Release tag is `v0.2.15-preview.<workflow-run-number>`, and both DLLs
embed the matching build version. Stable channel intentionally ignores
all Preview releases. The version shown in the Update Manager is an
installed DLL build version, not merely the ZIP filename. This release
mechanism cannot fetch short-lived PR/CI artifacts as if they were
official published GitHub Releases.

**Testing:** Offline Windows CI validates version ordering, channel
filtering, release URLs, SHA-256 mismatch rejection, PE machine types,
ZIP validation and simulated partial-install rollback. These tests
do not replace manual checks of your personalized Total Commander
profile, Windows user privileges, third-party SMB servers or enterprise
software restrictions on PowerShell.

## 0.2.14 Preview — three integrated native buttons with distinct icons

The WDX installer configures three commands in the **existing** Total
Commander startup toolbar. It never switches to a new .bar or removes
your existing buttons, custom icons, personal commands or WDX column sets.

1. **TC Symbolic Links - Create links**: auto-detects file vs directory,
   opens the common native Maker with Automatic selected.
2. **TC Symbolic Links - File symlink**: opens with File symbolic link
   preselected; file-only selection is enforced before writing.
3. **TC Symbolic Links - Directory symlink**: opens with Directory
   symbolic link preselected; directory-only selection is enforced.

All three use the **same** `SymlinkInspector.wdx`/`.wdx64` DLL, native
`rundll32.exe` and `TCLinkCreate` entry point with optional
`--mode file`/`--mode directory`; no helper EXE is installed.
Three embedded icon resources (Auto chain, blue file, gold folder) at
indexes 0, 1 and 2 include 16/32/48/64px images with alpha transparency.

For existing 0.2.13 installations the Auto command is repaired **in place**;
only missing File/Directory buttons are appended. Repeated setup leaves
exactly the same button count. If a different command owns one of the
exact menu labels, installation aborts without changing the user's files.
The installer saves an immutable first-time snapshot and a fresh
pre-update backup for every affected configuration. On any write failure,
it attempts byte-for-byte rollback. `usercmd.ini` is never edited.

When switching TC x86 and x64 with a shared toolbar, both the
`System32` and `SysWOW64` versions of `rundll32.exe` are recognized as
belonging to this plugin; rebinding is offered instead of silently
duplicating buttons. Auto-setup version marker increments to 214.

## 0.2.13 Preview — validate SMB creation separately from traversal

**What is new:** creating an SMB reparse-point symlink and successfully
*opening* its target are different operations. Even after a successful
`CreateSymbolicLinkW`, Windows can refuse traversal because of
client symlink-evaluation policy, share/NTFS permissions, or a missing target.
The maker now reports **Created with warnings** rather than claiming all
links are usable when a read-only `CreateFileW` traversal check fails.

- The result pane distinguishes genuine creation failures from successful
  creation with failed reparse readback or target traversal.
- Warnings show the original Win32 error code and a localized
  English/Russian/Hebrew explanation. No automatic security changes,
  elevation or permission retries are performed.
- GitHub Actions **optionally** creates a unique ephemeral Windows SMB share,
  owned by the CI identity, and runs both x86 and x64 real symlink tests.
  Each test creates a file symlink, a directory symlink and checks their
  reparse type and traversal separately. It also rejects SMB junctions
  without leaving placeholder directories.
- CI attempts a temporary mapped drive and tests portable relative targets
  between a mapped destination and UNC source on the same SMB share.
- Logs show **PASS**, **BLOCKED/SKIPPED**, or **FAIL** independently. The
  scripted cleanup removes only the temporary share, drive mapping and
  isolated test directory; failures in cleanup fail CI.
- When CI cannot provision the SMB share (e.g. runner permission limits),
  it reports **SKIPPED** visibly. A passing CI run alone never proves
  support by arbitrary Windows Server, Samba or NAS implementations.

## 0.2.12 Preview — one ZIP for Total Commander x86 and x64

The **same installer ZIP** contains `SymlinkInspector.wdx` (x86),
`SymlinkInspector.wdx64` (x64), `pluginst.inf`, documentation,
and a `Recovery/` directory with toolbar/icon and custom-command repair
scripts. There is **no companion EXE**. Total Commander selects the
architecture-matching WDX automatically when installed from `pluginst.inf`
(the INF registers the base `.wdx` file).

- Both variants are compiled with Visual Studio 2026 (v145, C++ latest),
  including native CTest checks on Windows.
- x86 exposes **undecorated** WDX and `rundll32` exports using a Win32
  module-definition file; x64 keeps its existing exports.
- First-load setup is available in `TOTALCMD.EXE` (32-bit) as well as
  `TOTALCMD64.EXE` (64-bit), with separate completion markers.
- Existing buttons, usercmd.ini, custom columns and the selected .bar are
  backed up and preserved. The installer must never replace an unrelated
  button with a matching caption.
- If 32- and 64-bit TC use a **shared** .bar, its one Create Links button
  references one architecture-specific DLL at a time. On switching TC
  architectures, the WDX offers to safely rebind that button; the existing
  button and icon are updated in place, never duplicated. Declining leaves
  the profile unchanged.
- CI verifies the WDX binaries and setup logic, but full interactive TC
  toolbar acceptance still requires a real, licensed GUI session.

## 0.2.11 Preview: generic SMB file and directory symlinks

Create native Windows file and directory symlinks *inside* UNC SMB shares or
connected mapped drives using the existing single-WDX `CreateSymbolicLinkW`
implementation. No SSH, extra EXE, hard-coded NAS/Samba vendor, or `.lnk`
substitution is required.

- **Same share:** calculate optional relative targets within the same
  `\\server\share` only. Different SMB shares are separate volumes.
- **Mapped drives:** resolve `Z:\path` to UNC through
  `WNetGetUniversalNameW` when the network provider supports it.
- **Different shares/servers:** store an absolute UNC target and warn about
  Windows Remote-to-Remote symlink evaluation and target-share permissions.
- **Remote-to-local:** warn that another client may not have the local disk,
  and Remote-to-Local evaluation may be blocked.
- **Errors:** distinguish denied access, unavailable shares, unsupported
  operations, and insufficient Windows symlink privileges. Error 5 alone
  does not prove a server-side permission problem.
- **Verification:** after successful remote creation, attempt to inspect
  the reparse point read-only and warn if type verification is unavailable.
- **Remote junctions:** reject before creating a potentially orphaned
  placeholder folder; use Directory Symlink instead.
- **No policy changes:** never silently set `SymlinkEvaluation`, edit
  Samba options, or replace a real symlink with a shortcut.
- A Windows SMB reparse-point symlink and a Unix server's native
  POSIX `ln -s` symlink are not necessarily the same on-disk object.

Automated regression tests cover UNC share boundaries, relative paths,
network diagnostics, and existing Unicode/TC behaviour. Real SMB creation
must still be verified against each server type; SMB endpoints that do not
implement reparse-point creation cannot be made to support it by the client.

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
