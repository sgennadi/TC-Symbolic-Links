## 0.2.9 (Preview) — NI Package Creator-inspired graphics and safe icon restore

- Bring the native maker visually in line with Nipkg-Creator: navy/blue
  gradient header, antialiased blue/green waves, rounded white source and
  target cards, green F5 Create, white/blue secondary controls and
  contextual blue/green/red status cards.
- Draw crisp GDI+ rounded edges at 100%, 150% and 200% scaling;
  remain one native WDX64 module with no companion EXE or .NET dependency.
- Add a safe restore-icons helper which matches command, parameters and
  caption against saved .bar snapshots and restores only missing icon
  references; never replace existing user buttons or usercmd.ini.
- Adopt v10 selection preflight: check missing/inaccessible paths before
  creation and reject mixed input when an explicit file-only/folder-only
  mode is selected; Automatic still supports both.
- Include recovery regression tests in Windows VS2026 CI and package the
  new helpers with the existing toolbar and custom-command restore tools.

## 0.2.8 (Preview) — self-link guard, embedded icons and colour

- Block source/target pointing into the same directory, including 8.3 and
  trailing dot aliases, and prevent recursive junction/symlink creation.
- Preflight shows a localized warning and disables F5; editing destination
  to a safe directory re-enables F5.
- Add an original chain-link ICO resource (16/32px) to the WDX DLL, and use
  that icon for the plugin-created TC toolbar button.
- Add Win32 vector glyphs for Create, Stop, Close, source folder, target and
  status, with tooltips and hover feedback.
- Add coloured RichEdit success/error output and animated blue progress strip.
- Extend Windows CI for the user's real same-folder and icon regression.

## 0.2.7 (Preview) — Total Commander-style native link maker

- Replace the large single-column WDX Maker interface with compact left/right
  source and target file panels, path strips and familiar Windows file icons.
- Display a native report-view table (Name / Type / Full path), with full-row
  selection and flexible column widths. Show destination link previews before
  creation, and keep detailed success/error results after creation.
- Use system Windows colours, smaller type, a conventional status/action strip,
  plus real F5 Create and Esc Stop/Close keyboard accelerators.
- Preserve English, Russian, Hebrew, Unicode, relative-path help and DPI-resize.
- Extend the Win32 Junction GUI test to enforce two-pane layout and F5 create.
- No changes to selected source data, NTFS creation logic, toolbar behaviour or
  user Total Commander preferences.

## 0.2.6 (Preview) — real user folder selection and link maker UI

- Fix `%WL` directory paths that end with a backslash; previously the GUI
  said `Invalid source name` and never created directory symlinks or junctions.
- New explicit Win32 regression with trailing-slash Hebrew folder selection,
  actual WDX UI, Junction creation, link traversal and error-message checks.
- Modernized native responsive Unicode / HiDPI maker layout and added
  contextual explanations for every link type and relative symlink targets.
- Display Error 1314 guidance only for real privilege errors.
- Add optional, read-only toolbar inspection utility to identify the original
  custom icon bar. Keep the safe 0.2.5 toolbar selection behavior unchanged.

## 0.2.5 (Preview) — critical customized-toolbar safety fix

- Fix **real 0.2.4 user report**: the saved button's `%COMMANDER_PATH%` was
  interpreted by Total Commander as `%C` (selected filename) plus trailing
  text, yielding a RunDLL "specified module not found" error. Generate an
  absolute plugin path in .bar parameters and reject any percent signs.
- STOP replacing the selected startup toolbar for profiles without an
  explicit `[Buttonbar] Buttonbar=`. Modify real DEFAULT.BAR in place only
  when writable and backed up; otherwise refuse without modifying settings.
- Refuse legacy 0.2.1 and 0.2.4 plugin-created shadow bars until original
  toolbar is recovered.
- Repair helper restores ONLY `[Buttonbar] Buttonbar` from the original backup
  for both known broken shadow bars, with a timestamped snapshot and no
  changes to other TC settings, usercmd.ini, or .bar files.
- Add Windows tests for a plugin physically installed beneath
  `%COMMANDER_PATH%`, persisted absolute .bar DLL arguments, system rundll32
  loading, custom-icon preservation, read-only refusal, and both recovery
  profiles. Upgrade first-load marker to 205.

## 0.2.4 (Preview) — October 25, 2026

- Fix valid Total Commander profiles without an explicit `[Buttonbar] Buttonbar=`.
- When TC uses `DEFAULT.BAR`, copy every existing button, icon and subbar to a
  verified per-user toolbar and append Create Links there. Never mutate the
  shared installation DEFAULT.BAR, even when running elevated.
- Activate the verified per-user copy through `%COMMANDER_INI_PATH%`, preserving
  Unicode home directory paths. Back up and roll back settings on failures.
- Keep explicit personal toolbar profiles unchanged; retain prior quoting fixes.
- Add dedicated Windows CTest regression for missing buttonbar setting,
  read-only global default, backup, collision, idempotence and failure safety.
