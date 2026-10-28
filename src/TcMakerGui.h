#pragma once
#include <windows.h>
#include <string_view>
namespace tcs {
// Run the link creator as a dialog in the rundll32 host. No plugin-specific EXE.
int RunMakerFromArguments(HINSTANCE module, std::wstring_view commandLine, int show);
}
