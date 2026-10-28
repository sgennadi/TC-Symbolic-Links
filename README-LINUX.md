# Native Linux WDX (Double Commander)

`SymlinkInspector.wdx` is a Linux ELF library compiled from the same source as the Windows WDX. It is inspection-only: fields `Link type`, `Link target`, `Target exists`, `Hardlink count`. There is no Total Commander Windows button-bar or Win32 symlink maker on Linux.

    cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
    cmake --build build-linux --parallel
    ctest --test-dir build-linux --output-on-failure

Select the plugin in Double Commander → Configuration → Options → Plugins → Content plugins (WDX). Select the content fields for your custom column layout. Tests with Hebrew/niqqud, emoji, relative links, broken links and hardlinks are performed through the exported WDX API.

Note: the native Linux plugin is not a replacement for the Windows `.wdx64` DLL or Total Commander GUI tests.
