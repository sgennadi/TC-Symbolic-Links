#include "UpdateManager.h"
#include "NativeUpdateInstall.h"
#include "TcUiPolish.h"
#include "TcUiGraphics.h"
#include <dwmapi.h>
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
    const std::wstring version = L"0.2.23";
#else
    const std::wstring version = L"0.2.23-preview." +
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
constexpr int ID_UPDATE_INSTALLED = 408;
constexpr int ID_UPDATE_AVAILABLE = 409;
constexpr int ID_UPDATE_HELP = 410;
constexpr int ID_UPDATE_TRUST = 411;
constexpr int ID_UPDATE_INSTALLED_LABEL = 412;
constexpr int ID_UPDATE_AVAILABLE_LABEL = 413;
constexpr wchar_t kUiVersion[] = L"v0.2.23";
inline tcs::ui::Lang UpdateLanguage(){
    const LANGID lang=GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang)==LANG_RUSSIAN?tcs::ui::Lang::Russian:
        PRIMARYLANGID(lang)==LANG_HEBREW?tcs::ui::Lang::Hebrew:
        tcs::ui::Lang::English;
}
struct UpdateCheck {
    HWND window = nullptr;
    HWND status = nullptr;
    HWND progress = nullptr;
    HWND installed = nullptr;
    HWND available = nullptr;
    HWND help = nullptr;
    HWND trust = nullptr;
    HFONT bodyFont=nullptr,valueFont=nullptr,labelFont=nullptr;
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
    NetHandle session{WinHttpOpen(L"TC-Symbolic-Links-NativeUpdater/0.2.23",
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
    const Version installed=ParseVersion("0.2.23");
#else
    const Version installed=ParseVersion("0.2.23-preview."+std::to_string(TC_PREVIEW_ITERATION));
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
    const UINT dpi=GetDpiForWindow(item.hwndItem);
    const bool main=item.CtlID==ID_UPDATE_INSTALL;
    const auto glyph=main?tcs::ui::Glyph::Download:
        item.CtlID==ID_UPDATE_OPEN?tcs::ui::Glyph::External:
        tcs::ui::Glyph::Close;
    tcs::ui::PaintButton(item,dpi,main,glyph);
}
void UpdateLayout(HWND hwnd) {
    if(!GetDlgItem(hwnd,ID_UPDATE_CLOSE))return;
    RECT rc{};GetClientRect(hwnd,&rc);
    const int w=rc.right,h=rc.bottom;
    const UINT dpi=GetDpiForWindow(hwnd);
    const auto dp=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    const int margin=dp(21),gap=dp(10);
    const int inner=(std::max)(dp(260),w-2*margin);
    const bool tight=h<dp(310);
    const int rowX=margin;
    const int versionsTop=dp(tight?75:86);
    const int half=(inner-gap)/2;
    auto position=[&](int id,int x,int y,int width,int height){
        if(HWND control=GetDlgItem(hwnd,id))
            MoveWindow(control,x,y,(std::max)(1,width),(std::max)(1,height),TRUE);
    };
    position(ID_UPDATE_INSTALLED_LABEL,rowX+dp(16),versionsTop+dp(8),
             half-dp(24),dp(19));
    position(ID_UPDATE_INSTALLED,rowX+dp(16),versionsTop+dp(tight?25:28),
             half-dp(24),dp(tight?26:31));
    position(ID_UPDATE_AVAILABLE_LABEL,rowX+half+gap+dp(16),
             versionsTop+dp(8),half-dp(24),dp(19));
    position(ID_UPDATE_AVAILABLE,rowX+half+gap+dp(16),
             versionsTop+dp(tight?25:28),half-dp(24),dp(tight?26:31));
    const int buttonsY=h-dp(57),buttonH=dp(42);
    const int mainW=(std::max)(dp(160),(inner-2*gap)*47/100);
    const int releaseW=(std::max)(dp(120),(inner-2*gap-mainW)*54/100);
    const int closeW=(std::max)(dp(65),inner-mainW-releaseW-2*gap);
    position(ID_UPDATE_INSTALL,margin,buttonsY,mainW,buttonH);
    position(ID_UPDATE_OPEN,margin+mainW+gap,buttonsY,releaseW,buttonH);
    position(ID_UPDATE_CLOSE,margin+mainW+gap+releaseW+gap,buttonsY,
             closeW,buttonH);
    const int statusY=dp(tight?143:171);
    const int hintY=buttonsY-dp(tight?27:31);
    const int helpY=statusY+dp(27);
    position(ID_UPDATE_RESULT,margin+dp(17),statusY,inner-dp(34),dp(24));
    position(ID_UPDATE_HELP,margin+dp(17),helpY,inner-dp(34),
             (std::max)(dp(18),hintY-helpY-dp(6)));
    position(ID_UPDATE_TRUST,margin+dp(2),hintY,inner-dp(4),dp(21));
    position(ID_UPDATE_PROGRESS,margin+dp(2),buttonsY-dp(7),
             inner-dp(4),dp(5));
    InvalidateRect(hwnd,nullptr,FALSE);
}
void PaintUpdateSurface(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC screen=BeginPaint(hwnd,&ps);
    if(!screen)return;
    RECT rc{};GetClientRect(hwnd,&rc);
    const int w=rc.right,h=rc.bottom;
    HDC memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=memory?CreateCompatibleBitmap(screen,w,h):nullptr;
    HGDIOBJ previous=memory&&bitmap?SelectObject(memory,bitmap):nullptr;
    HDC dc=memory&&bitmap?memory:screen;
    HBRUSH canvas=CreateSolidBrush(tcs::visual::Canvas);
    FillRect(dc,&rc,canvas);DeleteObject(canvas);
    const UINT dpi=GetDpiForWindow(hwnd);
    const auto dp=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    const int margin=dp(20),inner=w-2*margin;
    const bool tight=h<dp(310);
    const int gap=dp(10),half=(inner-gap)/2;
    HICON icon=reinterpret_cast<HICON>(
        SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0));
    const RECT header{margin,dp(tight?9:12),w-margin,dp(tight?65:76)};
    tcs::ui::Header(dc,header,dpi,icon,
        L"TC Symbolic Links",L"UPDATE MANAGER",kUiVersion);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    tcs::visual::Card(g,Gdiplus::RectF(static_cast<float>(margin)+.5f,
        static_cast<float>(dp(tight?75:86))+.5f,
        static_cast<float>(half)-1.f,static_cast<float>(dp(tight?61:71))-1.f),
        static_cast<float>(dp(11)),tcs::visual::White,tcs::visual::Stroke);
    tcs::visual::Card(g,Gdiplus::RectF(static_cast<float>(margin+half+gap)+.5f,
        static_cast<float>(dp(tight?75:86))+.5f,
        static_cast<float>(half)-1.f,static_cast<float>(dp(tight?61:71))-1.f),
        static_cast<float>(dp(11)),tcs::visual::White,tcs::visual::Stroke);
    const int statusY=dp(tight?138:165);
    const int buttonsY=h-dp(57);
    const int cardHeight=(std::max)(dp(52),buttonsY-statusY-dp(tight?29:33));
    tcs::visual::Card(g,Gdiplus::RectF(static_cast<float>(margin)+.5f,
        static_cast<float>(statusY)+.5f,
        static_cast<float>(inner)-1.f,static_cast<float>(cardHeight)-1.f),
        static_cast<float>(dp(11)),tcs::visual::White,tcs::visual::Stroke);
    if(dc==memory)BitBlt(screen,0,0,w,h,dc,0,0,SRCCOPY);
    if(previous)SelectObject(memory,previous);
    if(bitmap)DeleteObject(bitmap);
    if(memory)DeleteDC(memory);
    EndPaint(hwnd,&ps);
}
std::shared_ptr<UpdateCheck> CheckFor(HWND hwnd) {
    auto* value=reinterpret_cast<std::shared_ptr<UpdateCheck>*>(
        GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    return value?*value:nullptr;
}
void ApplyUpdateFonts(HWND hwnd,UpdateCheck& check){
    const UINT dpi=GetDpiForWindow(hwnd);
    const auto dp=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    HFONT normal=CreateFontW(-dp(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    HFONT strong=CreateFontW(-dp(19),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    HFONT label=CreateFontW(-dp(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    auto set=[&](int id,HFONT font){
        if(HWND control=GetDlgItem(hwnd,id))
            if(font)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    };
    for(int id:{ID_UPDATE_RESULT,ID_UPDATE_HELP,ID_UPDATE_INSTALL,
                ID_UPDATE_OPEN,ID_UPDATE_CLOSE})set(id,normal);
    for(int id:{ID_UPDATE_INSTALLED,ID_UPDATE_AVAILABLE})set(id,strong);
    for(int id:{ID_UPDATE_INSTALLED_LABEL,ID_UPDATE_AVAILABLE_LABEL,
                ID_UPDATE_TRUST})set(id,label);
    if(check.bodyFont)DeleteObject(check.bodyFont);
    if(check.valueFont)DeleteObject(check.valueFont);
    if(check.labelFont)DeleteObject(check.labelFont);
    check.bodyFont=normal;check.valueFont=strong;check.labelFont=label;
}
void UpdateStatus(HWND hwnd,const std::wstring& text) {
    if(HWND target=GetDlgItem(hwnd,ID_UPDATE_RESULT))
        SetWindowTextW(target,text.c_str());
}
LRESULT CALLBACK UpdateWndProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    const auto check=CheckFor(hwnd);
    if(message==WM_TCS_UPDATE_DONE) {
        if(check) {
            const auto lang=UpdateLanguage();
            const bool actionable=check->newer||check->canReinstall;
            if(HWND available=GetDlgItem(hwnd,ID_UPDATE_AVAILABLE)) {
                const auto value=check->tag.empty()?
                    tcs::ui::Translate(lang,L"Not available",L"Нет данных",
                        L"לא זמין"):tcs::ui::Version(check->tag);
                SetWindowTextW(available,value.c_str());
            }
            if(check->newer)UpdateStatus(hwnd,tcs::ui::Translate(lang,
                L"A new release is ready.",L"Доступна новая версия.",
                L"קיימת גרסה חדשה."));
            else if(check->canReinstall)UpdateStatus(hwnd,tcs::ui::Translate(lang,
                L"You're up to date. Reinstallation is optional.",
                L"Установлена актуальная версия. Повторная установка необязательна.",
                L"הגרסה מעודכנת. התקנה מחדש אינה חובה."));
            else UpdateStatus(hwnd,check->result);
            if(HWND help=GetDlgItem(hwnd,ID_UPDATE_HELP)) {
                const auto copy=tcs::ui::UpdateDescription(
                    check->newer,check->canReinstall,lang);
                SetWindowTextW(help,copy.c_str());
            }
            if(HWND open=GetDlgItem(hwnd,ID_UPDATE_OPEN))
                ShowWindow(open,actionable?SW_SHOW:SW_HIDE);
            if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL)) {
                ShowWindow(install,actionable?SW_SHOW:SW_HIDE);
                const auto label=tcs::ui::UpdateButton(check->newer,
                    check->canReinstall,check->tag,lang);
                SetWindowTextW(install,label.c_str());
                EnableWindow(install,actionable);
            }
            SetWindowTextW(hwnd,L"TC Symbolic Links v0.2.23 - Update Manager");
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    }
    if(message==WM_TCS_UPDATE_PROGRESS) {
        if(check && check->busy) {
            if(check->progress)SendMessageW(check->progress,PBM_SETPOS,wParam,0);
            UpdateStatus(hwnd,tcs::ui::Translate(UpdateLanguage(),
                L"Downloading release... ",L"Загрузка релиза... ",
                L"מוריד גרסה... ")+std::to_wstring(wParam)+L"%");
        }
        return 0;
    }
    if(message==WM_TCS_UPDATE_STAGE_DONE) {
        if(check) {
            check->busy=false;
            if(!check->stageOkay) {
                UpdateStatus(hwnd,check->stage.description);
                if(check->progress)ShowWindow(check->progress,SW_HIDE);
                if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL)){
                    const auto copy=tcs::ui::Translate(UpdateLanguage(),
                        L"Retry update",L"Повторить",L"נסה שוב");
                    SetWindowTextW(install,copy.c_str());
                    EnableWindow(install,TRUE);
                }
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
                if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL)){
                    const auto copy=launched?
                        tcs::ui::Translate(UpdateLanguage(),L"Install pending",
                            L"Установка ожидает",L"ההתקנה ממתינה"):
                        tcs::ui::Translate(UpdateLanguage(),L"Retry update",
                            L"Повторить",L"נסה שוב");
                    SetWindowTextW(install,copy.c_str());
                    EnableWindow(install,!launched);
                }
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
            if(HWND install=GetDlgItem(hwnd,ID_UPDATE_INSTALL)){
                const auto copy=tcs::ui::Translate(UpdateLanguage(),
                    L"Downloading...",L"Загрузка...",L"מוריד...");
                SetWindowTextW(install,copy.c_str());
                EnableWindow(install,FALSE);
            }
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
    if(message==WM_PAINT){PaintUpdateSurface(hwnd);return 0;}
    if(message==WM_ERASEBKGND)return TRUE;
    if(message==WM_SIZE){UpdateLayout(hwnd);return 0;}
    if(message==WM_DPICHANGED){
        const RECT* suggested=reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
            suggested->right-suggested->left,
            suggested->bottom-suggested->top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        if(check)ApplyUpdateFonts(hwnd,*check);
        UpdateLayout(hwnd);
        return 0;
    }
    if(message==WM_CTLCOLORSTATIC) {
        HDC dc=reinterpret_cast<HDC>(wParam);
        const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        static HBRUSH white=CreateSolidBrush(tcs::visual::White);
        static HBRUSH canvas=CreateSolidBrush(tcs::visual::Canvas);
        SetBkMode(dc,TRANSPARENT);
        if(id==ID_UPDATE_TRUST){
            SetTextColor(dc,tcs::visual::Muted);
            return reinterpret_cast<LRESULT>(canvas);
        }
        SetTextColor(dc,id==ID_UPDATE_AVAILABLE||id==ID_UPDATE_INSTALLED?
            tcs::visual::Navy:id==ID_UPDATE_INSTALLED_LABEL||
            id==ID_UPDATE_AVAILABLE_LABEL||id==ID_UPDATE_HELP?
            tcs::visual::Muted:tcs::visual::Ink);
        return reinterpret_cast<LRESULT>(white);
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
    // Native Win32 + GDI+ dialog embedded in the installed WDX.
    // The update remains an explicit, verified, deferred operation.
    tcs::ui::GdiSession graphics;
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    wchar_t hostName[MAX_PATH]{};
    if(GetModuleFileNameW(nullptr,hostName,MAX_PATH)){
        const wchar_t* basename=wcsrchr(hostName,L'\\');
        if(basename && _wcsicmp(basename+1,L"rundll32.exe")==0)
            SetCurrentProcessExplicitAppUserModelID(L"TC.SymbolicLinks.LinkMaker");
    }
    const wchar_t* klass=L"TCSymbolicLinksNativeUpdateWindow";
    WNDCLASSW cls{};
    cls.lpfnWndProc=UpdateWndProc;
    cls.hInstance=instance;
    const HMODULE iconModule=OwnModule();
    cls.hIcon=iconModule?LoadIconW(iconModule,MAKEINTRESOURCEW(101)):nullptr;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    cls.lpszClassName=klass;
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    const HWND anchorHwnd=owner&&IsWindow(owner)?owner:nullptr;
    const UINT dpi=anchorHwnd?GetDpiForWindow(anchorHwnd):GetDpiForSystem();
    const auto dp=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    MONITORINFO monitor{sizeof(monitor)};
    const HMONITOR current=MonitorFromWindow(
        anchorHwnd?anchorHwnd:GetDesktopWindow(),MONITOR_DEFAULTTONEAREST);
    if(!GetMonitorInfoW(current,&monitor))
        SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0);
    const int workW=monitor.rcWork.right-monitor.rcWork.left;
    const int workH=monitor.rcWork.bottom-monitor.rcWork.top;
    const auto logical=tcs::ui::UpdateWindowSize(
        MulDiv(workW,96,dpi),MulDiv(workH,96,dpi));
    const int width=(std::min)(dp(logical.width),workW);
    const int height=(std::min)(dp(logical.height),workH);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,klass,
        L"TC Symbolic Links v0.2.23 - Update Manager",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,width,height,anchorHwnd,nullptr,instance,nullptr);
    if(!window)return false;
    if(iconModule) {
        HICON big=static_cast<HICON>(LoadImageW(iconModule,MAKEINTRESOURCEW(101),
            IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
        HICON smallIcon=static_cast<HICON>(LoadImageW(iconModule,MAKEINTRESOURCEW(101),
            IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        if(big)SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(big));
        if(smallIcon)SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(smallIcon));
    }
    const COLORREF caption=RGB(11,46,90);
    const COLORREF white=RGB(255,255,255);
    DwmSetWindowAttribute(window,35,&caption,sizeof(caption));
    DwmSetWindowAttribute(window,36,&white,sizeof(white));
    RECT anchor{};
    if(anchorHwnd)GetWindowRect(anchorHwnd,&anchor);
    else anchor=monitor.rcWork;
    const int x=(std::max)(monitor.rcWork.left,(std::min)(
        anchor.left+(anchor.right-anchor.left-width)/2,
        monitor.rcWork.right-width));
    const int y=(std::max)(monitor.rcWork.top,(std::min)(
        anchor.top+(anchor.bottom-anchor.top-height)/2,
        monitor.rcWork.bottom-height));
    SetWindowPos(window,nullptr,x,y,0,0,
        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    auto check=std::make_shared<UpdateCheck>();
    check->window=window;
    SetWindowLongPtrW(window,GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(new std::shared_ptr<UpdateCheck>(check)));
    INITCOMMONCONTROLSEX commons{sizeof(commons),ICC_PROGRESS_CLASS|ICC_WIN95_CLASSES};
    InitCommonControlsEx(&commons);
    auto text=[&](int id,const std::wstring& content,DWORD style){
        return CreateWindowW(L"STATIC",content.c_str(),
            WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    };
    const auto lang=UpdateLanguage();
    text(ID_UPDATE_INSTALLED_LABEL,tcs::ui::Translate(lang,
        L"INSTALLED",L"УСТАНОВЛЕНО",L"מותקן"),SS_LEFT);
    text(ID_UPDATE_AVAILABLE_LABEL,tcs::ui::Translate(lang,
        L"AVAILABLE",L"ДОСТУПНО",L"זמין"),SS_LEFT);
#if TC_STABLE_RELEASE
    const std::wstring installed=kUiVersion;
#else
    const std::wstring installed=std::wstring(kUiVersion)+L" preview";
#endif
    check->installed=text(ID_UPDATE_INSTALLED,installed,SS_ENDELLIPSIS);
    check->available=text(ID_UPDATE_AVAILABLE,tcs::ui::Translate(lang,
        L"Checking...",L"Проверка...",L"בודק..."),SS_ENDELLIPSIS);
    check->status=text(ID_UPDATE_RESULT,tcs::ui::Translate(lang,
        L"Checking official GitHub releases...",L"Проверка релизов GitHub...",
        L"בודק גרסאות GitHub..."),SS_LEFT);
    check->help=text(ID_UPDATE_HELP,tcs::ui::UpdateDescription(
        false,false,lang),SS_LEFT);
    check->trust=text(ID_UPDATE_TRUST,tcs::ui::Translate(lang,
        L"Official GitHub release  |  SHA-256 before install  |  UAC may be required",
        L"GitHub  |  SHA-256 перед установкой  |  Возможен запрос UAC",
        L"גרסת GitHub רשמית  |  SHA-256 לפני התקנה  |  ייתכן אישור מנהל"),
        SS_LEFT|SS_ENDELLIPSIS);
    check->progress=CreateWindowW(PROGRESS_CLASSW,nullptr,
        WS_CHILD|PBS_SMOOTH,0,0,0,0,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_UPDATE_PROGRESS)),
        instance,nullptr);
    if(check->progress)SendMessageW(check->progress,PBM_SETRANGE,0,MAKELPARAM(0,100));
    CreateWindowW(L"BUTTON",L"",WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,
        0,0,0,0,window,reinterpret_cast<HMENU>(
        static_cast<INT_PTR>(ID_UPDATE_INSTALL)),instance,nullptr);
    CreateWindowW(L"BUTTON",tcs::ui::Translate(lang,
        L"Release notes",L"Изменения",L"פרטי גרסה").c_str(),
        WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,
        0,0,0,0,window,reinterpret_cast<HMENU>(
        static_cast<INT_PTR>(ID_UPDATE_OPEN)),instance,nullptr);
    CreateWindowW(L"BUTTON",tcs::ui::Translate(lang,
        L"Close",L"Закрыть",L"סגור").c_str(),
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,
        0,0,0,0,window,reinterpret_cast<HMENU>(
        static_cast<INT_PTR>(ID_UPDATE_CLOSE)),instance,nullptr);
    ApplyUpdateFonts(window,*check);
    UpdateLayout(window);
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_INSTALL),
        L"Download official ZIP, verify SHA-256 and x86/x64 binaries. After TC closes, install the verified files. Does not change TC buttons.",
        L"Скачать официальный ZIP и проверить SHA-256 и архитектуру. Установка после закрытия TC. Кнопки TC не изменяются.",
        L"הורדה ואימות של ZIP רשמי; ההתקנה תושלם לאחר סגירת TC.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_OPEN),
        L"Read release notes and full details on GitHub.",
        L"Прочитать описание и историю изменений релиза на GitHub.",
        L"קריאת פרטי הגרסה והשינויים ב-GitHub.");
    AddUpdateToolTip(window,GetDlgItem(window,ID_UPDATE_CLOSE),
        L"Close only this window. The Link Maker remains open.",
        L"Закрыть только это окно. Link Maker останется открытым.",
        L"סגור חלון זה בלבד; החלון הראשי יישאר פתוח.");
    ShowWindow(window,SW_SHOW);
    UpdateWindow(window);
    std::thread([check]{
        try{InspectReleases(*check);}
        catch(...){check->result=L"Update check failed.";}
        PostMessageW(check->window,WM_TCS_UPDATE_DONE,0,0);
    }).detach();
    MSG msg{};
    while(IsWindow(window)){
        const BOOL rc=GetMessageW(&msg,nullptr,0,0);
        if(rc<=0){if(rc==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(check->bodyFont)DeleteObject(check->bodyFont);
    if(check->valueFont)DeleteObject(check->valueFont);
    if(check->labelFont)DeleteObject(check->labelFont);
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