- Increment one-time integration marker to version 204.

# Changelog

## 0.2.3 (preview)

- Treat 0.2.2 auto-configuration marker as outdated (upgrade marker to Version=203), so previously installed broken buttons are repaired on first-load reconfiguration, without adding another button.

- Fix **critical root cause of no-op toolbar button**: Win32 INI readers strip the first and last double quotes when an entire `paramN` value begins and ends in quotes. Add harmless trailing `--tc-launch` (ignored by DLL GUI) to preserve DLL/target quoting, verify exact readback before committing and roll back on mismatch.

- Add an end-to-end Windows regression that launches the **exact command installed in the user's active .bar**, replaces TC `%WL` / `%T` placeholders, opens the one-DLL maker GUI and creates an actual Hebrew hardlink. Previous CI launched a hand-written command instead of the button's persisted settings.
- Add a non-destructive **toolbar-only** recovery script for users affected by 0.2.1. It restores only the `[Buttonbar] Buttonbar` key from `wincmd.ini.tcslinks.bak`, retaining all other INI settings and leaving `usercmd.ini` and every `.bar` file unchanged. Requires user consent and creates an additional rollback snapshot.
- Add a PowerShell Windows regression test for the surgical toolbar restore and a separate CI recovery artifact.
- Keep one WDX64 DLL, exact original toolbar selection, native system rundll32 and no external project EXE.


## 0.2.2 (preview: regression fix, Windows CI pending)

- **Critical restore**: 0.2.1 could hide all custom TC toolbar buttons by replacing the active `.bar` reference. Never switch `wincmd.ini [Buttonbar] Buttonbar` again.
- Append one button to the **explicit active `.bar` in place**, after backing up both the bar and INI. Existing buttons, menus and subbars are retained.
- Remove reliance on `em_TCSymbolicLinks_Create`: direct button calls system `rundll32.exe` with the Unicode WDX `TCLinkCreate` entry. Stop converting or editing `usercmd.ini` entirely.
- Reject unsafe/ambiguous toolbar configurations and legacy 0.2.1 shadow-bar profiles until user restores their backup.
- Add a safe recovery script for backed-up `wincmd.ini` and `usercmd.ini` with timestamped copies.
- Add comprehensive regression tests: untouched original toolbar selection, three existing buttons, direct command, unmodified custom commands, redirected/unknown refusal, backup fidelity, idempotence and native Unicode `rundll32` invocation.
- Keep the single WDX DLL approach; no maker EXE. Automatic columns for both panels remain opt-in at first WDX initialization.


## 0.2.1 (preview)

- Fix Windows VS2026 integration test failure: call the base export name in
  `rundll32.exe` so Windows picks the `...W` UTF-16 entrypoint correctly;
  avoid accidental ANSI argument delivery for Hebrew filenames.

- Correct Total Commander `SpecialView` offset: custom view #N must be N+2;
  add a portable regression test covering all 90 supported view numbers;
  ensure both panels show details and test the selected third custom preset.
- Use the trusted system directory for `rundll32.exe`, avoiding executable
  search-path ambiguity when a selected folder contains an EXE named rundll32.
- Prevent overwriting an unrelated `em_TCSymbolicLinks_Create` user command;
  accept only upgrades of the plugin-owned entry.
- Add Windows integration regressions for both the view offset and a colliding
  custom command. VS2026 CI passed all 6/6 native tests; Linux and sanitizer
  jobs also passed. The real TC executable started and the maker GUI created
  a Hebrew/niqqud hardlink; WDX custom-column rendering inside TC is unverified.

## 0.2.0 (Preview — pending Windows acceptance)

- Single Windows plugin binary `SymlinkInspector.wdx64`; delete obsolete `TCSymlinkMaker.exe` and its build target.
- Embed Win32 link maker UI and add `TCLinkCreateW`, `TCLinkSetupW`, `TCLinkProbeW` exports for `rundll32.exe`.
- Optional first-load TC integration adds one toolbar button and a custom-column preset to both panels, retaining previous settings and writing backups.
- Preserve Unicode path names in `usercmd.ini` using UTF-16LE; use `%COMMANDER_INI_PATH%` to keep ASCII `wincmd.ini` safe when a profile folder contains Hebrew characters.
- Fix quoted `%T` trailing-backslash issue with `%T.`.
- Extend native Windows tests with one-module installer INI configuration, idempotence and `rundll32` Unicode export checks.
- Update Windows CI packaging and GUI test to run only through the WDX (no separate application).
- 3/3 local Linux tests passed; new Windows CI execution is still pending.

## 0.1.5 (Preview)

- Initial verified GitHub Actions build of WDX and external GUI executable on VS2026; all four Windows tests passed.
- Attempt actual Total Commander 11.58 process launch and Hebrew/niqqud GUI link creation. WDX column rendering inside TC not confirmed.
