# Regression: 0.2.1 hid existing Total Commander buttons

## User-visible failure

In a real Total Commander 11.58 x64 Windows session, the custom toolbar icons
were replaced by a small default-looking set. The newly added Create Links
button did nothing. This escaped 0.2.1 CI because the previous test asserted
that `wincmd.ini` SHOULD switch to the plugin-created TCSymbolicLinks.bar.

## Root cause and mitigation

0.2.1 read `[Buttonbar] Buttonbar` and, when it could not determine the active
bar, fell back to DEFAULT.BAR. It then copied that bar to TCSymbolicLinks.bar,
appended an `em_TCSymbolicLinks_Create` button, and replaced the original
`[Buttonbar] Buttonbar` setting with the clone. That loses the toolbar actually
used by customized installations. It also converted the whole `usercmd.ini`
file to UTF-16LE (not acceptable for a non-destructive installer), and depended
on a user-defined command that could be absent or unreadable in TC.

0.2.2 does not ever change the active bar path, does not write `usercmd.ini`,
and uses a direct `rundll32.exe` command in the active `.bar`. It refuses
ambiguous/redirected buttonbar profiles instead of guessing.

## Recovery

1. In TC, use Help > About Total Commander to identify the actual wincmd.ini.
2. Close all TOTALCMD.EXE and TOTALCMD64.EXE windows.
3. Run `scripts/Restore-TC-Buttons-0.2.1.cmd` and select the INI.
4. The script backs up the current INI/usercmd.ini, then restores both from
   the `.tcslinks.bak` backups created by 0.2.1. It does not delete bar files.
5. Reopen TC to verify original buttons. A total INI restore also restores
   preferences/panel views from the backup timestamp.

## Mandatory acceptance before stable

Use actual user-configured TC profiles, including a custom bar with nested
subbars and unrelated `em_` commands. Record the active bar path and SHA-256
before and after. Verify every existing button survives, new direct button
launches the DLL UI when clicked, WDX fields render in both panes, old views
remain available, and no change is made to usercmd.ini. Hosted Windows
rundll32/ABI tests cannot replace these real application tests.
