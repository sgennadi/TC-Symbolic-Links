#pragma once
#include <windows.h>

namespace tcs::update {
// Launch the trusted PowerShell updater embedded in this WDX as raw data.
// The Windows PowerShell host is an OS component: no companion application.
// Manual mode opens the Update Manager UI; Automatic runs at most daily.
bool StartUpdateManager(HMODULE plugin, bool automatic, HWND owner = nullptr);
// Used only from WDX ContentSetDefaultParams outside DllMain/loader lock.
void StartAutomaticUpdateCheck();
}
