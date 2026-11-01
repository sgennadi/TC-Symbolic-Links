#pragma once
#include <windows.h>
#include <string>
#include <string_view>

namespace tcs::update {
struct NativeStage {
    std::wstring folder;
    std::wstring tag;
    std::wstring description;
};
// Download only from the canonical GitHub Release; require its HTTPS API
// SHA-256 asset digest and validate all three ZIP entries before staging.
// WM_APP+250 reports approximate percentage from 0..100.
bool StageNativeUpdate(const std::wstring& tag, HWND progressWindow,
                       NativeStage& result);
// Launch the *installed* WDX via Windows rundll32. Elevation, when required,
// executes the protected installed DLL, never a DLL from user-writable temp.
bool BeginNativeInstall(const NativeStage& stage,
                        const std::wstring& installedWdx,
                        DWORD originalHostPid, std::wstring& error);
// Called solely by the dedicated rundll32 WDX export for a two-phase,
// close-TC/transactional-update installation. Never run inside DllMain.
void NativeInstallerWorker(std::wstring_view args);
}
