#include "UpdateManager.h"
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <string>
#include <winhttp.h>
#include <shellapi.h>
#include <regex>
#include <thread>
#include <memory>
#include <vector>
#include <algorithm>
#pragma comment(lib, "winhttp.lib")

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

bool Launch(HMODULE module, bool automatic, const std::wstring& repairIni = {}) {
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
    const auto* payload = static_cast<const unsigned char*>(script);
    const bool embeddedBom = bytes >= 3 &&
        payload[0] == bom[0] && payload[1] == bom[1] &&
        payload[2] == bom[2];
    DWORD written = 0;
    bool okay = true;
    // Windows PowerShell 5.1 assumes an ANSI code page without this BOM.
    // The repository script already has a UTF-8 BOM; never write two.
    if (!embeddedBom)
        okay = WriteFile(file, bom, sizeof(bom), &written, nullptr) &&
               written == sizeof(bom);
    written = 0;
    okay = okay && WriteFile(file, script, bytes, &written, nullptr) &&
           written == bytes;
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
    const std::wstring version = L"0.2.16-preview." +
        std::to_wstring(TC_PREVIEW_ITERATION);
    std::wstring command = L"\"" + psHost +
        L"\" -NoLogo -NoProfile -NonInteractive -STA -ExecutionPolicy Bypass -File \"" +
        scriptFile + L"\" -Mode " + (!repairIni.empty() ? L"Repair" : (automatic ? L"Auto" : L"Manual")) +
        L" -PluginPath \"" + plugin + L"\" -CurrentVersion \"" + version + L"\"" +
        (repairIni.empty() ? L"" : (L" -IniPath \"" + repairIni + L"\""));
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


namespace {
constexpr UINT WM_TCS_UPDATE_DONE = WM_APP + 220;
struct UpdateCheck {
    HWND window = nullptr;
    HWND status = nullptr;
    std::wstring result;
    std::wstring url;
    bool newer = false;
    bool success = false;
    bool automatic = false;
};

void RecordCheck(bool success) {
    if (!success) return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t{};
    t.LowPart = ft.dwLowDateTime; t.HighPart = ft.dwHighDateTime;
    const ULONGLONG value = t.QuadPart;
    RegSetValueExW(key, L"LastCheckUtc", 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}
struct NetHandle {
    HINTERNET h = nullptr;
    ~NetHandle() { if (h) WinHttpCloseHandle(h); }
};
bool GetReleases(std::string& body) {
    NetHandle session{WinHttpOpen(L"TC-Symbolic-Links-NativeUpdater/0.2.16",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return false;
    WinHttpSetTimeouts(session.h, 6000, 6000, 9000, 9000);
    NetHandle connection{WinHttpConnect(session.h, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.h) return false;
    NetHandle request{WinHttpOpenRequest(connection.h, L"GET",
        L"/repos/sgennadi/TC-Symbolic-Links/releases?per_page=20",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE)};
    if (!request.h) return false;
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) return false;
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)
        || status != 200) return false;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) return false;
        if (!available) break;
        if (body.size() + available > 1024 * 1024) return false;
        const size_t start = body.size();
        body.resize(start + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, body.data() + start, available, &read)) return false;
        body.resize(start + read);
        if (!read) break;
    }
    return !body.empty();
}
struct Version {
    int major=0,minor=0,patch=0,iteration=0;
    bool preview=false,valid=false;
};
Version ParseVersion(const std::string& tag) {
    static const std::regex re(R"(^v?([0-9]+)\.([0-9]+)\.([0-9]+)(-preview\.([0-9]+))?$)");
    std::smatch m;
    Version v;
    if (!std::regex_match(tag,m,re)) return v;
    try {
        v.major=std::stoi(m[1]); v.minor=std::stoi(m[2]);
        v.patch=std::stoi(m[3]); v.preview=m[4].matched;
        if(m[5].matched) v.iteration=std::stoi(m[5]);
        v.valid=true;
    } catch (...) { v.valid=false; }
    return v;
}
bool Newer(const Version& v,const Version& old) {
    if(v.major!=old.major) return v.major>old.major;
    if(v.minor!=old.minor) return v.minor>old.minor;
    if(v.patch!=old.patch) return v.patch>old.patch;
    if(v.preview!=old.preview) return !v.preview;
    return v.iteration>old.iteration;
}
bool UsePreviewChannel() {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kRegistryPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return true;
    wchar_t buffer[32]{}; DWORD type=REG_SZ,bytes=sizeof(buffer);
    const bool ok=RegQueryValueExW(key,L"Channel",nullptr,&type,
        reinterpret_cast<BYTE*>(buffer),&bytes)==ERROR_SUCCESS && type==REG_SZ;
    RegCloseKey(key);
    return !ok || _wcsicmp(buffer,L"Stable")!=0;
}
void InspectReleases(UpdateCheck& check) {
    const Version installed=ParseVersion("0.2.16-preview."+std::to_string(TC_PREVIEW_ITERATION));
    std::string data;
    if(!GetReleases(data)) {
        check.result=L"Unable to check GitHub releases (network, proxy, API or TLS error).";
        return;
    }
    // Match only a complete release object with a tag, never arbitrary JSON text.
    // Scan candidate tag names; older or unsupported tags cannot cause a downgrade.
    const std::regex tags(R"TAG("tag_name"\s*:\s*"(v?[0-9]+\.[0-9]+\.[0-9]+(?:-preview\.[0-9]+)?)")TAG");
    Version best=installed;
    std::string bestTag;
    const bool preview=UsePreviewChannel();
    for(std::sregex_iterator it(data.begin(),data.end(),tags),end;it!=end;++it) {
        const std::string tag=(*it)[1].str();
        const Version v=ParseVersion(tag);
        if(v.valid && (preview||!v.preview) && Newer(v,best)) {
            best=v;bestTag=tag;
        }
    }
    check.success=true;
    RecordCheck(true);
    if(bestTag.empty()) {
        check.result=L"No newer release found for the selected channel.";
    } else {
        check.newer=true;
        check.result=L"New version available: "+std::wstring(bestTag.begin(),bestTag.end());
        check.url=L"https://github.com/sgennadi/TC-Symbolic-Links/releases/tag/"+
            std::wstring(bestTag.begin(),bestTag.end());
    }
}
constexpr int ID_UPDATE_CLOSE = 401;
constexpr int ID_UPDATE_OPEN = 402;
constexpr int ID_UPDATE_RESULT = 403;
constexpr int ID_UPDATE_TITLE = 404;
LRESULT CALLBACK UpdateWndProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* check=reinterpret_cast<UpdateCheck*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_TCS_UPDATE_DONE) {
        if(check) {
            SetWindowTextW(check->status,check->result.c_str());
            HWND open=GetDlgItem(hwnd,ID_UPDATE_OPEN);
            if(open) ShowWindow(open,check->newer?SW_SHOW:SW_HIDE);
            HWND close=GetDlgItem(hwnd,ID_UPDATE_CLOSE);
            if(close) SetWindowTextW(close,L"Close");
            SetWindowTextW(hwnd,L"TC Symbolic Links - Update Check");
        }
        return 0; // Keep dialog open, no second MessageBox.
    }
    if(message==WM_COMMAND) {
        if(LOWORD(wParam)==ID_UPDATE_OPEN && check && check->newer && !check->url.empty()) {
            ShellExecuteW(hwnd,L"open",check->url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            return 0;
        }
        if(LOWORD(wParam)==ID_UPDATE_CLOSE) {
            DestroyWindow(hwnd);return 0;
        }
    }
    if(message==WM_CTLCOLORSTATIC) {
        HDC dc=reinterpret_cast<HDC>(wParam);
        HWND control=reinterpret_cast<HWND>(lParam);
        if(GetDlgCtrlID(control)==ID_UPDATE_TITLE) {
            SetTextColor(dc,RGB(255,255,255));
            SetBkColor(dc,RGB(14,76,154));
            static HBRUSH blue=CreateSolidBrush(RGB(14,76,154));
            return reinterpret_cast<LRESULT>(blue);
        }
        SetTextColor(dc,RGB(25,47,78));
        SetBkColor(dc,RGB(245,248,253));
        static HBRUSH pale=CreateSolidBrush(RGB(245,248,253));
        return reinterpret_cast<LRESULT>(pale);
    }
    if(message==WM_CLOSE) { DestroyWindow(hwnd);return 0; }
    // Never PostQuitMessage here: this is a secondary window hosted by the
    // link maker, and WM_QUIT would close the entire parent UI.
    if(message==WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
bool NativeManualCheck(HWND owner) {
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    const wchar_t* klass=L"TCSymbolicLinksNativeUpdateWindow";
    WNDCLASSW cls{};cls.lpfnWndProc=UpdateWndProc;cls.hInstance=instance;
    // Use the same embedded chain-link icon as the main Link Maker window.
    // GetModuleHandle(nullptr) points to rundll32.exe, not to our WDX.
    const HMODULE iconModule=OwnModule();
    cls.hIcon=iconModule ? LoadIconW(iconModule,MAKEINTRESOURCEW(101)) : nullptr;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=CreateSolidBrush(RGB(245,248,253));
    cls.lpszClassName=klass;
    RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,klass,
        L"TC Symbolic Links - Checking GitHub...",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,525,205,owner,nullptr,instance,nullptr);
    if(!window) return false;
    if (iconModule) {
        HICON smallIcon=static_cast<HICON>(LoadImageW(iconModule,
            MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (smallIcon) SendMessageW(window, WM_SETICON, ICON_SMALL,
            reinterpret_cast<LPARAM>(smallIcon));
    }
    // Center over the maker, not at Windows' default cascaded position.
    // Clamp to the work area of the monitor containing the owner window.
    RECT wr{}, anchor{};
    GetWindowRect(window, &wr);
    if (!owner || !IsWindow(owner) || !GetWindowRect(owner, &anchor)) {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) anchor = info.rcWork;
    }
    const HMONITOR monitor = MonitorFromWindow(
        owner && IsWindow(owner) ? owner : window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info)) {
        const int width = wr.right - wr.left;
        const int height = wr.bottom - wr.top;
        const int x = (std::max)(info.rcWork.left, (std::min)(
            anchor.left + ((anchor.right - anchor.left) - width) / 2,
            info.rcWork.right - width));
        const int y = (std::max)(info.rcWork.top, (std::min)(
            anchor.top + ((anchor.bottom - anchor.top) - height) / 2,
            info.rcWork.bottom - height));
        SetWindowPos(window, nullptr, x, y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    auto check=std::make_shared<UpdateCheck>();
    check->window=window;
    CreateWindowW(L"STATIC",L"  TC Symbolic Links  |  Update Check",
        WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE,0,0,525,54,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_TITLE)),instance,nullptr);
    check->status=CreateWindowW(L"STATIC",L"Checking GitHub releases...",
        WS_CHILD|WS_VISIBLE|SS_LEFT,20,76,480,42,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_RESULT)),instance,nullptr);
    CreateWindowW(L"BUTTON",L"Open release",WS_CHILD|BS_PUSHBUTTON,
        220,124,130,29,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_OPEN)),instance,nullptr);
    CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        365,124,130,29,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_CLOSE)),instance,nullptr);
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(check.get()));
    ShowWindow(window,SW_SHOW);
    std::thread([check]{
        try {InspectReleases(*check);}
        catch (...) {check->result=L"Update check failed."; }
        PostMessageW(check->window,WM_TCS_UPDATE_DONE,0,0);
    }).detach();
    MSG msg{};
    // Private local loop, no PostQuitMessage. If the owner has been destroyed,
    // close only this dialog. Other windows remain functional.
    while(IsWindow(window)) {
        const BOOL result=GetMessageW(&msg,nullptr,0,0);
        if(result<=0) {
            if(result==0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if(!IsDialogMessageW(window,&msg)) {
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }
    return true;
}
void NativeAutomaticCheck() {
    std::thread([] {
        UpdateCheck check{};
        try {InspectReleases(check);} catch (...) { return; }
        if(check.newer) MessageBoxW(nullptr,
            (check.result+L"\nOpen Update Check from the plugin to view the release.").c_str(),
            L"TC Symbolic Links",MB_OK|MB_ICONINFORMATION);
    }).detach();
}
} // namespace

bool tcs::update::StartUpdateManager(HMODULE, bool automatic, HWND owner) {
    if (automatic) {
        if(!AutoCheckDue()) return true;
        NativeAutomaticCheck();
        return true;
    }
    return NativeManualCheck(owner);
}

void tcs::update::StartAutomaticUpdateCheck() {
    if (g_autoStarted.test_and_set()) return;
    if (!AutoCheckDue()) return;
    NativeAutomaticCheck();
}

bool tcs::update::StartRegistrationRepair(HMODULE plugin,
                                          const std::wstring& iniPath,
                                          HWND owner) {
    if (!plugin) plugin = OwnModule();
    if (plugin && !iniPath.empty() && Launch(plugin, false, iniPath)) return true;
    MessageBoxW(owner,
        L"Cannot start the deferred registration repair. No TC settings were changed.",
        L"TC Symbolic Links", MB_OK | MB_ICONERROR);
    return false;
}
