#include "UpdateManager.h"
#include "NativeUpdateInstall.h"
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <string>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <regex>
#include <thread>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#pragma comment(lib, "winhttp.lib")

#ifndef TC_PREVIEW_ITERATION
#define TC_PREVIEW_ITERATION 0
#endif
#ifndef TC_STABLE_RELEASE
#define TC_STABLE_RELEASE 0
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
#if TC_STABLE_RELEASE
    const std::wstring version = L"0.2.18";
#else
    const std::wstring version = L"0.2.18-preview." +
        std::to_wstring(TC_PREVIEW_ITERATION);
#endif
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
constexpr UINT WM_TCS_UPDATE_STAGE_DONE = WM_APP + 221;
constexpr UINT WM_TCS_UPDATE_PROGRESS = WM_APP + 250;
constexpr int ID_UPDATE_INSTALL = 405;
constexpr int ID_UPDATE_PROGRESS = 406;
constexpr int ID_UPDATE_BRAND = 407;
struct UpdateCheck {
    HWND window = nullptr;
    HWND status = nullptr;
    HWND progress = nullptr;
    std::wstring result;
    std::wstring url;
    std::wstring tag;
    tcs::update::NativeStage stage;
    bool newer = false;
    bool canReinstall = false;
    bool success = false;
    bool busy = false;
    bool installationStarted = false;
    bool stageOkay = false;
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
    NetHandle session{WinHttpOpen(L"TC-Symbolic-Links-NativeUpdater/0.2.18",
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
#if TC_STABLE_RELEASE
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kRegistryPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return false;
#else
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kRegistryPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return true;
#endif
    wchar_t buffer[32]{}; DWORD type=REG_SZ,bytes=sizeof(buffer);
    const bool ok=RegQueryValueExW(key,L"Channel",nullptr,&type,
        reinterpret_cast<BYTE*>(buffer),&bytes)==ERROR_SUCCESS && type==REG_SZ;
    RegCloseKey(key);
#if TC_STABLE_RELEASE
    return ok && _wcsicmp(buffer,L"Preview")==0;
#else
    return !ok || _wcsicmp(buffer,L"Stable")!=0;
#endif
}
void InspectReleases(UpdateCheck& check) {
#if TC_STABLE_RELEASE
    const Version installed=ParseVersion("0.2.18");
#else
    const Version installed=ParseVersion("0.2.18-preview."+std::to_string(TC_PREVIEW_ITERATION));
#endif
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
    std::string currentTag;
    const bool preview=UsePreviewChannel();
    for(std::sregex_iterator it(data.begin(),data.end(),tags),end;it!=end;++it) {
        const std::string tag=(*it)[1].str();
        const Version v=ParseVersion(tag);
        if(v.valid && !Newer(v,installed) && !Newer(installed,v))
            currentTag=tag;
        if(v.valid && (preview||!v.preview) && Newer(v,best)) {
            best=v;bestTag=tag;
        }
    }
    check.success=true;
    RecordCheck(true);
    if(bestTag.empty()) {
        check.result=L"No newer release found for the selected channel.";
        if(!currentTag.empty()) {
            check.canReinstall=true;
            check.tag=std::wstring(currentTag.begin(),currentTag.end());
            check.url=L"https://github.com/sgennadi/TC-Symbolic-Links/releases/tag/"+check.tag;
            check.result+=L" The verified current release can be reinstalled.";
        }
    } else {
        check.newer=true;
        check.tag=std::wstring(bestTag.begin(),bestTag.end());
        check.result=L"New version available: "+check.tag;
        check.url=L"https://github.com/sgennadi/TC-Symbolic-Links/releases/tag/"+
            std::wstring(bestTag.begin(),bestTag.end());
    }
}
constexpr int ID_UPDATE_CLOSE = 401;
constexpr int ID_UPDATE_OPEN = 402;
constexpr int ID_UPDATE_RESULT = 403;
constexpr int ID_UPDATE_TITLE = 404;

void DrawUpdateButton(const DRAWITEMSTRUCT& item) {
    RECT rect=item.rcItem;
    const UINT dpi=GetDpiForWindow(item.hwndItem);
    const auto dp=[dpi](int v){return MulDiv(v,static_cast<int>(dpi),96);};
    const bool disabled=(item.itemState & ODS_DISABLED)!=0;
    const bool selected=(item.itemState & ODS_SELECTED)!=0;
    const bool primary=item.CtlID==ID_UPDATE_INSTALL;
    const COLORREF fill=disabled?RGB(227,233,242):
        primary?(selected?RGB(10,137,81):RGB(16,170,93)):
        (selected?RGB(222,234,250):RGB(247,250,255));
    const COLORREF ink=disabled?RGB(126,140,160):
        primary?RGB(255,255,255):RGB(20,57,100);
    const COLORREF border=primary?fill:RGB(198,215,239);
    HBRUSH b=CreateSolidBrush(fill);
    HPEN pen=CreatePen(PS_SOLID,1,border);
    HGDIOBJ oldPen=SelectObject(item.hDC,pen);
    HGDIOBJ oldBrush=SelectObject(item.hDC,b);
    RoundRect(item.hDC,rect.left+1,rect.top+1,rect.right-1,rect.bottom-1,dp(13),dp(13));
    SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);
    DeleteObject(b);DeleteObject(pen);
    SetTextColor(item.hDC,ink);SetBkMode(item.hDC,TRANSPARENT);
    wchar_t text[160]{};
    GetWindowTextW(item.hwndItem,text,160);
    RECT label=rect;
    label.left+=dp(34);
    label.right-=dp(8);
    DrawTextW(item.hDC,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    // Draw vector icons using Win32 GDI: no font glyph/emoji fallback.
    const int cx=rect.left+dp(20),cy=(rect.top+rect.bottom)/2;
    HPEN lines=CreatePen(PS_SOLID,(std::max)(1,dp(2)),ink);
    HGDIOBJ prior=SelectObject(item.hDC,lines);
    if(item.CtlID==ID_UPDATE_INSTALL) {
        MoveToEx(item.hDC,cx,cy-dp(8),nullptr);LineTo(item.hDC,cx,cy+dp(4));
        MoveToEx(item.hDC,cx-dp(5),cy,nullptr);LineTo(item.hDC,cx,cy+dp(5));
        LineTo(item.hDC,cx+dp(5),cy);
        MoveToEx(item.hDC,cx-dp(7),cy+dp(9),nullptr);LineTo(item.hDC,cx+dp(7),cy+dp(9));
    } else if(item.CtlID==ID_UPDATE_OPEN) {
        MoveToEx(item.hDC,cx-dp(7),cy+dp(6),nullptr);LineTo(item.hDC,cx+dp(7),cy-dp(8));
        MoveToEx(item.hDC,cx,cy-dp(8),nullptr);LineTo(item.hDC,cx+dp(7),cy-dp(8));
        LineTo(item.hDC,cx+dp(7),cy-dp(1));
    } else {
        MoveToEx(item.hDC,cx-dp(5),cy-dp(5),nullptr);LineTo(item.hDC,cx+dp(5),cy+dp(5));
        MoveToEx(item.hDC,cx+dp(5),cy-dp(5),nullptr);LineTo(item.hDC,cx-dp(5),cy+dp(5));
    }
    SelectObject(item.hDC,prior);DeleteObject(lines);
    if(item.itemState & ODS_FOCUS) {
        RECT ring=rect;InflateRect(&ring,-4,-4);
        DrawFocusRect(item.hDC,&ring);
    }
}
std::shared_ptr<UpdateCheck> CheckFor(HWND hwnd) {
    auto* value=reinterpret_cast<std::shared_ptr<UpdateCheck>*>(
        GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    return value?*value:nullptr;
}
void UpdateStatus(HWND hwnd,const std::wstring& text) {
    if(HWND target=GetDlgItem(hwnd,ID_UPDATE_RESULT))
        SetWindowTextW(target,text.c_str());
}
LRESULT CALLBACK UpdateWndProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    const auto check=CheckFor(hwnd);
    if(message==WM_TCS_UPDATE_DONE) {
        if(check) {
            UpdateStatus(hwnd,check->result);
            if(HWND open=GetDlgItem(hwnd,ID_UPDATE_OPEN))
                ShowWindow(open,check->newer||check->canReinstall?SW_SHOW:SW_HIDE);
            if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL)) {
                ShowWindow(install,check->newer||check->canReinstall?SW_SHOW:SW_HIDE);
                SetWindowTextW(install,check->canReinstall?L"Reinstall":
                                                L"Download & Install");
                EnableWindow(install,check->newer||check->canReinstall);
            }
            SetWindowTextW(hwnd,L"TC Symbolic Links v0.2.18 - Update Manager");
        }
        return 0;
    }
    if(message==WM_TCS_UPDATE_PROGRESS) {
        if(check && check->busy) {
            if(check->progress)SendMessageW(check->progress,PBM_SETPOS,wParam,0);
            UpdateStatus(hwnd,L"Downloading verified release... "+
                std::to_wstring(wParam)+L"%");
        }
        return 0;
    }
    if(message==WM_TCS_UPDATE_STAGE_DONE) {
        if(check) {
            check->busy=false;
            if(!check->stageOkay) {
                UpdateStatus(hwnd,check->stage.description);
                if(check->progress)ShowWindow(check->progress,SW_HIDE);
                if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL))
                    EnableWindow(install,TRUE);
            } else {
                UpdateStatus(hwnd,L"SHA-256 verified. Starting protected native installer...");
                const auto module=ModuleFile(OwnModule());
                std::wstring error;
                const bool launched=tcs::update::BeginNativeInstall(check->stage,module,
                                                        GetCurrentProcessId(),error);
                check->installationStarted=launched;
                if(launched) {
                    UpdateStatus(hwnd,L"Verified ZIP staged. Close Total Commander and the "+
                        std::wstring(L"Link Maker window to finish installation."));
                    if(check->progress)SendMessageW(check->progress,PBM_SETPOS,100,0);
                } else {
                    UpdateStatus(hwnd,L"Update could not start: "+error);
                }
                if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL))
                    EnableWindow(install,!launched);
            }
        }
        return 0;
    }
    if(message==WM_DRAWITEM && lParam) {
        const auto& item=*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if(item.CtlID==ID_UPDATE_INSTALL || item.CtlID==ID_UPDATE_OPEN ||
           item.CtlID==ID_UPDATE_CLOSE) {
            DrawUpdateButton(item);return TRUE;
        }
    }
    if(message==WM_COMMAND) {
        if(LOWORD(wParam)==ID_UPDATE_INSTALL && check &&
           (check->newer||check->canReinstall) && !check->busy &&
           !check->installationStarted) {
            check->busy=true;
            if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL))EnableWindow(install,FALSE);
            if(check->progress) {
                ShowWindow(check->progress,SW_SHOW);
                SendMessageW(check->progress,PBM_SETPOS,0,0);
            }
            UpdateStatus(hwnd,L"Downloading and verifying official GitHub ZIP...");
            std::thread([check] {
                try {check->stageOkay=tcs::update::StageNativeUpdate(check->tag,check->window,
                                                        check->stage);}
                catch (...) {
                    check->stageOkay=false;
                    check->stage.description=L"Download failed unexpectedly.";
                }
                PostMessageW(check->window,WM_TCS_UPDATE_STAGE_DONE,0,0);
            }).detach();
            return 0;
        }
        if(LOWORD(wParam)==ID_UPDATE_OPEN && check &&
           (check->newer||check->canReinstall) && !check->url.empty()) {
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
        if(GetDlgCtrlID(control)==ID_UPDATE_TITLE ||
           GetDlgCtrlID(control)==ID_UPDATE_BRAND) {
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
    if(message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    // Secondary window must not issue WM_QUIT to the Link Maker.
    if(message==WM_NCDESTROY) {
        auto* holder=reinterpret_cast<std::shared_ptr<UpdateCheck>*>(
            GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        delete holder;
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
void AddUpdateToolTip(HWND parent, HWND control,
                      const wchar_t* english, const wchar_t* russian,
                      const wchar_t* hebrew) {
    if(!control) return;
    const LANGID language=GetUserDefaultUILanguage();
    const WORD primary=PRIMARYLANGID(language);
    const wchar_t* value=primary==LANG_RUSSIAN?russian:
                         primary==LANG_HEBREW?hebrew:english;
    if(!value || !*value) return;
    HWND tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,
        WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
        parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!tip)return;
    TOOLINFOW info{};
    info.cbSize=sizeof(info);
    info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
    info.hwnd=parent;
    info.uId=reinterpret_cast<UINT_PTR>(control);
    info.lpszText=const_cast<LPWSTR>(value);
    if(!SendMessageW(tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info))) {
        DestroyWindow(tip);
        return;
    }
    const UINT dpi=GetDpiForWindow(parent);
    SendMessageW(tip,TTM_SETMAXTIPWIDTH,0,MulDiv(440,dpi,96));
    SendMessageW(tip,TTM_SETDELAYTIME,TTDT_INITIAL,600);
    SendMessageW(tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,18000);
}
bool NativeManualCheck(HWND owner) {
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    // A standalone Update Manager is also hosted by rundll32.exe.
    // Identify it to Windows separately from the generic host executable.
    wchar_t hostName[MAX_PATH]{};
    if(GetModuleFileNameW(nullptr,hostName,MAX_PATH)) {
        const wchar_t* basename=wcsrchr(hostName,L'\\');
        if(basename && _wcsicmp(basename+1,L"rundll32.exe")==0)
            SetCurrentProcessExplicitAppUserModelID(L"TC.SymbolicLinks.LinkMaker");
    }
    const wchar_t* klass=L"TCSymbolicLinksNativeUpdateWindow";
    WNDCLASSW cls{};cls.lpfnWndProc=UpdateWndProc;cls.hInstance=instance;
    const HMODULE iconModule=OwnModule();
    cls.hIcon=iconModule?LoadIconW(iconModule,MAKEINTRESOURCEW(101)):nullptr;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=CreateSolidBrush(RGB(245,248,253));
    cls.lpszClassName=klass;
    RegisterClassW(&cls);
    const UINT dpi=owner&&IsWindow(owner)?GetDpiForWindow(owner):GetDpiForSystem();
    const auto dp=[dpi](int value){return MulDiv(value,static_cast<int>(dpi),96);};
    const int width=dp(610),height=dp(252);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,klass,
        L"TC Symbolic Links - Update Manager",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,width,height,owner,nullptr,instance,nullptr);
    if(!window)return false;
    if(iconModule) {
        HICON big=static_cast<HICON>(LoadImageW(iconModule,MAKEINTRESOURCEW(101),
            IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
        HICON littleIcon=static_cast<HICON>(LoadImageW(iconModule,MAKEINTRESOURCEW(101),
            IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        if(big)SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(big));
        if(littleIcon)SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(littleIcon));
    }
    RECT wr{},anchor{};
    GetWindowRect(window,&wr);
    if(!owner || !IsWindow(owner) || !GetWindowRect(owner,&anchor)) {
        MONITORINFO mi{sizeof(mi)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&mi))
            anchor=mi.rcWork;
    }
    MONITORINFO mi{sizeof(mi)};
    if(GetMonitorInfoW(MonitorFromWindow(owner&&IsWindow(owner)?owner:window,
        MONITOR_DEFAULTTONEAREST),&mi)) {
        const int x=(std::max)(mi.rcWork.left,(std::min)(
            anchor.left+(anchor.right-anchor.left-width)/2,mi.rcWork.right-width));
        const int y=(std::max)(mi.rcWork.top,(std::min)(
            anchor.top+(anchor.bottom-anchor.top-height)/2,mi.rcWork.bottom-height));
        SetWindowPos(window,nullptr,x,y,0,0,
            SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    auto check=std::make_shared<UpdateCheck>();
    check->window=window;
    SetWindowLongPtrW(window,GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(new std::shared_ptr<UpdateCheck>(check)));
    // Embedded icon + branded header, no generic Windows icon or emoji fonts.
    HWND head=CreateWindowW(L"STATIC",L"      TC Symbolic Links v0.2.18  |  Update Manager",
        WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE,0,0,width,dp(57),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_TITLE)),instance,nullptr);
    if(iconModule) {
        HWND brand=CreateWindowW(L"STATIC",nullptr,WS_CHILD|WS_VISIBLE|SS_ICON,
            dp(14),dp(13),dp(32),dp(32),window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_BRAND)),instance,nullptr);
        HICON large=static_cast<HICON>(LoadImageW(iconModule,MAKEINTRESOURCEW(101),
            IMAGE_ICON,dp(32),dp(32),LR_SHARED));
        if(large)SendMessageW(brand,STM_SETICON,reinterpret_cast<WPARAM>(large),0);
    }
    const HFONT body=CreateFontW(-dp(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    if(body)SendMessageW(head,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
    check->status=CreateWindowW(L"STATIC",L"Checking GitHub releases...",
        WS_CHILD|WS_VISIBLE|SS_LEFT,dp(22),dp(73),dp(555),dp(52),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_RESULT)),instance,nullptr);
    if(body)SendMessageW(check->status,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
    INITCOMMONCONTROLSEX c{sizeof(c),ICC_PROGRESS_CLASS|ICC_WIN95_CLASSES};
    InitCommonControlsEx(&c);
    check->progress=CreateWindowW(PROGRESS_CLASSW,nullptr,WS_CHILD|PBS_SMOOTH,
        dp(22),dp(126),dp(555),dp(12),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_PROGRESS)),instance,nullptr);
    if(check->progress)SendMessageW(check->progress,PBM_SETRANGE,0,MAKELPARAM(0,100));
    CreateWindowW(L"BUTTON",L"Download & Install",WS_CHILD|BS_OWNERDRAW,
        dp(20),dp(155),dp(230),dp(40),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_INSTALL)),instance,nullptr);
    CreateWindowW(L"BUTTON",L"Release page",WS_CHILD|BS_OWNERDRAW,
        dp(263),dp(155),dp(151),dp(40),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_OPEN)),instance,nullptr);
    CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        dp(428),dp(155),dp(154),dp(40),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_CLOSE)),instance,nullptr);
    if(body) {
        for(int id : {ID_UPDATE_INSTALL,ID_UPDATE_OPEN,ID_UPDATE_CLOSE}) {
            if(HWND button=GetDlgItem(window,id))
                SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
        }
    }
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_INSTALL),
        L"Download the official GitHub ZIP, verify SHA-256 and file architecture.\n"
        L"Install the selected version after closing Total Commander.\n"
        L"Existing plugin DLLs are backed up; UAC may be requested.",
        L"Скачать официальный ZIP, проверить SHA-256 и архитектуру DLL.\n"
        L"Установить выбранную версию после закрытия Total Commander.\n"
        L"Создаются резервные копии; возможен запрос UAC.",
        L"הורדת ZIP רשמי ואימות SHA-256 וארכיטקטורת DLL.\n"
        L"ההתקנה מתבצעת לאחר סגירת Total Commander.\n"
        L"נוצרים גיבויים; ייתכן שתידרש הרשאת מנהל.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_OPEN),
        L"Open the selected official GitHub Release page in your browser.",
        L"Открыть страницу выбранного официального релиза на GitHub.",
        L"פתיחת דף הגרסה הרשמית שנבחרה ב-GitHub.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_CLOSE),
        L"Close Update Manager only. The main Link Maker stays open.",
        L"Закрыть только окно обновлений. Основное окно создания ссылок останется.",
        L"סגירת מנהל העדכונים בלבד, בלי לסגור את חלון יצירת הקישורים.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_RESULT),
        L"Shows the installed/released version or download and installation status.",
        L"Показывает найденную версию и ход проверки, загрузки и установки.",
        L"מציג את הגרסה ואת מצב הבדיקה, ההורדה וההתקנה.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_PROGRESS),
        L"Progress of the verified ZIP download; installation begins after Total Commander closes.",
        L"Прогресс скачивания проверенного ZIP. Установка — после закрытия TC.",
        L"התקדמות ההורדה; ההתקנה מתבצעת לאחר סגירת TC.");
    ShowWindow(window,SW_SHOW);
    std::thread([check]{
        try{InspectReleases(*check);}
        catch(...){check->result=L"Update check failed.";}
        PostMessageW(check->window,WM_TCS_UPDATE_DONE,0,0);
    }).detach();
    MSG msg{};
    while(IsWindow(window)) {
        const BOOL rc=GetMessageW(&msg,nullptr,0,0);
        if(rc<=0){if(rc==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}
        if(!IsDialogMessageW(window,&msg)) {
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }
    if(body)DeleteObject(body);
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
