# Total Commander x64 manual test checklist (0.1.5 preview)

Run after the GitHub Actions **Windows** build succeeds. Use a disposable folder on NTFS. Do not run these tests inside a production tree.

1. Open the packaged installer zip inside TC x64; confirm WDX install prompt; verify `SymlinkInspector` in Content Plugins and that `TCSymlinkMaker.exe` was extracted next to it.
2. Add the button with `--list "%WL" --dest "%T"`. Select one Hebrew file (including niqqud), one Hebrew directory, and a filename containing English/number/space, then click the button. Confirm every source appears correctly in the list.
3. With the opposite pane showing an empty folder, create **Automatic** links. Verify each file and folder link is accessible and resolves to its original target; check the WDX `Link target` and `Target exists` columns.
4. Separately create a **Directory junction**. Verify folder traversal, field type, and target; verify failure when the source is a file or an UNC network path.
5. Separately create a **Hardlink** in the same NTFS volume, and confirm editing one entry changes the other; verify WDX count >= 2. Attempt a hardlink on another volume and check failure is reported.
6. Attempt to create a link with a destination name that already exists (including a broken symlink). Verify it does not overwrite, delete, or rename the existing item.
7. Rename/delete a test symlink target (not the link) and check `Target exists` changes to `No` without crashing TC; restore the target after inspection.
8. Test interface languages by changing Windows UI language to English, Russian and Hebrew; restart app; check visible text, no clipped controls, and correct logical UTF-16 filenames (not reversed by bidi display).
9. Repeat at 100%, 150%, 200% DPI; resize the window and move between monitors with different scaling. Check combo box, edit, checkboxes, buttons and results list.
10. Run Total Commander as non-admin with Developer Mode disabled to ensure insufficient symlink privilege gives error 1314 and remains recoverable; run elevated or enable appropriate privilege as needed.
11. Select 1000+ disposable source files, start creation and verify the interface stays responsive, window title shows progress and Stop terminates remaining work. Already-created links must remain untouched, and pressing Create again must start a fresh operation rather than repeat the previous work.
12. Check both File Symbolic and Directory Symbolic modes, the relative target option, and that the destination is snapshotted at the beginning (editing the input during an active batch must not redirect subsequent links).

Record compiler version, Total Commander version, Windows version, NTFS volume and results before calling it stable.

## Optional Linux Double Commander acceptance (not yet executed)

1. Install Linux Double Commander x64; in **Configuration / Options / Plugins /
   Content plugins (WDX)** add the Linux ELF `SymlinkInspector.wdx`.
2. Enable fields in custom columns, and check a Hebrew-named directory symlink,
   Hebrew filename including niqqud and emoji, a hardlink, a broken symlink.
3. Expected values include `Directory symlink`, `File symlink`, `Hardlinked
   file`, exact Unicode `Link target`, `Target exists` Yes/No and hardlink count.
4. Check real TC x64 on Windows separately with the PE `.wdx64` and Win32 maker.

The `linux_wdx_hebrew_plugin` automated test already loads the ELF `.wdx` and
calls its WDX exports, but does not automate the graphical Double Commander UI.

## GitHub Actions real-app test

Review `TC-RealApp-Smoke-Windows/tc-smoke.json`: confirm
`tc_process_started`, `tc_wdx_loaded_in_process` and
`maker_hebrew_hardlink_created` separately before marking the GUI integration
verified. CI may have no interactive desktop even when the process runs.
See [`REAL-TC-CI.md`](REAL-TC-CI.md).
