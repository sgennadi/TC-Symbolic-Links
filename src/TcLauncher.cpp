// Exported Unicode entry points invoked by the OS-provided rundll32.exe.
// Invoke them as ,TCLinkCreate / ,TCLinkSetup / ,TCLinkProbe.
// rundll32 appends W to the requested base name and supplies LPWSTR only when
// it finds the suffixed export. Asking for ...W itself can cause ANSI fallback.
// The WDX content API is implemented by SymlinkInspector.cpp in this SAME DLL.
#include "TcIntegration.h"
#include "TcMakerGui.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>

namespace {
std::wstring ModulePath() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ModulePath), &self)) return {};
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return {};
    path.resize(n);
    return path;
}
std::wstring Trim(std::wstring s) {
    auto start = s.find_first_not_of(L" \t\r\n\"");
    if (start == std::wstring::npos) return {};
    auto end = s.find_last_not_of(L" \t\r\n\"");
    return s.substr(start, end - start + 1);
}
}

// NEVER execute UI or config operations from DllMain (loader lock).
extern "C" __declspec(dllexport) void CALLBACK TCLinkCreateW(
    HWND, HINSTANCE module, LPWSTR args, int show) {
    // Guard against corrupt/untrusted command lines. UI parses with CommandLineToArgvW.
    try { tcs::RunMakerFromArguments(module, args ? args : L"", show); }
    catch (...) { MessageBoxW(nullptr, L"Failed to create links.", L"TC Symbolic Links", MB_ICONERROR); }
}

// Available for explicit reconfiguration and automated isolated regression tests.
// Without arguments, resolves current Total Commander INI via COMMANDER_INI.
// With arguments it accepts exactly one quoted absolute INI path.
extern "C" __declspec(dllexport) void CALLBACK TCLinkSetupW(
    HWND parent, HINSTANCE, LPWSTR args, int) {
    try {
        std::wstring ini = Trim(args ? args : L"");
        if (ini.empty()) {
            wchar_t env[32768]{};
            const auto length = GetEnvironmentVariableW(L"COMMANDER_INI", env, 32768);
            if (!length || length >= 32768) {
                MessageBoxW(parent, L"Open this command from Total Commander or specify an INI path.",
                            L"TC Symbolic Links", MB_OK | MB_ICONWARNING);
                return;
            }
            ini = env;
        }
        std::wstring status;
        const bool ok = tcs::ConfigureForTotalCommander(ini, ModulePath(), status);
        MessageBoxW(parent, status.c_str(), L"TC Symbolic Links",
                    MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
    } catch (...) {
        MessageBoxW(parent, L"Configuration failed.", L"TC Symbolic Links", MB_OK | MB_ICONERROR);
    }
}

// Non-destructive CI check that the *real* system rundll32.exe can load the
// unusual .wdx64 extension and deliver a Unicode command line to this DLL.
extern "C" __declspec(dllexport) void CALLBACK TCLinkProbeW(
    HWND, HINSTANCE, LPWSTR args, int) {
    if (!args || !*args) return;
    std::wstring marker = Trim(args);
    if (marker.empty() || marker.size() > 32000) return;
    HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    constexpr char result[] = "Unicode rundll32 WDX OK";
    DWORD wrote = 0;
    WriteFile(file, result, sizeof(result) - 1, &wrote, nullptr);
    CloseHandle(file);
}
