# Changelog

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
  custom command. Still pending a native Windows CI build and visual TC test.

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
