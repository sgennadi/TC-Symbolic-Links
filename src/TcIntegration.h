#pragma once
#include <windows.h>
#include <string>
namespace tcs {
// Backs up files and appends a TC button + a new custom-column preset.
// Safe to call repeatedly. Paths may contain Hebrew, Cyrillic, and emoji.
bool ConfigureForTotalCommander(const std::wstring& tcIni, const std::wstring& dllPath,
                                std::wstring& status);
void AutoConfigureOnFirstLoad(const char* suggestedIniPath);
}
