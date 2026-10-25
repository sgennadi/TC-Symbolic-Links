# TC Symbolic Links 0.1.5 - Linux Double Commander WDX

Native Linux x86-64 WDX content plugin: `SymlinkInspector.wdx` (ELF 64-bit).

**Install:** Open Double Commander -> Configuration -> Options -> Plugins -> Content plugins (WDX) -> Add. Select `SymlinkInspector.wdx`. Then create a custom-column layout with its fields: `Link type`, `Link target`, `Target exists`, `Hardlink count`.

On Linux it recognizes POSIX symlinks, broken/relative symlinks, and regular files with multiple hardlinks. It preserves Hebrew, niqqud, mixed Hebrew/English, and supplementary Unicode symbols in UTF-16 WDX output. Linux does not support Windows directory junctions; the shared portable NTFS reparse parser is tested independently with synthetic Windows buffers.

**Verification:** The test loader calls the actual ELF WDX exports with UTF-16 filenames and creates real POSIX links. GCC 14 and Clang 17 Release as well as GCC ASan/UBSan passed all three CTest suites. GUI integration in Double Commander remains unverified in the current headless container.

**Important:** This Linux ELF WDX is *not* the Windows `.wdx64` DLL, and it does not create links; link creation with `TCSymlinkMaker.exe` requires Windows x64. Do not load it in Windows Total Commander.
