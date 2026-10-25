# TC Symbolic Links 0.2.2: GitHub CI and recovery

Repository: https://github.com/sgennadi/TC-Symbolic-Links

0.2.1 had a real-user regression: it replaced the active button bar and could
break custom toolbar commands. Do not deploy 0.2.1. Restore the original
wincmd.ini and usercmd.ini backups using scripts/Restore-TC-Buttons-0.2.1.cmd.

## 0.2.2 Windows CI must verify

- Visual Studio 2026 MSVC v145 x64 builds only SymlinkInspector.wdx64.
- All WDX ABI, Hebrew and reparse parser tests pass.
- Native `tc_one_dll_installer_and_rundll32` test verifies the original active
  `.bar` reference remains **unchanged**, every prior button stays, and the
  Create Links button uses a direct trusted system rundll32 command.
- `usercmd.ini` is not changed byte-for-byte; backed-up `.bar` and wincmd.ini
  equal the original pre-install bytes; repeated configuration is idempotent.
- Unsafe legacy shadow bar and ambiguous bar profiles fail without touching
  the user's data.
- GitHub Actions real TC smoke attempts launch and Hebrew GUI hardlink
  creation, and explicitly records missing visual custom-column acceptance.

Linux builds/UTF-16 tests and sanitizer jobs must also pass. Keep PR in draft
until Windows CI is green; do not call it stable until interactive TC button
and both column panes are confirmed on real customized Windows profiles.

See REGRESSION-0.2.1.md for cause and recovery.
