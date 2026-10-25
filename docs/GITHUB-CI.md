# GitHub Actions CI for 0.2.1 Preview

Repo: https://github.com/sgennadi/TC-Symbolic-Links

1. Push **the 0.2.1 source tree** to `main` (not the older 0.1.5 source).
2. Actions → `TC Symbolic Links - VS2026, real TC GUI smoke and Linux Unicode`.
3. All three required jobs should pass: `windows-vs2026`, `linux-wdx-unicode`, `linux-sanitizers`.
4. Windows job should build `SymlinkInspector.wdx64` only; its five CTest suites include `tc_one_dll_installer_and_rundll32`.
5. Download `TC-Symbolic-Links-win64-VS2026`. The inner plugin ZIP must have `pluginst.inf`, `SymlinkInspector.wdx64`, README and test instructions **without any EXE**.
6. Examine `TC-RealApp-Smoke-Windows/tc-smoke.json`: process start is not proof that WDX custom columns loaded inside TC. The latter requires a real GUI desktop.

The pluginst.inf installer by itself cannot configure toolbar/custom views. The WDX first-load consent is a follow-up step; when WDX is lazily loaded this may occur after plugin installation. Keep Preview until tested on interactive Total Commander.
