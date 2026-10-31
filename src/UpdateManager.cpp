#include "UpdateManager.h"
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <string>

#ifndef TC_PREVIEW_ITERATION
#define TC_PREVIEW_ITERATION 0
#endif

namespace {
constexpr int kResourceUpdater = 220;
constexpr wchar_t kRegistryPath[] = L"Software\\TCSymbolicLinks\\Updates";
std::atomic_flag g_autoStarted = ATOMIC_FLAG_INIT;

// Content plugins can be loaded from both TC bitnesses and the associated
// rundll32 hosts. Resolve the ACTUAL WDX module that contains this code.
HMODULE OwnModule() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OwnModule), &module)) return nullptr;
    return module;
}

std::wstring ModuleFile(HMODULE module) {
    std::wstring result(32768, L'\0');
    const DWORD n = GetModuleFileNameW(module, result.data(), static_cast<DWORD>(result.size()));
    if (!n || n >= result.size()) return {};
    result.resize(n);
    return result;
}

std::wstring Join(const std::wstring& left, const wchar_t* right) {
    return left + L"\\" + right;
}

bool Directory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS &&
        (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool AutoCheckDue() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) return true;
    DWORD disabled = 0, length = sizeof(disabled), kind = REG_DWORD;
    const bool never = RegQueryValueExW(key, L"Never", nullptr, &kind,
        reinterpret_cast<LPBYTE>(&disabled), &length) == ERROR_SUCCESS && disabled != 0;
    ULONGLONG lastCheck = 0;
    length = sizeof(lastCheck);
    kind = REG_QWORD;
    RegQueryValueExW(key, L"LastCheckUtc", nullptr, &kind,
        reinterpret_cast<LPBYTE>(&lastCheck), &length);
    RegCloseKey(key);
    if (never) return false;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER now{};
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    constexpr ULONGLONG oneDay = 864000000000ULL;
    return lastCheck == 0 || now.QuadPart < lastCheck ||
           now.QuadPart - lastCheck >= oneDay;
}

bool Launch(HMODULE module, bool automatic) {
    const std::wstring plugin = ModuleFile(module);
    if (plugin.empty()) return false;
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(kResourceUpdater),
                                  MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!resource) return false;
    const DWORD bytes = SizeofResource(module, resource);
    if (!bytes || bytes > 1024 * 1024) return false;
    HGLOBAL loaded = LoadResource(module, resource);
    const void* script = loaded ? LockResource(loaded) : nullptr;
    if (!script) return false;

    std::wstring local(32768, L'\0');
    const DWORD localLen = GetEnvironmentVariableW(L"LOCALAPPDATA",
        local.data(), static_cast<DWORD>(local.size()));
    if (!localLen || localLen >= local.size()) return false;
    local.resize(localLen);
    const auto root = Join(local, L"TCSymbolicLinks");
    const auto dir = Join(root, L"Updates");
    if (!Directory(root) || !Directory(dir)) return false;

    wchar_t temporary[MAX_PATH + 1]{};
    if (dir.size() + 40 >= MAX_PATH ||
        !GetTempFileNameW(dir.c_str(), L"tcu", 0, temporary)) return false;
    const std::wstring reserved(temporary);
    const std::wstring scriptFile = reserved + L".ps1";
    if (!MoveFileW(reserved.c_str(), scriptFile.c_str())) {
        DeleteFileW(reserved.c_str());
        return false;
    }
    HANDLE file = CreateFileW(scriptFile.c_str(), GENERIC_WRITE, 0, nullptr,
                              TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(scriptFile.c_str());
        return false;
    }
    constexpr unsigned char bom[3]{0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    bool okay = WriteFile(file, bom, sizeof(bom), &written, nullptr) &&
        written == sizeof(bom);
    written = 0;
    okay = okay && WriteFile(file, script, bytes, &written, nullptr) && written == bytes;
    CloseHandle(file);
    if (!okay) {
        DeleteFileW(scriptFile.c_str());
        return false;
    }

    wchar_t windowsDir[MAX_PATH + 1]{};
    if (!GetWindowsDirectoryW(windowsDir, MAX_PATH + 1)) return false;
    // Windows automatically redirects System32 for an x86 WDX process.
    const std::wstring psHost = std::wstring(windowsDir) +
        L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (GetFileAttributesW(psHost.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const std::wstring version = L"0.2.15-preview." +
        std::to_wstring(TC_PREVIEW_ITERATION);
    std::wstring command = L"\"" + psHost +
        L"\" -NoLogo -NoProfile -NonInteractive -STA -ExecutionPolicy Bypass -File \"" +
        scriptFile + L"\" -Mode " + (automatic ? L"Auto" : L"Manual") +
        L" -PluginPath \"" + plugin + L"\" -CurrentVersion \"" + version + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(psHost.c_str(), command.data(),
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(),
        &startup, &process);
    if (!launched) {
        DeleteFileW(scriptFile.c_str());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
} // anonymous namespace

bool tcs::update::StartUpdateManager(HMODULE plugin, bool automatic, HWND owner) {
    if (automatic && !AutoCheckDue()) return true;
    if (!plugin) plugin = OwnModule();
    if (plugin && Launch(plugin, automatic)) return true;
    if (!automatic)
        MessageBoxW(owner, L"Update Manager could not start. Check that Windows PowerShell is available.",
                    L"TC Symbolic Links", MB_OK | MB_ICONERROR);
    return false;
}

void tcs::update::StartAutomaticUpdateCheck() {
    if (g_autoStarted.test_and_set()) return;
    if (!AutoCheckDue()) return;
    StartUpdateManager(OwnModule(), true);
}
