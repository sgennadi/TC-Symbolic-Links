# Changelog

## 0.2.3 (preview)

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
