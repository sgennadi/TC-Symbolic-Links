# Windows / Total Commander acceptance (0.2.1 Preview)

Use a clean Total Commander 11.58 x64 profile, plus a second existing profile with real buttons and custom columns. Save a separate external copy of the profile before the test.

1. Double-click the install ZIP. Confirm the native TC installer registers `SymlinkInspector.wdx64` under WDX, and does not deploy `TCSymlinkMaker.exe`.
2. Load a WDX field in TC (custom columns or the plugin manager). Record whether the setup prompt appears. Choose Yes.
3. Confirm `[Buttonbar]` now points at a cloned `.bar`, the original bar is untouched, `usercmd.ini` contains the `em_TCSymbolicLinks_Create` command with `rundll32.exe`, and both `[left]` and `[right]` use the new `SpecialView` index.
4. Restart TC. Confirm the **Create Links** button is actually visible and the four WDX fields display for symlinks and hardlinks in both panels.
5. Select a file named `שָלוֹם עברית test.txt`, choose another directory in the opposite panel, click the button, and create a **hardlink**. Confirm both names refer to the same NTFS file.
6. Test directory junctions, file/directory symlinks, relative targets, pre-existing destinations and cross-volume hardlink rejection.
7. Test the second profile with original toolbar/columns, unusual UTF-16 Hebrew/emoji paths, Windows scaling 200%, and Unicode profile folder paths.
8. Repeat setup: verify no extra button or duplicate column preset. Decline setup in a new profile and confirm no settings change.
9. Verify `.tcslinks.bak` files exist and verify manual uninstall instructions before rolling out broadly.

The standard TC installer **cannot** itself execute WDX setup code. If the initial prompt never occurs, load the WDX plugin explicitly and observe whether `ContentSetDefaultParams` fires. Do not mark first-load automation as verified without actual Total Commander evidence.

## Mandatory 0.2.2 real-user regression

**DO NOT install 0.2.1 on an existing customized Total Commander profile.**

1. Before configuring 0.2.2, save your actual `wincmd.ini`, active `.bar` and `usercmd.ini`; note the number and identity of all existing buttons.
2. Install `SymlinkInspector.wdx64` using TC's built-in plugin installer and accept the plugin's one-time configuration. Close and restart all TC windows.
3. Verify the original toolbar is still selected, every prior icon/subbar works, and exactly one additional **Create Links** button appears.
4. With source files selected, press the new button: it must launch a **visible Win32 GUI**, not silently exit. Create file symlink, directory symlink, junction and hardlink with Russian and Hebrew file names. Verify both paths and hardlink count.
5. Check both panel column presets display `Link type`, `Link target`, `Target exists`, `Hardlink count`, and previous custom views remain selectable.
6. Re-run integration explicitly and restart; verify no duplicate button and no unrelated usercmd.ini changes.
7. If a previous 0.2.1 install hid toolbar buttons, recover from `wincmd.ini.tcslinks.bak` first (see README and scripts/Restore-TC-Buttons-0.2.1.cmd).

## 0.2.3 acceptance: previously affected profile

1. Record the current `wincmd.ini`, previous buttons, and path of the active
   `.bar` (see TC Help → About, Configuration → Change button bar).
2. Exit **all** TC instances, then run `Restore-TC-Toolbar-Only.cmd`. Confirm
   that original button icons return and the other customized views remain.
3. Install the updated single `.wdx64` ZIP and accept one-time integration.
   Restart every TC instance. Old buttons must still be present in the same bar.
4. Select a sample file with Hebrew/niqqud in the source panel and a distinct
   directory in the other panel. Click **TC Symbolic Links - Create links**.
   A Win32 dialog must appear. Choose Hardlink and create. Confirm link exists.
5. Repeat step 4 for a directory junction and a symbolic link, selecting only
   compatible destination/volume pairs. Verify each WDX field in both panels.
6. Do not report a stable release unless toolbar click and column rendering
   both pass in real Total Commander.

## 0.2.4: valid DEFAULT.BAR without [Buttonbar] Buttonbar

1. In an ISOLATED Total Commander test profile, preserve a copy of wincmd.ini
   and DEFAULT.BAR; do not run on a production profile before backing up.
2. Remove only the test profile's [Buttonbar] Buttonbar entry. Make sure TC
   shows its default toolbar with existing custom buttons and subbars.
3. Install the 0.2.4 WDX and accept one-time configuration.
4. Verify the original DEFAULT.BAR is byte-for-byte unchanged, and a new
   TC-Symbolic-Links-Default.bar exists beside the INI with every original
   button plus exactly one Create Links button.
5. Restart TC; verify the old buttons still work, the new button opens the
   one-WDX GUI, and WDX columns appear in both panels.
6. Reinstall; verify no duplicated buttons or column groups.
7. For a profile with an explicitly configured .bar, verify its path does not
   change and no shadow bar is activated.
