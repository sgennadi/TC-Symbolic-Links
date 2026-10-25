# Real Total Commander smoke test, 0.2.1

GitHub Actions downloads and verifies official Total Commander 11.58 x64 and uses an isolated temporary profile with Hebrew source/target directories. On Windows it launches `TOTALCMD64.EXE` and asks TC to activate custom WDX columns when a GUI window is available. The hosted runner may not expose an interactive desktop; if so custom-column acceptance remains unverified.

The maker is now part of **the same WDX DLL**, so the test launches the system x64 `rundll32.exe` with `SymlinkInspector.wdx64,TCLinkCreateW`, passing a real UTF-16LE BOM-bearing `%WL` selection file and `%T` destination. It attempts the GUI Hardlink action with Hebrew/niqqud paths and validates the resulting file.

`tc-smoke.json` reports TC process start, actual main window, WDX module loaded, maker window, Hebrew hardlink created and full GUI acceptance as independent booleans. Never infer GUI success from process creation, a passing WDX ABI test, or the CI job's overall success.

No TC license key is supplied or bypassed. The hosted runner may show the publisher's trial reminder; only the visibly requested button can be selected.
