#pragma once
#include <windows.h>
#include <string>
namespace tcs {
// Backs up files and appends a TC button + a new custom-column preset.
// Safe to call repeatedly. Paths may contain Hebrew, Cyrillic, and emoji.
// Count distinct numeric WDX registrations, excluding [ContentPlugins64] flags.
int DuplicateRegistrationCount(const std::wstring& tcIni);
bool ConfigureForTotalCommander(const std::wstring& tcIni, const std::wstring& dllPath,
                                std::wstring& status);
void AutoConfigureOnFirstLoad(const char* suggestedIniPath);
}
