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
