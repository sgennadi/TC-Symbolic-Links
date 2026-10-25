# Required manual acceptance for 0.2.8 before stable release

On a real Total Commander 11.58 x64 machine with existing personal toolbar:

1. Back up the exact `wincmd.ini` path shown in Help > About, plus the active
   `.bar` and `usercmd.ini`. Do NOT test on your only unbacked-up profile.
2. If using the 0.2.1 or 0.2.4 shadow bar, close TC and run the surgical
   recovery helper first. Check all original icons are visible again.
3. Install the new WDX in Total Commander. Confirm no extra EXE is installed.
4. In a profile with an explicit writable `.bar`, approve first-load setup:
   the `Buttonbar=` key must be identical before/after, all old buttons/icons
   must still be visible, and exactly ONE Create Links button must be added.
5. In a profile without `Buttonbar=`, test a **read-only** DEFAULT.BAR first:
   the plugin must refuse and preserve every setting, never activate a clone.
   Test separately with a writable DEFAULT.BAR and verify in-place append.
6. Click Create Links while selecting a real source path in TC (e.g.
   `Inspect-ISOReadOnly.ps1` as seen in the report). No RunDLL module path may
   contain that selected file name. The link maker must open.
7. With disposable files and folders and both panels visible, test symlink,
   junction and hardlink creation, Hebrew/niqqud names, and paths with spaces.
8. Verify all four WDX custom fields display in left and right panels.
9. Restart TC twice, confirm stable toolbar/icons, no duplicate Create Links
   button, no unwanted .bar or usercmd.ini replacement, and no lost columns.
10. If any step fails, do not retry the automatic installer on the same
    unverified profile. Preserve the backup and report the exact error and
    relevant `Buttonbar=` value for diagnosis.

**Do not merge or tag stable until steps 4–9 are observed on a real TC GUI.**

---

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

## 0.2.6 actual user screenshot regressions (mandatory)

1. With TC's existing custom toolbar selected, note all original icons and
   verify `[Buttonbar] Buttonbar` remains identical before and after setup.
2. If icons were already hidden by 0.2.1/0.2.4, run the read-only
   `Inspect-TC-Toolbars.cmd` and the consent-gated surgical restore before
   doing any further plugin setup. Do not overwrite old `.bar` files.
3. Select a directory named `WinPE-CHKDSK-v1.0.17` in the source panel; TC
   should transmit `...\WinPE-CHKDSK-v1.0.17\` in the UTF-16LE `%WL` list.
4. Choose a different folder in the opposite panel; select **Directory Junction**
   and Create. It must create a working folder link and must NOT show
   "Invalid source name" or an irrelevant Error 1314 privilege warning.
5. Select **Directory symbolic link** and Create in a fresh destination,
   recording privilege-dependent results separately from folder-name validation.
6. Verify the description changes for each of the five link types, the
   Relative target example is readable and only enabled for symlink kinds,
   and long paths/HiDPI at 100%, 150%, 200% do not clip the buttons.
7. Test Hebrew, Cyrillic, spaces and ending separators in selected folders.
8. Verify file Hardlink and file Symlink still create expected link kinds,
   and no pre-existing destination is ever overwritten.


## Visual acceptance of the TC-style Creator (0.2.7)

On a Windows 11 workstation with Total Commander x64:
- Open the Maker from the existing TC button with files and folders selected.
- Confirm the **two horizontally aligned file panes**: left with Name / Type /
  Full path and small native icons; right showing planned destination links.
- Both source and destination path bars must be visible, including paths with
  Hebrew niqqud and mixed RTL/LTR text. Destination must remain editable.
- Change link type and destination: preview and guidance update without
  recreating the window or resetting a completed operation log.
- Press **F5** to create a Junction or hardlink. Verify the target and success
  row. Press **Esc** to close; while running, Esc requests Stop.
- At 100%, 150%, and 200% display scaling and after window resizing, no
  buttons should overlap the two panes; the destination input must remain
  accessible. Inspect Win32 fonts and control reading order on Hebrew Windows.
- Existing Total Commander toolbar icons/custom columns must be unchanged
  by this purely visual update.


## 0.2.8 — Link safety, colour and icon regression

1. In Total Commander select \`C:\\Work\\PS1\`, with the opposite panel open
   to \`C:\\Work\\.\`. **F5 must be disabled** and the dialog must show a warning
   in the chosen display language. No file or link may be written.
2. Set destination to \`C:\\Work\\PS1\` or a subfolder of that source directory.
   The creator must refuse a recursive symlink or junction.
3. Change destination to a safe existing directory; the blue **chain-link
   Create** button and F5 must immediately become available.
4. Create a Junction and a Hardlink. Ensure result lines are green on success,
   red on failure, and the blue progress indicator animates while working.
5. Check icons: gold folder beside source; blue link beside target; chain on
   blue Create button; X for close; square for Stop; red warning when blocked.
   Hover buttons to see visual highlight and translated tooltips.
6. The new plugin-installed Total Commander button uses its embedded chain
   icon, not the prior \`WCMICONS.DLL,44\` image. All other custom toolbar
   icons and commands remain unchanged.
