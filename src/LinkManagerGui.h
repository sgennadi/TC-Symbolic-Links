#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace tcs::manager {
void ShowLinkManager(HWND owner, const std::wstring& initialFolder,
                     const std::vector<std::wstring>& selected);
}
