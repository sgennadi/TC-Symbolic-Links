#pragma once
#include <windows.h>
#include <string>

namespace tcs::update {
// Native WinHTTP GitHub release check: no temporary PS1 and no PowerShell host.
// Manual mode displays progress and result; automatic runs at most daily.
// Installation and conservative registration repair are separate operations.
bool StartUpdateManager(HMODULE plugin, bool automatic, HWND owner = nullptr);
// Used only from WDX ContentSetDefaultParams outside DllMain/loader lock.
void StartAutomaticUpdateCheck();
// Schedules a conservative, opt-in duplicate WDX-registration repair once TC exits.
bool StartRegistrationRepair(HMODULE plugin, const std::wstring& iniPath, HWND owner = nullptr);
}
