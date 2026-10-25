# TC Symbolic Links 0.2.4: two user-visible regressions

## RunDLL module path corruption (reproduced from a user's screenshot)

0.2.4 wrote a toolbar parameter starting with:

    "%COMMANDER_PATH%\plugins\wdx\TCSymbolicLinks\SymlinkInspector.wdx64",TCLinkCreate

Total Commander interprets `%C` as a special substitution for the selected
file. The resulting rundll32 error contained part of the selected file path,
followed by literal `OMMANDER_PATH...`, instead of the installed DLL path.
The isolated tests did not reproduce this because they substituted the entire
`%COMMANDER_PATH%` string themselves, not using TC's button parser.

**Resolution:** write the absolute module file path as installed. Test it
with a synthetic TC installation containing the DLL at
`TCInstall\plugins\wdx\TCSymbolicLinks\SymlinkInspector.wdx64` and run
the persisted .bar parameter through the real system rundll32 Unicode probe.
A literal `%` in the module path is rejected until tested escaping semantics
are implemented.

## Existing toolbar icons disappeared again

0.2.4 automatically cloned DEFAULT.BAR into a new per-user .bar and changed
`[Buttonbar] Buttonbar` to that copy when no key was present. Although this
preserved DEFAULT.BAR on disk, it **changed the selected toolbar** and could
hide buttons displayed from another bar in an interactive TC session.

**Resolution:** 0.2.5 never changes `[Buttonbar] Buttonbar`. It may only
append to the real, writable default bar or an explicitly configured personal
bar, and backs up the actual .bar before writing. Read-only or ambiguous bars
are refused, even if that means an automatic button cannot be installed.

## Recovery and acceptance

`Restore-TC-Toolbar-Only.ps1` recognizes both historical shadow bar names:
`TCSymbolicLinks.bar` and `TC-Symbolic-Links-Default.bar`. It requires the
original `wincmd.ini.tcslinks.bak`, previews the old and current selections,
and updates only the toolbar key after explicit confirmation.

Windows CI verifies code and synthetic .bar behavior, but a genuine TC GUI
with its actual custom buttons and WDX columns is still required before
stable release.
