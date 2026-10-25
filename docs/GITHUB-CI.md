# GitHub Windows CI: TC Symbolic Links 0.1.5

This project needs its **own repository**, e.g. `Digitoxin1/TC-Symbolic-Links`; it should not be merged into the unrelated ISO-WCX source tree.

1. Create an empty GitHub repository named `TC-Symbolic-Links` under your account. Leave GitHub's initialization checkboxes disabled.
2. Upload the **contents of the source archive root**, including `.github/workflows/windows-build.yml` (avoid uploading a parent `TC-Symbolic-Links-0.1.5` directory instead).
3. In **Actions**, enable workflows if GitHub requests confirmation, and run **TC Symbolic Links - Windows VS2026 and Linux Unicode** via **Run workflow** from the `main` branch. Pushes to main also run it.
4. Three jobs must pass: `windows-vs2026`, `linux-wdx-unicode`, `linux-sanitizers`. The Windows job compiles with CMake `Visual Studio 18 2026` and `v145` on the `windows-2025-vs2026` runner.
5. Download the workflow artifact **TC-Symbolic-Links-win64-VS2026**. Inside it are the **TC-Symbolic-Links-win64.zip** package and its `.sha256` file. Test the plugin in Total Commander x64 using `docs/MANUAL-TESTS.md` before declaring a stable release.
6. Only after the workflow passes, a tag `v0.1.5` can automatically publish a GitHub **prerelease** with Windows and Linux bundles. **A tag does not certify GUI integration.**

Commands on a machine with Git, and with the repository already created:

    git init
    git add .
    git commit -m "TC Symbolic Links 0.1.5 preview"
    git branch -M main
    git remote add origin https://github.com/Digitoxin1/TC-Symbolic-Links.git
    git push -u origin main

No workflow or GitHub release has been run from this Linux container. A repository must exist and its GitHub Actions checks must pass first.

## Additional real-app test (0.1.5)

The Windows job executes `scripts/windows-tc-smoke.ps1` after CTest.
It verifies the official Total Commander 11.58 installer and starts
`TOTALCMD64.EXE` with an isolated WDX configuration and Hebrew paths. The
`TC-RealApp-Smoke-Windows` Actions artifact reports which additional GUI
checks actually succeeded. See [`REAL-TC-CI.md`](REAL-TC-CI.md) for detailed
interpretation and restrictions.
