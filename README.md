# TC Symbolic Links 0.2.5 Preview

**All-in-one Total Commander x64 WDX for inspecting and creating Windows links.**
The installer package contains `SymlinkInspector.wdx64` (link inspector and
Unicode link-creation UI). **No companion EXE is needed.** The button calls
the installed DLL through the system `rundll32.exe`.

> **Safety update for people affected by 0.2.1 and 0.2.4**: Both versions
> could switch the active button bar and hide existing icons. Version 0.2.4
> also wrote `%COMMANDER_PATH%` in a TC button parameter; TC expanded `%C`
> as the selected filename, so the DLL could not load. Do not keep using
> those broken generated buttons.

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
   do not retry the plugin: preserve your backups for diagnosis.

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
   for a passing 0.2.5 build. Inside the artifact there is the Total Commander
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
