# Real Total Commander acceptance in GitHub Actions (0.1.5)

The `windows-vs2026` GitHub Actions job runs on the explicit
`windows-2025-vs2026` image, which has Visual Studio 2026 and CMake 4.2+.
The job does the normal CMake/CTest Windows tests **and runs the actual**
`TOTALCMD64.EXE`, using `scripts/windows-tc-smoke.ps1`.

## Exactly what is exercised

1. Download official Total Commander 11.58 x64 from
   `https://totalcommander.ch/1158/tcmd1158x64.exe`, verify its pinned SHA-256
   (`2281f3827a8746a597a75287f521690f1291bdb535d7e1269768dbe077a13ed4`)
   and validate the Authenticode signer is Ghisler Software. The installer is
   **not** included in our plugin release archives.
2. Extract the publisher's installer and `INSTALL.CAB` into a disposable
   test folder. This is a portable test invocation, not a machine-wide install.
3. Create fresh Hebrew/English folders and a UTF-16 filename with niqqud.
4. Write a temporary `wincmd-ci.ini` registering the built `.wdx64`, and a
   genuine Total Commander custom-column layout using the WDX fields.
5. Start the **real `TOTALCMD64.EXE`** with `/N`, `/I`, `/L`, `/R`, with
   the test's Hebrew source and destination paths. Fail if the process exits
   immediately.
6. If an interactive desktop is present, identify the `TTOTAL_CMD` window,
   request TC internal command 271 (`cm_SrcCustomView1`) with TC's documented
   `WM_USER+51` message, and inspect whether the actual TC process loaded
   `SymlinkInspector.wdx64`.
7. Start the *built* `TCSymlinkMaker.exe` GUI with a UTF-16LE+BOM `%WL`
   selection list. If its window exists, select Hardlink and activate its
   Create button with Win32 messages, then verify a real Hebrew-named link.
8. Upload `TC-RealApp-Smoke-Windows` artifact containing a `tc-smoke.json`
   report and screenshots if the Windows desktop made them possible.

## GUI honesty and licensing

The publisher's trial/shareware reminder is not patched or disabled. If UI
Automation can see the *requested* numbered button in the message, the test
selects that exact button just as a trial user would. Otherwise the reminder
can prevent the plugin columns from being displayed. No `WINCMD.KEY` is
included or copied by the workflow. Use a licensed Total Commander on a
self-hosted, interactive Windows runner if you want repeatable enterprise
GUI automation with no trial prompt.

`tc-smoke.json` separates actual successes:

- `tc_process_started`: publisher's executable was launched.
- `tc_window_found`: TC's real main window class was found.
- `tc_wdx_registered`: TC's temporary INI contains the plugin entry.
- `tc_wdx_loaded_in_process`: Windows reports this exact WDX module loaded by TC.
- `maker_process_started`: the GUI companion process launched.
- `maker_hebrew_hardlink_created`: a hardlink was created through the GUI.
- `interactive_gui_verified`: all relevant GUI and plugin-loading checks passed.

**Do not claim end-to-end GUI acceptance** unless
`interactive_gui_verified` is true. A headless runner may only pass process,
installer and ABI tests; screenshots may not be available. You can invoke the
script manually on an interactive Windows PC or runner with `-RequireFullGui`
to make incomplete GUI acceptance fail the step.

## How to run

After creating a repository containing this project, use **Actions** ->
**TC Symbolic Links - VS2026, real TC GUI smoke and Linux Unicode** ->
**Run workflow**. See artifacts from the `windows-vs2026` job.

No GitHub Actions job has been executed from this workspace: the connected
GitHub account has no `TC-Symbolic-Links` repository and the GitHub connector
does not expose repository creation. CI outcome remains **pending** until the
files are pushed to a repository and Actions run.
