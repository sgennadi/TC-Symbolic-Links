// Native HiDPI Unicode link maker hosted inside the WDX DLL.
// One window for file/directory symlinks, directory junctions and hardlinks.
#include "LinkCore.h"
#include "LinkBatchPlan.h"
#include "LinkManagerCore.h"
#include "LinkManagerGui.h"
#include "TcMakerDensity.h"
#include "BatchPreviewGui.h"
#include "LinkFailureHelp.h"
#include "ListParser.h"
#include "TcMakerGui.h"
#include "UpdateManager.h"
#include "TcVisualTheme.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <richedit.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"TCSymbolicLinksMakerWindow";
constexpr int ID_SOURCES = 100;
constexpr int ID_DEST = 101;
constexpr int ID_KIND = 102;
constexpr int ID_RELATIVE = 103;
constexpr int ID_RESULTS = 104;
constexpr int ID_CREATE = 105;
constexpr int ID_CLOSE = 106;
constexpr int ID_MODE_HINT = 108;
constexpr int ID_RELATIVE_HINT = 109;
constexpr int ID_PREVIEW = 110;
constexpr int ID_SOURCE_PATH = 111;
constexpr int ID_SOURCE_GLYPH = 112;
constexpr int ID_TARGET_GLYPH = 113;
constexpr int ID_STATUS_GLYPH = 114;
constexpr int ID_BROWSE = 115;
constexpr int ID_UPDATE = 116;
constexpr int ID_NAME_PATTERN = 117;
constexpr int ID_BATCH_PREVIEW = 118;
constexpr int ID_LINK_MANAGER = 119;
constexpr int ID_UNDO_LAST = 120;
constexpr int ID_COMPACT = 121;
constexpr UINT_PTR ID_ANIMATION_TIMER = 7301;
constexpr UINT WM_TCS_CREATE_NEXT = WM_APP + 41;
constexpr UINT WM_TCS_TEST_TOOLTIPS = WM_APP + 80; // read-only CI diagnostic
constexpr std::size_t kCreateBatchSize = 12;

class GdiPlusScope {
    ULONG_PTR token_ = 0;
    bool ready_ = false;
public:
    GdiPlusScope() {
        Gdiplus::GdiplusStartupInput input;
        ready_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok;
    }
    GdiPlusScope(const GdiPlusScope&) = delete;
    GdiPlusScope& operator=(const GdiPlusScope&) = delete;
    ~GdiPlusScope() { if (ready_) Gdiplus::GdiplusShutdown(token_); }
    bool Ready() const { return ready_; }
};

// Respect the Windows/Total Commander classic colours and user's OS theme.
constexpr COLORREF kCanvas = tcs::visual::Canvas;
constexpr COLORREF kSurface = tcs::visual::White;
constexpr COLORREF kLine = tcs::visual::Stroke;
constexpr COLORREF kInk = tcs::visual::Ink;
constexpr COLORREF kSecondary = tcs::visual::Muted;
constexpr COLORREF kAccent = tcs::visual::Blue;
constexpr COLORREF kAccentHover = tcs::visual::DarkBlue;
constexpr COLORREF kPositive = tcs::visual::Green;
constexpr COLORREF kDanger = tcs::visual::Red;
constexpr COLORREF kWarning = RGB(217, 119, 6);
constexpr COLORREF kSoftButton = RGB(235, 240, 248);

struct Ui {
    bool ru = false;
    bool he = false;
    UINT dpi = 96;
    HFONT font = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontLabel = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontIcon = nullptr;
    HMODULE richEditModule = nullptr;
    HICON folderIcon = nullptr;
    bool hoveringCreate = false;
    bool hoveringClose = false;
    bool hoveringBrowse = false;
    bool hoveringUpdate = false;
    unsigned animationFrame = 0;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH cardBrush = nullptr;
    HBRUSH fieldBrush = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND labelSources = nullptr;
    HWND sourceGlyph = nullptr;
    HWND targetGlyph = nullptr;
    HWND statusGlyph = nullptr;
    HWND sources = nullptr;
    HWND sourcePath = nullptr;
    HWND labelDestination = nullptr;
    HWND destination = nullptr;
    HWND browse = nullptr;
    HWND labelKind = nullptr;
    HWND kind = nullptr;
    HWND modeHint = nullptr;
    HWND relative = nullptr;
    HWND relativeHint = nullptr;
    HWND preview = nullptr;
    HWND labelResults = nullptr;
    HWND results = nullptr;
    HWND status = nullptr;
    HWND create = nullptr;
    HWND close = nullptr;
    HWND update = nullptr;
    HWND patternLabel = nullptr;
    HWND namePattern = nullptr;
    HWND batchPreview = nullptr;
    HWND linkManager = nullptr;
    HWND undoLast = nullptr;
    HWND compactToggle = nullptr;
    bool compact = false;
    std::vector<tcs::batch::Row> batchRows;
    std::vector<tcs::manager::Identity> undoItems;
    HWND tooltip = nullptr; // one native tooltip window, many owned controls
    int tooltipCount = 0;
    std::vector<std::wstring> selected;
    std::wstring initialDestination;
    bool creating = false;
    // The toolbar may start the Maker in Automatic, File or Directory mode.
    // Users can still change the combo before pressing F5.
    int initialMode = 0;
    bool privilegeError = false;
    bool hasRun = false;
    bool unsafeDestination = false;
    std::wstring unsafeReason;
    std::size_t nextItem = 0;
    int completed = 0;
    int failed = 0;
    int warnings = 0;
    tcs::LinkKind batchKind = tcs::LinkKind::Automatic;
    bool batchRelative = false;
    std::wstring batchDestination;
    ~Ui() {
        for (HFONT f : {font, fontTitle, fontLabel, fontSmall, fontIcon}) if (f) DeleteObject(f);
        if (backgroundBrush) DeleteObject(backgroundBrush);
        if (cardBrush) DeleteObject(cardBrush);
        if (fieldBrush) DeleteObject(fieldBrush);
        if (richEditModule) FreeLibrary(richEditModule);
        if (folderIcon) DestroyIcon(folderIcon);
    }
    const wchar_t* Choose(const wchar_t* english, const wchar_t* russian,
                          const wchar_t* hebrew = nullptr) const {
        if (he && hebrew) return hebrew;
        return ru ? russian : english;
    }
};

std::wstring GetText(HWND control);
// Persist only user-controlled Maker preferences. Never touch TC or Samba.
constexpr wchar_t kMakerSettings[]=L"Software\\TCSymbolicLinks\\Maker";
std::wstring LoadNamePattern() {
    wchar_t pattern[256]{};
    DWORD bytes=sizeof(pattern);
    const auto code=RegGetValueW(HKEY_CURRENT_USER,kMakerSettings,
        L"NamePattern",RRF_RT_REG_SZ,nullptr,pattern,&bytes);
    if(code!=ERROR_SUCCESS||bytes<sizeof(wchar_t)||
       bytes>sizeof(pattern))return L"{Name}";
    pattern[255]=L'\0';
    return pattern;
}
bool LoadCompactMode() {
    DWORD mode=0,bytes=sizeof(mode);
    return RegGetValueW(HKEY_CURRENT_USER,kMakerSettings,
        L"CompactUI",RRF_RT_REG_DWORD,nullptr,&mode,&bytes)==ERROR_SUCCESS &&
        mode==1;
}
void SaveMakerPreferences(const Ui& state) {
    if(!state.namePattern || !state.compactToggle)return;
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,kMakerSettings,0,nullptr,
         REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        return;
    const std::wstring pattern=GetText(state.namePattern);
    if(pattern.size()<=240)
        RegSetValueExW(key,L"NamePattern",0,REG_SZ,
            reinterpret_cast<const BYTE*>(pattern.c_str()),
            static_cast<DWORD>((pattern.size()+1)*sizeof(wchar_t)));
    const DWORD mode=state.compact?1u:0u;
    RegSetValueExW(key,L"CompactUI",0,REG_DWORD,
        reinterpret_cast<const BYTE*>(&mode),sizeof(mode));
    RegCloseKey(key);
}
int Dp(const Ui& state, int n) { return MulDiv(n, static_cast<int>(state.dpi), 96); }

std::wstring GetText(HWND control) {
    const auto n = GetWindowTextLengthW(control);
    if (n <= 0) return {};
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    const int actual = GetWindowTextW(control, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(actual));
    return s;
}

void SetRichTextColor(HWND edit, COLORREF color, bool all = false) {
    if (!edit) return;
    wchar_t cls[48]{};
    GetClassNameW(edit, cls, 48);
    if (_wcsicmp(cls, L"RICHEDIT50W") != 0) return;
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = color;
    SendMessageW(edit, EM_SETCHARFORMAT, all ? SCF_ALL : SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&format));
}
void AppendLine(HWND edit, const std::wstring& line) {
    if (!edit) return;
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    COLORREF tint = kInk;
    if (line.rfind(L"OK ", 0) == 0) tint = kPositive;
    else if (line.rfind(L"ERR ", 0) == 0) tint = kDanger;
    else if (line.rfind(L"WARN ", 0) == 0) tint = kWarning;
    else if (line.rfind(L"Created:", 0) == 0 ||
             line.rfind(L"Создано:", 0) == 0) tint = kAccent;
    else if (line.find(L"Stopped") != std::wstring::npos ||
             line.find(L"Остановлено") != std::wstring::npos) tint = kWarning;
    SetRichTextColor(edit, tint);
    const std::wstring text = line + L"\r\n";
    SendMessageW(edit, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

HFONT MakeFont(UINT dpi, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0,
        weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void RecreateFont(Ui& state) {
    const auto oldFont = state.font;
    const auto oldTitle = state.fontTitle;
    const auto oldLabel = state.fontLabel;
    const auto oldSmall = state.fontSmall;
    const auto oldIcon = state.fontIcon;
    state.font = MakeFont(state.dpi, 9, FW_NORMAL);
    state.fontTitle = MakeFont(state.dpi, 10, FW_SEMIBOLD);
    state.fontLabel = MakeFont(state.dpi, 9, FW_SEMIBOLD);
    state.fontSmall = MakeFont(state.dpi, 9, FW_NORMAL);
    state.fontIcon = CreateFontW(-MulDiv(16, static_cast<int>(state.dpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe MDL2 Assets");
    if (!state.font) state.font = oldFont;
    if (!state.fontTitle) state.fontTitle = oldTitle;
    if (!state.fontLabel) state.fontLabel = oldLabel;
    if (!state.fontSmall) state.fontSmall = oldSmall;
    if (!state.fontIcon) state.fontIcon = oldIcon;
    for (HWND item : {state.sources, state.sourcePath, state.destination, state.kind,
                      state.browse,
                      state.relative, state.results, state.create, state.close, state.update,
                      state.namePattern, state.batchPreview, state.linkManager,
                      state.undoLast, state.compactToggle}) {
        if (item && state.font) SendMessageW(item, WM_SETFONT,
                                             reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    if (state.title && state.fontTitle) SendMessageW(state.title, WM_SETFONT,
                                                     reinterpret_cast<WPARAM>(state.fontTitle), TRUE);
    for (HWND item : {state.labelSources, state.labelDestination, state.labelKind,
                      state.labelResults, state.patternLabel}) {
        if (item && state.fontLabel) SendMessageW(item, WM_SETFONT,
                                                  reinterpret_cast<WPARAM>(state.fontLabel), TRUE);
    }
    for (HWND item : {state.subtitle, state.modeHint, state.relativeHint, state.preview,
                      state.status}) {
        if (item && state.fontSmall) SendMessageW(item, WM_SETFONT,
                                                  reinterpret_cast<WPARAM>(state.fontSmall), TRUE);
    }
    if (oldFont && oldFont != state.font) DeleteObject(oldFont);
    if (oldTitle && oldTitle != state.fontTitle) DeleteObject(oldTitle);
    if (oldLabel && oldLabel != state.fontLabel) DeleteObject(oldLabel);
    if (oldSmall && oldSmall != state.fontSmall) DeleteObject(oldSmall);
    if (oldIcon && oldIcon != state.fontIcon) DeleteObject(oldIcon);
}

HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* title,
                DWORD style, int id, DWORD extended = 0) {
    return CreateWindowExW(extended, cls, title, style | WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

// Compare identities as well as spellings: .\, mixed case, short (8.3)
// aliases and junctions can all refer to the same physical parent directory.
bool SameDirectory(const std::wstring& first, const std::wstring& second) {
    if (first.empty() || second.empty()) return false;
    if (_wcsicmp(first.c_str(), second.c_str()) == 0) return true;
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const auto open = [&](const std::wstring& path) {
        return CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    };
    HANDLE a = open(first), b = open(second);
    BY_HANDLE_FILE_INFORMATION ai{}, bi{};
    const bool equal = a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(a, &ai) != FALSE &&
        GetFileInformationByHandle(b, &bi) != FALSE &&
        ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
        ai.nFileIndexHigh == bi.nFileIndexHigh &&
        ai.nFileIndexLow == bi.nFileIndexLow;
    if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
    if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
    return equal;
}
bool DirectoryAtOrInside(std::wstring child, std::wstring parent) {
    if (child.empty() || parent.empty()) return false;
    if (_wcsicmp(child.c_str(), parent.c_str()) == 0) return true;
    if (parent.back() != L'\\' && parent.back() != L'/') parent.push_back(L'\\');
    if (child.size() <= parent.size()) return false;
    return _wcsnicmp(child.c_str(), parent.c_str(), parent.size()) == 0;
}
std::wstring InvalidDestinationReason(const Ui& state, const std::wstring& dest) {
    if (dest.empty() || !tcs::IsExistingDirectory(dest)) {
        // %Z lets TC pass the real *virtual* path of a WCX/WIM panel.
        // A path containing an existing regular file (boot.wim\\image1)
        // cannot be a Windows filesystem directory. Never fall back to the
        // archive's parent, where we could overwrite the selected source.
        if (!dest.empty() && !tcs::ExistingFileAncestor(dest).empty())
            return state.Choose(
                L"Target panel is inside an archive or file (WIM/WCX). Select a real filesystem folder.",
                L"Целевая панель внутри файла/архива WIM/WCX. Выберите обычную папку на диске.",
                L"חלונית היעד בתוך קובץ או ארכיון WIM/WCX. בחר תיקייה רגילה בדיסק.");
        return state.Choose(
            L"Target is not a real filesystem folder. Choose an existing disk or SMB directory, not a virtual panel.",
            L"Назначение не является обычной папкой. Выберите каталог на диске или SMB, а не виртуальную панель.",
            L"היעד אינו תיקיית מערכת קבצים. בחר תיקיית דיסק או SMB קיימת.");
    }
    for (std::size_t i = 0; i < state.selected.size(); ++i) {
        const auto& selected = state.selected[i];
        const auto source = tcs::FullPath(selected);
        const auto name = tcs::batch::MakeName(tcs::BaseName(selected),
            GetText(state.namePattern), i);
        if (source.empty() || name.empty() || name == L"." || name == L"..") continue;
        const auto parent = tcs::FullPath(tcs::ParentPath(source));
        const auto output = tcs::FullPath(tcs::Join(dest, name));
        // Never manufacture a link on top of its original file or directory.
        // A custom name in the same parent is allowed; self-links are not.
        if ((SameDirectory(parent, dest) &&
             tcs::batch::Fold(name)==tcs::batch::Fold(tcs::BaseName(selected))) ||
            (!output.empty() && _wcsicmp(output.c_str(), source.c_str()) == 0)) {
            return state.Choose(
                L"Source and destination are the same folder. Choose another target panel.",
                L"Источник и назначение — одна папка. Выберите другую папку справа.",
                L"המקור והיעד הם אותה תיקייה. יש לבחור תיקיית יעד אחרת.");
        }
        if (tcs::IsExistingDirectory(source) && DirectoryAtOrInside(dest, source)) {
            return state.Choose(
                L"Destination is inside the selected source folder. This would make a recursive link.",
                L"Назначение внутри исходной папки. Такая ссылка станет рекурсивной.",
                L"תיקיית היעד נמצאת בתוך המקור. פעולה זו תיצור קישור רקורסיבי.");
        }
    }
    return {};
}
void RefreshCreateSafety(Ui& s) {
    if (!s.destination || !s.create) return;
    const std::wstring destination = tcs::FullPath(GetText(s.destination));
    s.unsafeReason = InvalidDestinationReason(s, destination);
    s.unsafeDestination = !s.unsafeReason.empty();
    if (!s.creating) EnableWindow(s.create, !s.unsafeDestination);
    if (!s.creating && !s.hasRun && s.status) {
        SetWindowTextW(s.status, s.unsafeDestination ? s.unsafeReason.c_str() :
            s.Choose(L"Ready to create links.", L"Готово к созданию ссылок.",
                     L"מוכן ליצירת קישורים."));
    }
    if (s.status) InvalidateRect(s.status, nullptr, TRUE);
    if (s.statusGlyph) InvalidateRect(s.statusGlyph, nullptr, TRUE);
    if (s.create) InvalidateRect(s.create, nullptr, FALSE);
    if (s.preview) InvalidateRect(s.preview, nullptr, TRUE);
    if (HWND owner = GetParent(s.destination)) {
        RECT bottom{};
        GetClientRect(owner, &bottom);
        bottom.top = (std::max)(LONG{0}, bottom.bottom - static_cast<LONG>(Dp(s, 74)));
        InvalidateRect(owner, &bottom, FALSE);
    }
}

// Read-only batch plan: use exactly the same mapping for preview and F5.
std::vector<tcs::batch::Row> BuildBatchPlan(Ui& s,std::size_t limit){
    std::vector<tcs::batch::Source> inputs;
    const std::size_t count=(std::min)(s.selected.size(),limit);
    inputs.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto path=tcs::FullPath(s.selected[i]);
        const DWORD attr=path.empty()?INVALID_FILE_ATTRIBUTES:
            GetFileAttributesW(path.c_str());
        inputs.push_back({path,attr!=INVALID_FILE_ATTRIBUTES,
                          attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY)!=0});
    }
    const int index=s.kind?int(SendMessageW(s.kind,CB_GETCURSEL,0,0)):0;
    return tcs::batch::Plan(inputs,tcs::FullPath(GetText(s.destination)),
        GetText(s.namePattern),index,[](const std::wstring& link){
            const DWORD attr=GetFileAttributesW(link.c_str());
            if(attr!=INVALID_FILE_ATTRIBUTES)return true;
            // Read the link object itself: a broken NTFS/SMB reparse point
            // can still occupy a name even when its target is missing.
            HANDLE h=CreateFileW(link.c_str(),FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|
                FILE_FLAG_BACKUP_SEMANTICS,nullptr);
            if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);return true;}
            const DWORD error=GetLastError();
            // Treat uncertainty as occupied; never overwrite by guessing.
            return error==ERROR_ACCESS_DENIED || error==ERROR_SHARING_VIOLATION ||
                   error==ERROR_LOCK_VIOLATION;
        });
}
void UpdatePlannedOutput(Ui& s){
    if(!s.results||s.creating||s.hasRun)return;
    if(s.unsafeDestination){
        SetWindowTextW(s.results,(std::wstring(L" ! ")+s.unsafeReason).c_str());
        SetRichTextColor(s.results,kDanger,true);
        return;
    }
    const auto rows=BuildBatchPlan(s,80);
    std::wstring lines;
    for(const auto& row:rows){
        lines+=L"  ["+std::wstring(tcs::batch::StatusName(row.status))+
            L"] "+row.name+L" -> "+row.destination+L"\r\n";
    }
    if(rows.size()<s.selected.size())lines+=s.Choose(
        L"...more selected items",L"...остальные выделенные элементы",
        L"...פריטים נוספים");
    SetWindowTextW(s.results,lines.c_str());
    SetRichTextColor(s.results,kInk,true);
}
void UpdatePreview(Ui& s){
    if(!s.preview||!s.destination||s.selected.empty())return;
    RefreshCreateSafety(s);
    const auto name=tcs::batch::MakeName(tcs::BaseName(s.selected.front()),
        GetText(s.namePattern),0);
    const auto destination=tcs::FullPath(GetText(s.destination));
    std::wstring value=s.unsafeDestination?
        (std::wstring(L"!  ")+s.unsafeReason):
        std::wstring(s.Choose(L"New link: ",L"Новая ссылка: ",L"קישור חדש: "));
    if(!s.unsafeDestination)
        value+=name.empty()||destination.empty()?s.Choose(
            L"(choose source and destination)",L"(укажите источник и назначение)",
            L"(בחר מקור ויעד)"):tcs::Join(destination,name);
    if(s.selected.size()>1)value+=L"  (+"+
        std::to_wstring(s.selected.size()-1)+L")";
    SetWindowTextW(s.preview,value.c_str());
    UpdatePlannedOutput(s);
}

void UpdateModeHelp(Ui& s) {
    if (!s.kind || !s.modeHint) return;
    const auto selection = SendMessageW(s.kind, CB_GETCURSEL, 0, 0);
    const wchar_t* description = nullptr;
    switch (selection) {
        case 1:
            description = s.Choose(
                L"File symlink: another path to a FILE. Works across volumes; creating it may need Developer Mode.",
                L"Symlink файла: путь к ФАЙЛУ, в том числе на другом диске. Может потребоваться режим разработчика.",
                L"קישור סמלי לקובץ: יכול להפנות גם לכונן אחר; ייתכן צורך במצב מפתח.");
            break;
        case 2:
            description = s.Choose(
                L"Folder symlink: another path to a FOLDER. Can link across volumes; may need Developer Mode.",
                L"Symlink папки: ссылка на ПАПКУ, в том числе на другом диске. Может потребоваться режим разработчика.",
                L"קישור סמלי לתיקייה: גם לכונן אחר; ייתכן צורך במצב מפתח.");
            break;
        case 3:
            description = s.Choose(
                L"Junction: folder-to-folder link on a local Windows volume. Stores an absolute target, not relative.",
                L"Junction: ссылка на ПАПКУ на локальном томе Windows. Хранит абсолютный путь.",
                L"Junction: קישור לתיקייה בכונן מקומי; שומר נתיב מוחלט בלבד.");
            break;
        case 4:
            description = s.Choose(
                L"Hardlink: a second name for the SAME FILE on the SAME volume. Not supported for directories.",
                L"Hardlink: второе имя того же ФАЙЛА на том же диске. Для папок не поддерживается.",
                L"Hardlink: שם נוסף לאותו קובץ באותו כונן; אינו מתאים לתיקיות.");
            break;
        default:
            description = s.Choose(
                L"Automatic: folder selections become folder symlinks, files become file symlinks.",
                L"Автоматически: для папок создаётся symlink папки, для файлов — symlink файла.",
                L"אוטומטי: לתיקיות קישור סמלי לתיקייה ולקבצים קישור סמלי לקובץ.");
            break;
    }
    SetWindowTextW(s.modeHint, description);
    const bool relativeAvailable = selection < 3 && selection >= 0;
    EnableWindow(s.relative, relativeAvailable && !s.creating);
    if (!relativeAvailable) SendMessageW(s.relative, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextW(s.relativeHint,
        relativeAvailable ? s.Choose(
            L"Relative target = e.g. ..\\Software instead of D:\\Software. Useful when both folders move together; requires the same volume.",
            L"Относительный путь = например ..\\Software вместо D:\\Software. Удобен при совместном переносе папок на одном диске.",
            L"נתיב יחסי: למשל ..\\Software במקום D:\\Software. שימושי כאשר שתי התיקיות זזות יחד באותו כונן.") :
        s.Choose(L"Relative targets are not available for junctions and hardlinks.",
                 L"Для Junction и Hardlink относительные пути не используются.",
                 L"ל-Junction ול-Hardlink אין נתיב יעד יחסי."));
}

// Windows 10/11 include Segoe MDL2 Assets. Icons are vector glyphs, so they
// stay crisp at 100%, 150%, 200% DPI without shipping extra images or DLLs.
bool PaintFolderIcon(HDC dc, const Ui& state, const RECT& rect) {
    if (!state.folderIcon) return false;
    const int size = Dp(state, 19);
    const int x = rect.left + (rect.right - rect.left - size) / 2;
    const int y = rect.top + (rect.bottom - rect.top - size) / 2;
    return DrawIconEx(dc, x, y, state.folderIcon, size, size, 0, nullptr,
                      DI_NORMAL) != FALSE;
}
void PaintSymbol(HDC dc, const Ui& state, RECT rect, wchar_t symbol, COLORREF tint) {
    if (!state.fontIcon) return;
    const HGDIOBJ oldFont = SelectObject(dc, state.fontIcon);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, tint);
    wchar_t glyph[2]{symbol, L'\0'};
    DrawTextW(dc, glyph, 1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
}
LRESULT CALLBACK ActionButtonProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam, UINT_PTR id, DWORD_PTR value) {
    auto* state = reinterpret_cast<Ui*>(value);
    if (state) {
        bool& hovering = id == ID_CREATE ? state->hoveringCreate :
                         id == ID_CLOSE ? state->hoveringClose :
                         id == ID_UPDATE ? state->hoveringUpdate : state->hoveringBrowse;
        if (msg == WM_MOUSEMOVE && !hovering) {
            hovering = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (msg == WM_MOUSELEAVE && hovering) {
            hovering = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
void PaintAction(const Ui& state, const DRAWITEMSTRUCT& item) {
    RECT r = item.rcItem;
    const bool create = item.CtlID == ID_CREATE;
    const bool update = item.CtlID == ID_UPDATE;
    const bool browse = item.CtlID == ID_BROWSE || update;
    const bool enabled = (item.itemState & ODS_DISABLED) == 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = create ? state.hoveringCreate :
                     update ? state.hoveringUpdate :
                     browse ? state.hoveringBrowse : state.hoveringClose;
    const COLORREF fill = browse ? (hot ? tcs::visual::SoftBlue : kSurface) :
        create ? (!enabled ? RGB(192, 207, 216) : pressed ? tcs::visual::GreenDark :
                  hot ? tcs::visual::GreenHover : tcs::visual::Green) :
        state.creating ? (hot ? RGB(255, 220, 226) : tcs::visual::SoftRed) :
                         (hot ? tcs::visual::SoftBlue : kSurface);
    const COLORREF border = create ? fill :
        state.creating ? RGB(241, 179, 179) :
                         (hot ? tcs::visual::Blue : tcs::visual::Stroke);
    const COLORREF ink = browse ? tcs::visual::DarkBlue :
        create ? kSurface : state.creating ? kDanger : tcs::visual::Navy;

    // Double buffer the button. GDI+ anti-aliases its corners; avoid the
    // visibly jagged native GDI RoundRect edges reported at 150/200% DPI.
    HDC memory = CreateCompatibleDC(item.hDC);
    const int w = r.right - r.left, h = r.bottom - r.top;
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(item.hDC, w, h) : nullptr;
    HGDIOBJ originalBitmap = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    HDC dc = memory && bitmap ? memory : item.hDC;
    if (dc == memory) { r.left = 0; r.top = 0; r.right = w; r.bottom = h; }
    const COLORREF underlay = browse ? kSurface :
        state.unsafeDestination || state.failed ? tcs::visual::SoftRed :
        state.hasRun && !state.creating ? tcs::visual::SoftGreen :
        tcs::visual::SoftBlue;
    HBRUSH canvas = CreateSolidBrush(underlay);
    FillRect(dc, &r, canvas);
    DeleteObject(canvas);
    tcs::visual::RoundedButton(dc, r, fill, border, Dp(state, 9));

    // Native vector icons and readable text for every button, following
    // NipkgCreator.ModernButton instead of depending on MDL2 font glyphs.
    const int iconSize = Dp(state, 20);
    const int iconY = r.top + ((r.bottom - r.top) - iconSize) / 2;
    const int iconX = state.he ? r.right - Dp(state, 15) - iconSize :
                                 r.left + Dp(state, 14);
    const RECT icon{iconX, iconY, iconX + iconSize, iconY + iconSize};
    const auto glyph = update ? tcs::visual::ButtonGlyph::Refresh :
                       browse ? tcs::visual::ButtonGlyph::Folder :
                       create ? tcs::visual::ButtonGlyph::Create :
                       state.creating ? tcs::visual::ButtonGlyph::Stop :
                                        tcs::visual::ButtonGlyph::Close;
    tcs::visual::Glyph(dc, icon, glyph, ink, state.dpi);
    std::wstring label = GetText(item.hwndItem);
    RECT labelRect = state.he ?
        RECT{r.left + Dp(state, 8), r.top,
             r.right - Dp(state, 42), r.bottom} :
        RECT{r.left + Dp(state, 43), r.top,
             r.right - Dp(state, 8), r.bottom};
    const HGDIOBJ oldFont = state.fontLabel ?
        SelectObject(dc, state.fontLabel) : nullptr;
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldText = SetTextColor(dc, ink);
    DrawTextW(dc, label.c_str(), -1, &labelRect,
              (state.he ? (DT_RIGHT | DT_RTLREADING) : DT_LEFT) |
              DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(dc, oldText);
    SetBkMode(dc, oldMode);
    if (oldFont) SelectObject(dc, oldFont);
    if (item.itemState & ODS_FOCUS)
        tcs::visual::FocusRing(dc, r, state.dpi, create);
    if (dc == memory) BitBlt(item.hDC, item.rcItem.left, item.rcItem.top,
                            w, h, memory, 0, 0, SRCCOPY);
    if (originalBitmap) SelectObject(memory, originalBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
}
void PaintPanelSymbol(const Ui& state, const DRAWITEMSTRUCT& item) {
    RECT region = item.rcItem;
    HBRUSH fill = CreateSolidBrush(item.CtlID == ID_STATUS_GLYPH ?
        kCanvas : kSurface);
    FillRect(item.hDC, &region, fill);
    DeleteObject(fill);
    wchar_t glyph = static_cast<wchar_t>(0xE71B);
    COLORREF color = kAccent;
    if (item.CtlID == ID_SOURCE_GLYPH) {
        glyph = static_cast<wchar_t>(0xE8B7);
        color = RGB(217, 151, 19);
    } else if (item.CtlID == ID_TARGET_GLYPH) {
        glyph = static_cast<wchar_t>(0xE71B);
        color = kAccent;
    } else if (item.CtlID == ID_STATUS_GLYPH) {
        glyph = state.unsafeDestination ? static_cast<wchar_t>(0xE7BA) :
                state.creating ? static_cast<wchar_t>(0xE72C) :
                state.failed ? static_cast<wchar_t>(0xE783) :
                state.hasRun ? static_cast<wchar_t>(0xE73E) :
                               static_cast<wchar_t>(0xE71B);
        color = state.unsafeDestination || state.failed ? kDanger :
                state.creating ? kAccent :
                state.hasRun ? kPositive : kSecondary;
    }
    if (item.CtlID == ID_SOURCE_GLYPH) {
        if (!PaintFolderIcon(item.hDC, state, region))
            tcs::visual::Glyph(item.hDC, region,
                tcs::visual::ButtonGlyph::Folder, color, state.dpi);
    } else if (item.CtlID == ID_TARGET_GLYPH) {
        tcs::visual::Glyph(item.hDC, region,
            tcs::visual::ButtonGlyph::Link, color, state.dpi);
    } else {
        PaintSymbol(item.hDC, state, region, glyph, color);
    }
}
void AddToolTip(HWND parent, HWND control, const wchar_t* help) {
    if (!control || !help || !*help) return;
    auto* state=reinterpret_cast<Ui*>(GetWindowLongPtrW(parent,GWLP_USERDATA));
    if(!state)return;
    if(!state->tooltip) {
        // One owner-managed common-controls tooltip for all Maker fields:
        // avoids multiple overlapping hover windows and subclass conflicts.
        state->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,
            WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
            CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
            parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!state->tooltip)return;
        SendMessageW(state->tooltip,TTM_SETMAXTIPWIDTH,0,Dp(*state,430));
        SendMessageW(state->tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,600);
        SendMessageW(state->tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,18000);
    }
    TOOLINFOW tip{};
    tip.cbSize=sizeof(tip);
    tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
    tip.hwnd=parent;
    tip.uId=reinterpret_cast<UINT_PTR>(control);
    tip.lpszText=const_cast<LPWSTR>(help);
    if(!SendMessageW(state->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip))) {
        // Some Common Controls builds reject the newest TOOLINFO size.
        // V2 contains the HWND, Unicode text and subclass flags we use.
        tip.cbSize=TTTOOLINFOW_V2_SIZE;
        if(!SendMessageW(state->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip)))
            return;
    }
    ++state->tooltipCount;
}
void ChooseDestination(HWND owner, Ui& state) {
    if (!state.destination || state.creating) return;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                     COINIT_DISABLE_OLE1DDE);
    const bool ownApartment = SUCCEEDED(apartment);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) && dialog) {
        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(state.Choose(
            L"Choose destination folder", L"Выберите папку назначения",
            L"בחירת תיקיית יעד"));
        const auto current = tcs::FullPath(GetText(state.destination));
        IShellItem* initial = nullptr;
        if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(
                current.c_str(), nullptr, IID_PPV_ARGS(&initial))) && initial) {
            dialog->SetFolder(initial);
            initial->Release();
        }
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* choice = nullptr;
            if (SUCCEEDED(dialog->GetResult(&choice)) && choice) {
                PWSTR path = nullptr;
                if (SUCCEEDED(choice->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    SetWindowTextW(state.destination, path);
                    // Also supports programmatic SetWindowText on nested dialogs.
                    RefreshCreateSafety(state);
                    UpdatePreview(state);
                    CoTaskMemFree(path);
                }
                choice->Release();
            }
        }
        dialog->Release();
    }
    if (ownApartment) CoUninitialize();
}

int MeasuredButtonWidth(const Ui& state, HWND button, int baseDp) {
    const int minimum = Dp(state, baseDp);
    if (!button) return minimum;
    HDC dc = GetDC(button);
    if (!dc) return minimum;
    const HFONT font = state.fontLabel ? state.fontLabel : state.font;
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    const std::wstring caption = GetText(button);
    SIZE measured{};
    const BOOL ok = GetTextExtentPoint32W(dc, caption.c_str(),
        static_cast<int>(caption.size()), &measured);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(button, dc);
    // Mimic NipkgCreator's measured text + icon + generous right padding.
    return ok ? (std::max)(minimum, static_cast<int>(measured.cx) + Dp(state, 68)) : minimum;
}

void Layout(HWND hwnd, Ui& s) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const auto layout=tcs::maker::density::Resolve(
        s.compact,width,height,static_cast<int>(s.dpi));
    const bool compact=layout.compact || layout.adaptive;
    const int margin=Dp(s,layout.margin);
    const int gap=Dp(s,layout.gap);
    const int usable = (std::max)(Dp(s, 320), width - 2 * margin);
    const int cardWidth = (usable - gap) / 2;
    const int cardRightX = margin + cardWidth + gap;
    const int inset = Dp(s, 13);
    const int leftX = margin + inset;
    const int rightX = cardRightX + inset;
    const int leftWidth = cardWidth - 2 * inset;
    const int rightWidth = usable - cardWidth - gap - 2 * inset;

    auto place = [&](HWND child, int x, int y, int w, int h) {
        if (child) MoveWindow(child, x, y, (std::max)(1, w),
                              (std::max)(1, h), TRUE);
    };
    // The blue/green rounded banner is painted in WM_PAINT, not by
    // transparent STATIC controls (which erase the gradient on Windows).
    const int toolsY=Dp(s,layout.toolsY);
    const int labelWidth = Dp(s, 86);
    const int kindWidth=(std::min)(Dp(s,344),
        (std::max)(Dp(s,158),usable/2-labelWidth-Dp(s,15)));
    place(s.labelKind, leftX, toolsY + Dp(s, 7), labelWidth, Dp(s, 23));
    place(s.kind, leftX + labelWidth, toolsY + Dp(s, 1),
          kindWidth, Dp(s, 300));
    const int checkX = leftX + labelWidth + kindWidth + Dp(s, 16);
    place(s.relative, checkX, toolsY + Dp(s, 5),
          (std::max)(Dp(s, 105), width - margin - inset - checkX), Dp(s, 25));

    const int patternY=Dp(s,layout.patternY);
    const int patternH=Dp(s,layout.patternH);
    const int patternStart=leftX;
    const int patternLabelW=Dp(s,compact?56:77);
    const int patternGap=Dp(s,compact?4:7);
    const int remaining=(width-margin-inset)-patternStart-patternLabelW-
        5*patternGap;
    const int unit=(std::max)(Dp(s,31),remaining/6);
    place(s.patternLabel,patternStart,patternY+Dp(s,5),
        patternLabelW,patternH-Dp(s,7));
    const int editX=patternStart+patternLabelW+patternGap;
    place(s.namePattern,editX,patternY,unit*2,patternH);
    const int previewX=editX+unit*2+patternGap;
    place(s.batchPreview,previewX,patternY,unit,patternH);
    const int managerX=previewX+unit+patternGap;
    place(s.linkManager,managerX,patternY,unit,patternH);
    const int undoX=managerX+unit+patternGap;
    place(s.undoLast,undoX,patternY,unit,patternH);
    const int compactX=undoX+unit+patternGap;
    place(s.compactToggle,compactX,patternY,unit,patternH);
    const bool shortLabels=compact || unit<Dp(s,95);
    SetWindowTextW(s.batchPreview,shortLabels?s.Choose(
        L"Plan",L"План",L"תכנון"):s.Choose(
        L"Preview",L"Просмотр",L"תצוגה"));
    SetWindowTextW(s.linkManager,shortLabels?s.Choose(
        L"Links",L"Ссылки",L"קישורים"):s.Choose(
        L"Link Manager",L"Менеджер ссылок",L"מנהל קישורים"));
    SetWindowTextW(s.undoLast,shortLabels?s.Choose(
        L"Undo",L"Отмена",L"בטל"):s.Choose(
        L"Undo Last",L"Отменить",L"בטל אחרון"));
    const int headingY=Dp(s,layout.headingY);
    place(s.sourceGlyph, leftX, headingY - Dp(s, 2), Dp(s, 24), Dp(s, 24));
    place(s.labelSources, leftX + Dp(s, 29), headingY,
          leftWidth - Dp(s, 29), Dp(s, 22));
    place(s.targetGlyph, rightX, headingY - Dp(s, 2), Dp(s, 24), Dp(s, 24));
    place(s.labelResults, rightX + Dp(s, 29), headingY,
          rightWidth - Dp(s, 29), Dp(s, 22));
    const int pathY=Dp(s,layout.pathY);
    const int pathH = Dp(s,layout.pathH);
    const int fieldPad = Dp(s, 10);
    const int browseGap = Dp(s, 9);
    const int browseWidth = (std::min)(
        MeasuredButtonWidth(s, s.browse, 126), rightWidth - Dp(s, 135));
    const int targetFieldWidth = rightWidth - browseWidth - browseGap;
    // The edit controls are inset into anti-aliased rounded frames.
    place(s.sourcePath, leftX + fieldPad, pathY + Dp(s, 6),
          leftWidth - 2 * fieldPad, pathH - Dp(s, 12));
    place(s.destination, rightX + fieldPad, pathY + Dp(s, 6),
          targetFieldWidth - 2 * fieldPad, pathH - Dp(s, 12));
    place(s.browse, rightX + targetFieldWidth + browseGap, pathY,
          browseWidth, pathH);

    const int paneY = pathY + pathH + Dp(s,layout.compact?7:9);
    const int paneBottom=height-Dp(s,layout.paneBottomInset);
    const int paneHeight = (std::max)(Dp(s, 64), paneBottom - paneY);
    place(s.sources, leftX, paneY, leftWidth, paneHeight);
    place(s.results, rightX, paneY, rightWidth, paneHeight);
    if (s.sources) {
        const int nameWidth = (std::max)(Dp(s, 120), leftWidth * 43 / 100);
        const int typeWidth = Dp(s, 84);
        ListView_SetColumnWidth(s.sources, 0, nameWidth);
        ListView_SetColumnWidth(s.sources, 1, typeWidth);
        ListView_SetColumnWidth(s.sources, 2,
            (std::max)(Dp(s, 75), leftWidth - nameWidth - typeWidth - Dp(s, 7)));
    }

    const int cardBottom = height - Dp(s,layout.cardBottomInset);
    const int infoY = cardBottom + Dp(s,6);
    ShowWindow(s.modeHint,layout.compact?SW_HIDE:SW_SHOW);
    ShowWindow(s.relativeHint,layout.compact?SW_HIDE:SW_SHOW);
    if(!layout.compact) {
        place(s.modeHint, margin + Dp(s,15), infoY + Dp(s,3),
              usable - Dp(s,30), Dp(s,24));
        place(s.relativeHint, margin + Dp(s,15), infoY + Dp(s,30),
              usable - Dp(s,30), Dp(s,24));
    }
    place(s.preview, margin + Dp(s,15),
          height - Dp(s,layout.previewBottomInset),
          usable - Dp(s,30), Dp(s,layout.compact?17:22));

    // Buttons sit inside an adaptive soft-green/blue/red status card.
    const int buttonY = height - Dp(s, 61);
    const int actionH = Dp(s, 40);
    const int createWidth = MeasuredButtonWidth(s, s.create, 148);
    const int closeWidth = MeasuredButtonWidth(s, s.close, 126);
    const int updateWidth = MeasuredButtonWidth(s, s.update, 180);
    place(s.update, width - margin - Dp(s, 10) - closeWidth -
          Dp(s, 10) - createWidth - Dp(s, 10) - updateWidth,
          buttonY, updateWidth, actionH);
    place(s.create, width - margin - Dp(s, 10) - closeWidth - Dp(s, 10) - createWidth,
          buttonY, createWidth, actionH);
    place(s.close, width - margin - Dp(s, 10) - closeWidth,
          buttonY, closeWidth, actionH);
    // Hidden accessibility/status controls retain their text and Win32 IDs.
    place(s.statusGlyph, leftX, buttonY + Dp(s, 7), Dp(s, 24), Dp(s, 24));
    place(s.status, leftX + Dp(s, 29), buttonY + Dp(s, 5),
          (std::max)(Dp(s, 60), usable - createWidth - closeWidth - updateWidth - Dp(s, 126)),
          Dp(s, 28));
    InvalidateRect(hwnd, nullptr, FALSE);
}
void CreateControls(HWND hwnd, Ui& s) {
    const DWORD captionEx = s.he ? WS_EX_RTLREADING : 0;
    const DWORD alignCaption = s.he ? SS_RIGHT : SS_LEFT;
    s.title = AddControl(hwnd, L"STATIC", L"TC Symbolic Links v0.2.21",
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
    const std::wstring subtitle = std::to_wstring(s.selected.size()) + L" " + s.Choose(
        L"selected  |  Existing files are never overwritten",
        L"выбрано  |  Существующие файлы не перезаписываются",
        L"נבחרו  |  קבצים קיימים לא יוחלפו");
    s.subtitle = AddControl(hwnd, L"STATIC", subtitle.c_str(),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
    // Text is drawn with antialiasing on top of the real gradient header.
    // Hide the legacy labels to prevent Windows static backgrounds erasing it.
    ShowWindow(s.title, SW_HIDE);
    ShowWindow(s.subtitle, SW_HIDE);
    s.labelKind = AddControl(hwnd, L"STATIC",
        s.Choose(L"Link type:", L"Тип ссылки:", L"סוג קישור:"),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
    s.kind = AddControl(hwnd, L"COMBOBOX", L"",
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_KIND);
    s.relative = AddControl(hwnd, L"BUTTON",
        s.Choose(L"Relative target (symlinks)",
                 L"Относительный путь (symlink)",
                 L"נתיב יחסי (קישור סמלי)"),
        WS_TABSTOP | BS_AUTOCHECKBOX | (s.he ? BS_RIGHTBUTTON : 0),
        ID_RELATIVE, captionEx);

    s.patternLabel=AddControl(hwnd,L"STATIC",s.Choose(
        L"Name:",L"Имя:",L"שם:"),SS_ENDELLIPSIS|alignCaption,0,captionEx);
    const std::wstring savedPattern=LoadNamePattern();
    s.namePattern=AddControl(hwnd,L"EDIT",savedPattern.c_str(),
        WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,ID_NAME_PATTERN);
    s.batchPreview=AddControl(hwnd,L"BUTTON",s.Choose(
        L"Preview",L"Просмотр",L"תצוגה"),WS_TABSTOP|BS_PUSHBUTTON,ID_BATCH_PREVIEW);
    s.linkManager=AddControl(hwnd,L"BUTTON",s.Choose(
        L"Link Manager",L"Менеджер ссылок",L"מנהל קישורים"),
        WS_TABSTOP|BS_PUSHBUTTON,ID_LINK_MANAGER);
    s.undoLast=AddControl(hwnd,L"BUTTON",s.Choose(
        L"Undo Last",L"Отменить",L"בטל אחרון"),
        WS_TABSTOP|BS_PUSHBUTTON,ID_UNDO_LAST);
    EnableWindow(s.undoLast,FALSE);
    s.compactToggle=AddControl(hwnd,L"BUTTON",s.Choose(
        L"Compact",L"Компактно",L"קומפקטי"),
        WS_TABSTOP|BS_AUTOCHECKBOX,ID_COMPACT);
    SendMessageW(s.compactToggle,BM_SETCHECK,
                 s.compact?BST_CHECKED:BST_UNCHECKED,0);

    const std::wstring sourceCaption = std::wstring(s.Choose(
        L"Source panel", L"Исходная панель", L"חלונית מקור")) + L"  [" +
        std::to_wstring(s.selected.size()) + L"]";
    s.labelSources = AddControl(hwnd, L"STATIC", sourceCaption.c_str(),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
    s.sourceGlyph = AddControl(hwnd, L"STATIC", L"", SS_OWNERDRAW, ID_SOURCE_GLYPH);
    s.targetGlyph = AddControl(hwnd, L"STATIC", L"", SS_OWNERDRAW, ID_TARGET_GLYPH);
    s.labelResults = AddControl(hwnd, L"STATIC", s.Choose(
        L"Target panel  |  Planned links / results",
        L"Целевая панель  |  Ссылки / результат",
        L"חלונית יעד  |  קישורים / תוצאות"),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);

    // Display the selected parent folder like TC's path bar. Never change the
    // original selected strings: the LinkCore still receives the exact paths.
    std::wstring origin = s.selected.empty() ? L"" : s.selected.front();
    auto stripSlash = [](std::wstring path) {
        while (path.size() > 3 &&
               (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        return path;
    };
    origin = stripSlash(std::move(origin));
    const auto slash = origin.find_last_of(L"\\/");
    const std::wstring parent = slash == std::wstring::npos ?
        L"" : origin.substr(0, slash + 1);
    bool sameParent = true;
    for (const auto& item : s.selected) {
        const auto source = stripSlash(item);
        const auto i = source.find_last_of(L"\\/");
        if ((i == std::wstring::npos ? L"" : source.substr(0, i + 1)) != parent) {
            sameParent = false;
            break;
        }
    }
    const std::wstring displayOrigin = sameParent ? parent : s.Choose(
        L"(multiple source folders)", L"(несколько исходных папок)",
        L"(מספר תיקיות מקור)");
    s.sourcePath = AddControl(hwnd, L"EDIT", displayOrigin.c_str(),
        ES_READONLY | ES_AUTOHSCROLL, ID_SOURCE_PATH);
    s.destination = AddControl(hwnd, L"EDIT", s.initialDestination.c_str(),
        WS_TABSTOP | ES_AUTOHSCROLL, ID_DEST);
    s.browse = AddControl(hwnd, L"BUTTON", s.Choose(
        L"Browse...", L"Обзор...", L"עיון..."),
        WS_TABSTOP | BS_OWNERDRAW, ID_BROWSE);

    // Native report-view table: familiar TC name/type/path columns and icons.
    s.sources = AddControl(hwnd, WC_LISTVIEWW, L"",
        WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS |
        LVS_SINGLESEL, ID_SOURCES, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(s.sources,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(s.sources, tcs::visual::Field);
    ListView_SetTextBkColor(s.sources, tcs::visual::Field);
    ListView_SetTextColor(s.sources, tcs::visual::Ink);
    auto col = [&](int index, int pixels, const wchar_t* text) {
        LVCOLUMNW column{};
        column.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;
        column.cx = Dp(s, pixels);
        column.fmt = LVCFMT_LEFT;
        column.pszText = const_cast<LPWSTR>(text);
        ListView_InsertColumn(s.sources, index, &column);
    };
    col(0, 185, s.Choose(L"Name", L"Имя", L"שם"));
    col(1, 74, s.Choose(L"Type", L"Тип", L"סוג"));
    col(2, 335, s.Choose(L"Full path", L"Полный путь", L"נתיב מלא"));

    SHFILEINFOW dirIcon{}, regularIcon{};
    const HIMAGELIST icons = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
        L"folder", FILE_ATTRIBUTE_DIRECTORY, &dirIcon, sizeof(dirIcon),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
    SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &regularIcon,
        sizeof(regularIcon), SHGFI_SYSICONINDEX | SHGFI_SMALLICON |
        SHGFI_USEFILEATTRIBUTES);
    if (icons) ListView_SetImageList(s.sources, icons, LVSIL_SMALL);

    for (std::size_t i = 0; i < s.selected.size(); ++i) {
        const auto& full = s.selected[i];
        const auto name = tcs::BaseName(full);
        const std::wstring normalized = stripSlash(full);
        const DWORD attrib = GetFileAttributesW(normalized.c_str());
        const bool directory = attrib != INVALID_FILE_ATTRIBUTES &&
                               (attrib & FILE_ATTRIBUTE_DIRECTORY) != 0;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = static_cast<int>(i);
        item.iImage = directory ? dirIcon.iIcon : regularIcon.iIcon;
        item.pszText = const_cast<LPWSTR>(name.c_str());
        const int row = ListView_InsertItem(s.sources, &item);
        if (row == -1) continue;
        const wchar_t* type = directory ? s.Choose(L"<DIR>", L"<КАТ>", L"<תיקייה>") :
                                          s.Choose(L"File", L"Файл", L"קובץ");
        ListView_SetItemText(s.sources, row, 1, const_cast<LPWSTR>(type));
        ListView_SetItemText(s.sources, row, 2, const_cast<LPWSTR>(full.c_str()));
    }
    if (!s.selected.empty())
        ListView_SetItemState(s.sources, 0, LVIS_SELECTED | LVIS_FOCUSED,
                             LVIS_SELECTED | LVIS_FOCUSED);

    // Microsoft's built-in RichEdit offers coloured per-link success/error
    // rows; the standard EDIT control remains a compatible fallback.
    s.richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (s.richEditModule) {
        s.results = AddControl(hwnd, L"RICHEDIT50W", L"",
            WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_BORDER, ID_RESULTS, WS_EX_CLIENTEDGE);
    }
    if (!s.results) s.results = AddControl(hwnd, L"EDIT", L"",
        WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_BORDER, ID_RESULTS, WS_EX_CLIENTEDGE);
    if (s.results && s.richEditModule)
        SendMessageW(s.results, EM_SETBKGNDCOLOR, 0,
            static_cast<LPARAM>(tcs::visual::Field));
    s.modeHint = AddControl(hwnd, L"STATIC", L"",
        SS_EDITCONTROL | alignCaption, ID_MODE_HINT, captionEx);
    s.relativeHint = AddControl(hwnd, L"STATIC", L"",
        SS_EDITCONTROL | alignCaption, ID_RELATIVE_HINT, captionEx);
    s.preview = AddControl(hwnd, L"STATIC", L"",
        SS_ENDELLIPSIS | alignCaption, ID_PREVIEW, captionEx);
    s.statusGlyph = AddControl(hwnd, L"STATIC", L"", SS_OWNERDRAW, ID_STATUS_GLYPH);
    s.status = AddControl(hwnd, L"STATIC", s.Choose(
        L"Ready.", L"Готово.", L"מוכן."), SS_ENDELLIPSIS | alignCaption, 0,
        captionEx);
    // Keep these HWNDs for status text, assistive tooling and old tests.
    // The visible status card is composited in the main window instead.
    ShowWindow(s.status, SW_HIDE);
    ShowWindow(s.statusGlyph, SW_HIDE);
    s.create = AddControl(hwnd, L"BUTTON", s.Choose(
        L"F5  Create", L"F5  Создать", L"F5  צור"),
        WS_TABSTOP | BS_OWNERDRAW, ID_CREATE);
    s.close = AddControl(hwnd, L"BUTTON", s.Choose(
        L"Esc  Close", L"Esc  Закрыть", L"Esc  סגור"),
        WS_TABSTOP | BS_OWNERDRAW, ID_CLOSE);
    s.update = AddControl(hwnd, L"BUTTON", s.Choose(
        L"Check for Updates", L"Проверить обновления", L"בדיקת עדכונים"),
        WS_TABSTOP | BS_OWNERDRAW, ID_UPDATE);

    for (const wchar_t* label : {
        s.Choose(L"Automatic (file / folder)", L"Автоматически (файл / папка)",
                 L"אוטומטי (קובץ / תיקייה)"),
        s.Choose(L"File symbolic link", L"Symlink файла", L"קישור סמלי לקובץ"),
        s.Choose(L"Directory symbolic link", L"Symlink папки", L"קישור סמלי לתיקייה"),
        s.Choose(L"Directory junction (NTFS)", L"Junction папки (NTFS)",
                 L"צומת תיקיות (NTFS)"),
        s.Choose(L"File hardlink", L"Hardlink файла", L"קישור קשיח לקובץ")
    }) SendMessageW(s.kind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));

    SendMessageW(s.kind, CB_SETCURSEL, static_cast<WPARAM>(s.initialMode), 0);
    SendMessageW(s.relative, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowSubclass(s.create, ActionButtonProc, ID_CREATE,
                      reinterpret_cast<DWORD_PTR>(&s));
    SetWindowSubclass(s.close, ActionButtonProc, ID_CLOSE,
                      reinterpret_cast<DWORD_PTR>(&s));
    SetWindowSubclass(s.update, ActionButtonProc, ID_UPDATE,
                      reinterpret_cast<DWORD_PTR>(&s));
    SetWindowSubclass(s.browse, ActionButtonProc, ID_BROWSE,
                      reinterpret_cast<DWORD_PTR>(&s));
    RecreateFont(s);
    UpdateModeHelp(s);
    UpdatePreview(s);
    Layout(hwnd, s);
    // Register mouse hover targets after controls have measurable geometry.
    // Help follows the same English/Russian/Hebrew language as the Maker.
    // Real Win32 hover tooltips, not permanent labels or a second dialog.
    const wchar_t* relativeHelp = s.Choose(
        L"Relative target (symbolic links only).\n"
        L"Off: the link stores a full path, e.g. D:\\Apps.\n"
        L"On: it stores a path from the LINK's folder, e.g. ..\\Apps.\n"
        L"Both paths must be on the same volume. Moving both folders together "
        L"preserves the link. Not available for Junction or Hardlink.",
        L"Относительный путь (только для символических ссылок).\n"
        L"Выкл.: ссылка хранит полный путь, например D:\\Apps.\n"
        L"Вкл.: путь относительно ПАПКИ ССЫЛКИ, например ..\\Apps.\n"
        L"Обе папки должны быть на одном томе. При совместном переносе "
        L"ссылка продолжит работать. Для Junction/Hardlink недоступно.",
        L"נתיב יחסי (קישורים סמליים בלבד).\n"
        L"כבוי: נשמר נתיב מלא, כגון D:\\Apps.\n"
        L"מופעל: נשמר נתיב יחסי לתיקיית הקישור, כגון ..\\Apps.\n"
        L"הנתיבים חייבים להיות באותו כונן. בעת העברת שתי התיקיות יחד, "
        L"הקישור ימשיך לעבוד. לא זמין עבור Junction או Hardlink.");
    AddToolTip(hwnd, s.relative, relativeHelp);
    AddToolTip(hwnd, s.relativeHint, relativeHelp);
    const wchar_t* linkTypeHelp = s.Choose(
        L"Automatic selects File/Folder symlink from the source.\n"
        L"Symlink: Windows symbolic link to a file or folder.\n"
        L"Junction: absolute local folder target.\n"
        L"Hardlink: second name for a file on the same volume.",
        L"Автоматически: тип ссылки определяется по файлу или папке.\n"
        L"Symlink: символическая ссылка на файл или папку.\n"
        L"Junction: абсолютная ссылка на локальную папку.\n"
        L"Hardlink: второе имя файла на том же диске.",
        L"אוטומטי: סוג הקישור נקבע לפי קובץ או תיקייה.\n"
        L"Symlink: קישור סמלי לקובץ או לתיקייה.\n"
        L"Junction: תיקייה מקומית בנתיב מוחלט.\n"
        L"Hardlink: שם נוסף לקובץ באותו כונן.");
    AddToolTip(hwnd, s.kind, linkTypeHelp);
    AddToolTip(hwnd, s.labelKind, linkTypeHelp);
    AddToolTip(hwnd, s.modeHint, linkTypeHelp);
    const wchar_t* targetHelp = s.Choose(
        L"Folder in which NEW links will be created.\n"
        L"Use a real local disk or accessible SMB directory.\n"
        L"Virtual folders inside WIM, ISO or WCX archives are not supported.\n"
        L"Existing files and links are never overwritten.",
        L"Папка, В КОТОРОЙ будут создаваться новые ссылки.\n"
        L"Выберите обычный каталог диска или доступную папку SMB.\n"
        L"Создание ссылок внутри WIM/ISO/WCX не поддерживается.\n"
        L"Существующие объекты не перезаписываются.",
        L"תיקייה שבה ייווצרו קישורים חדשים.\n"
        L"יש לבחור תיקיית דיסק או SMB נגישה.\n"
        L"אין ליצור קישורים בתוך WIM/ISO/WCX.\n"
        L"קבצים וקישורים קיימים לעולם לא יוחלפו.");
    // Avoid tooltip flicker on the editable destination path; label retains the same help.
    AddToolTip(hwnd, s.labelDestination, targetHelp);
    AddToolTip(hwnd, s.browse, s.Choose(
        L"Browse for the physical destination folder (disk or SMB).",
        L"Выбрать реальную папку назначения на диске или SMB.",
        L"בחירת תיקיית יעד אמיתית בדיסק או ב-SMB."));
    const wchar_t* sourceHelp = s.Choose(
        L"Selected objects from Total Commander's active source panel.\n"
        L"Sources remain unchanged; new links are created at the destination.",
        L"Выбранные объекты активной панели Total Commander.\n"
        L"Исходные файлы не изменяются — ссылки создаются в папке назначения.",
        L"פריטים שנבחרו בחלונית המקור של Total Commander.\n"
        L"קובצי המקור אינם משתנים; הקישורים נוצרים בתיקיית היעד.");
    AddToolTip(hwnd, s.sources, sourceHelp);
    // The live-updating path bar triggers tooltip re-entry; use the adjacent label instead.
    AddToolTip(hwnd, s.labelSources, sourceHelp);
    AddToolTip(hwnd, s.update, s.Choose(
        L"Check the official GitHub release.\n"
        L"Download & Install verifies SHA-256 and starts a native update "
        L"after Total Commander has closed; no PowerShell updater.",
        L"Проверить официальный выпуск GitHub.\n"
        L"«Download & Install» проверяет SHA-256 и устанавливает обновление "
        L"после закрытия Total Commander без PowerShell.",
        L"בדיקת גרסה רשמית ב-GitHub.\n"
        L"Download & Install מאמת SHA-256 ומתקין לאחר סגירת Total Commander, "
        L"ללא PowerShell."));
    AddToolTip(hwnd, s.preview, s.Choose(
        L"Preview of the path and name of the next link. No action has run yet.",
        L"Предпросмотр пути и имени новой ссылки. Создание ещё не запускалось.",
        L"תצוגה מקדימה של נתיב ושם הקישור החדש. עדיין לא בוצעה פעולה."));
    AddToolTip(hwnd, s.results, s.Choose(
        L"Planned link paths and the result of each operation.\n"
        L"OK = created; ERR = failed; WARN = warning.",
        L"Планируемые ссылки и результаты операций.\n"
        L"OK — создано; ERR — ошибка; WARN — предупреждение.",
        L"נתיבי הקישורים ותוצאות הפעולות.\n"
        L"OK — נוצר; ERR — שגיאה; WARN — אזהרה."));
        AddToolTip(hwnd,s.patternLabel,s.Choose(
        L"Link names: {Name}, {Stem}, {Ext}, {Index}. Example: {Stem}-link.{Ext}. Existing names are never replaced.",
        L"Шаблон имён: {Name}, {Stem}, {Ext}, {Index}. Пример: {Stem}-link.{Ext}. Существующие имена не заменяются.",
        L"שמות קישורים: {Name}, {Stem}, {Ext}, {Index}. לא מחליפים פריטים קיימים."));
    AddToolTip(hwnd,s.batchPreview,s.Choose(
        L"Inspect the complete batch before any changes. Conflicts block creation.",
        L"Проверить все ссылки перед созданием. Конфликты блокируют операцию.",
        L"הצג את כל הקישורים לפני יצירה; התנגשויות חוסמות."));
    AddToolTip(hwnd,s.linkManager,s.Choose(
        L"Find and inspect links, copy/open targets and repair confirmed broken local symlinks.",
        L"Поиск и проверка ссылок, копирование/открытие целей и исправление повреждённых локальных symlink.",
        L"חיפוש ובדיקת קישורים, העתקת יעדים ותיקון קישורים מקומיים שבורים."));
    AddToolTip(hwnd,s.undoLast,s.Choose(
        L"Undo only the links from the last batch that still have the same file identity.",
        L"Отменить только ссылки последней операции, идентичность которых не изменилась.",
        L"בטל רק קישורים מהפעולה האחרונה שלא השתנו."));
    AddToolTip(hwnd,s.compactToggle,s.Choose(
        L"Reduce visual spacing. Layout automatically adapts to screen size and DPI.",
        L"Уменьшить отступы. Интерфейс автоматически подстраивается под размер экрана и DPI.",
        L"הקטן רווחים; התאמה אוטומטית למסך ו-DPI."));
AddToolTip(hwnd, s.create, s.Choose(
        L"Create links at the destination (F5). Sources are not overwritten.",
        L"Создать ссылки в папке назначения (F5), не изменяя источники.",
        L"יצירת קישורים בתיקיית היעד (F5) ללא שינוי המקורות."));
    AddToolTip(hwnd, s.close, s.Choose(
        L"Close the Maker, or stop a running creation operation (Esc).",
        L"Закрыть окно или прервать выполняемую операцию (Esc).",
        L"סגירת החלון או עצירת הפעולה (Esc)."));

}

void FinishCreate(HWND hwnd, Ui& state, bool cancelled) {
    state.creating = false;
    KillTimer(hwnd, ID_ANIMATION_TIMER);
    InvalidateRect(hwnd, nullptr, FALSE);
    if (state.statusGlyph) InvalidateRect(state.statusGlyph, nullptr, TRUE);
    if (state.close) InvalidateRect(state.close, nullptr, FALSE);
    if (state.browse) EnableWindow(state.browse, TRUE);
    if (state.update) EnableWindow(state.update, TRUE);
    for(HWND button:{state.namePattern,state.batchPreview,state.linkManager,
                     state.compactToggle})
        if(button)EnableWindow(button,TRUE);
    if(state.undoLast)EnableWindow(state.undoLast,!state.undoItems.empty());
    AppendLine(state.results, L"------------------------------");
    AppendLine(state.results, state.Choose(L"Created: ", L"Создано: ", L"נוצרו: ") +
        std::to_wstring(state.completed) + state.Choose(L"; skipped / failed: ",
            L"; пропущено / ошибки: ", L"; דולגו / נכשלו: ") +
        std::to_wstring(state.failed));
    if (state.warnings)
        AppendLine(state.results, state.Choose(L"Warnings: ", L"Предупреждений: ",
            L"אזהרות: ") + std::to_wstring(state.warnings));
    if (cancelled) AppendLine(state.results, state.Choose(
        L"Stopped by user. Already created links remain in place.",
        L"Остановлено. Созданные ссылки сохранены.",
        L"נעצר על ידי המשתמש. קישורים שכבר נוצרו נשמרו."));
    if (state.privilegeError) AppendLine(state.results, state.Choose(
        L"Some symlinks failed with Windows error 1314. Enable Developer Mode or use an elevated account.",
        L"Ошибка 1314 только для Symlink: включите режим разработчика или запустите с нужными правами.",
        L"שגיאה 1314 בקישורים סמליים: יש להפעיל מצב מפתח או הרשאות מנהל."));
    SetWindowTextW(state.status, state.failed ? state.Choose(L"Completed with errors - see details above.",
        L"Завершено с ошибками — подробности выше.", L"הושלם עם שגיאות - ראה פירוט.") :
        (cancelled ? state.Choose(L"Stopped.", L"Остановлено.", L"נעצר.") :
         state.warnings ? state.Choose(L"Created, but with warnings - check results.",
             L"Ссылки созданы, но есть предупреждения.",
             L"הקישורים נוצרו עם אזהרות.") :
         state.Choose(L"Links created successfully.", L"Ссылки успешно созданы.", L"קישורים נוצרו בהצלחה.")));
    RefreshCreateSafety(state);
    SetWindowTextW(state.close, state.Choose(L"Esc  Close", L"Esc  Закрыть", L"Esc  סגור"));
    SetWindowTextW(hwnd, state.Choose(L"TC Symbolic Links v0.2.21 - Create Links",
        L"TC Symbolic Links v0.2.21 - создание ссылок", L"TC Symbolic Links v0.2.21 - יצירת קישורים"));
    UpdateModeHelp(state);
}

// Explanation-only diagnostics: no extra probes, no SSH and no policy changes.
void AppendFailureDetails(Ui& state, DWORD error, const std::wstring& source,
                          const std::wstring& link, bool sourceDirectory) {
    const auto lang = state.he ? tcs::help::Language::Hebrew :
        state.ru ? tcs::help::Language::Russian : tcs::help::Language::English;
    AppendLine(state.results, std::wstring(state.Choose(
        L"  Source: ", L"  Источник: ", L"  מקור: ")) + tcs::FullPath(source));
    AppendLine(state.results, std::wstring(state.Choose(
        L"  Link path: ", L"  Путь ссылки: ", L"  נתיב הקישור: ")) +
        tcs::FullPath(link));
    const bool junction = state.batchKind == tcs::LinkKind::Junction;
    const bool hardlink = state.batchKind == tcs::LinkKind::Hardlink;
    const wchar_t* kind = junction ? L"Junction" :
        hardlink ? L"Hardlink" :
        sourceDirectory ? L"Directory Symlink" : L"File Symlink";
    AppendLine(state.results, std::wstring(state.Choose(
        L"  Requested operation: ", L"  Тип операции: ",
        L"  סוג פעולה: ")) + kind);
    const tcs::help::Failure context{
        error, tcs::IsRemoteSmbPath(link), sourceDirectory,
        junction, hardlink, tcs::IsRemoteSmbPath(source)
    };
    for (const auto& line : tcs::help::Explain(context, lang))
        AppendLine(state.results, L"  " + line);
}

void ContinueCreate(HWND hwnd, Ui& state) {
    if (!state.creating) return;
    const auto total = state.batchRows.size();
    const auto end = (std::min)(total, state.nextItem + kCreateBatchSize);
    for (; state.nextItem < end; ++state.nextItem) {
        const auto& row=state.batchRows[state.nextItem];
        const auto& source=row.source;
        const auto& filename=row.name;
        if (filename.empty() || filename == L"." || filename == L"..") {
            ++state.failed;
            AppendLine(state.results, source + L": " + state.Choose(
                L"Invalid source (drive root, share root or empty path).",
                L"Некорректный источник (корень диска / пустой путь).",
                L"מקור לא תקין (תיקיית שורש או נתיב ריק)."));
            continue;
        }
        const auto& link=row.destination;
        const auto absoluteSource = tcs::FullPath(source);
        const auto absoluteLink = tcs::FullPath(link);
        // Never attempt to replace the selected source with a link to itself.
        // Windows reports ERROR_ALREADY_EXISTS (183) in this situation,
        // which is misleading without showing the two resulting paths.
        if (!absoluteSource.empty() && !absoluteLink.empty() &&
            _wcsicmp(absoluteSource.c_str(), absoluteLink.c_str()) == 0) {
            ++state.failed;
            AppendLine(state.results, std::wstring(L"ERR  ") + link + L": " +
                state.Choose(
                    L"Source and link path are identical. Choose a different destination folder.",
                    L"Путь исходного файла совпадает с путём ссылки. Выберите другую папку назначения.",
                    L"נתיב המקור והקישור זהים. יש לבחור תיקיית יעד אחרת."));
            const DWORD attrs = GetFileAttributesW(source.c_str());
            AppendFailureDetails(state, ERROR_ALREADY_EXISTS, source, link,
                attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
            continue;
        }
        const DWORD error = tcs::CreateLink(source, link, state.batchKind, state.batchRelative);
        const DWORD sourceAttrs = GetFileAttributesW(source.c_str());
        const bool sourceDirectory = sourceAttrs != INVALID_FILE_ATTRIBUTES &&
            (sourceAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const auto diagnostic = tcs::DiagnoseLinkOutcome(source, link,
            state.batchKind, sourceDirectory, error);
        if (error == ERROR_SUCCESS) {
            ++state.completed;
            AppendLine(state.results, L"OK   " + link + L"  ->  " + tcs::FullPath(source));
            tcs::manager::Identity createdIdentity;
            if(tcs::manager::Capture(link,createdIdentity))
                state.undoItems.push_back(std::move(createdIdentity));
            else {
                ++state.warnings;
                AppendLine(state.results,state.Choose(
                    L"WARN  Link created but Undo is unavailable for this entry (identity cannot be verified).",
                    L"WARN  Ссылка создана, но Undo недоступен: идентичность объекта не подтверждена.",
                    L"WARN  הקישור נוצר אך אין אפשרות לבטל: הזהות לא אומתה."));
            }
            if (diagnostic == tcs::LinkOutcomeDiagnostic::RemoteToLocalCreated)
                AppendLine(state.results, state.Choose(
                    L"NOTE: SMB link targets a local PC path. R2L traversal may be disabled; other computers may not have the same drive.",
                    L"ВНИМАНИЕ: SMB-ссылка ведёт на локальный диск ПК. Переход R2L может быть запрещён, а на других ПК такого диска может не быть.",
                    L"שים לב: קישור SMB מצביע לנתיב מקומי. מעבר R2L עלול להיות חסום."));
            else if (diagnostic == tcs::LinkOutcomeDiagnostic::RemoteToRemoteCreated)
                AppendLine(state.results, state.Choose(
                    L"NOTE: SMB link crosses shares or servers. Check R2R traversal and access to the target share on each client.",
                    L"ВНИМАНИЕ: ссылка ведёт на другую SMB-шару или сервер. Проверьте R2R и права доступа к целевой шаре.",
                    L"שים לב: הקישור עובר לשיתוף אחר. בדוק הרשאות ומדיניות R2R."));
            // A successful Win32 return is not proof that the endpoint exposes
            // the expected reparse data. Verify without modifying the new link.
            if (tcs::IsRemoteSmbPath(link) &&
                (state.batchKind == tcs::LinkKind::Automatic ||
                 state.batchKind == tcs::LinkKind::FileSymbolic ||
                 state.batchKind == tcs::LinkKind::DirectorySymbolic)) {
                const auto verified = tcs::InspectLink(link);
                const auto expected = sourceDirectory ? tcs::ReparseKind::SymbolicDirectory :
                    tcs::ReparseKind::SymbolicFile;
                if (verified.kind != expected) {
                    ++state.warnings;
                    AppendLine(state.results, state.Choose(
                        L"WARN  SMB link created, but its reparse type could not be confirmed.",
                        L"WARN  SMB-ссылка создана, но тип reparse point подтвердить не удалось.",
                        L"WARN  הקישור נוצר, אך לא ניתן לאמת את סוג הקישור."));
                }
                // Creation and traversal are independent on SMB: the client
                // may enforce R2R/R2L policy, ACLs or target availability.
                const DWORD traversal = tcs::CheckLinkTraversal(link);
                if (traversal != ERROR_SUCCESS) {
                    ++state.warnings;
                    AppendLine(state.results, std::wstring(state.Choose(
                        L"WARN  Link exists, but this client cannot open its target: ",
                        L"WARN  Ссылка создана, но Windows не может открыть цель: ",
                        L"WARN  הקישור נוצר אך לא ניתן לגשת ליעד: ")) +
                        tcs::WindowsErrorMessage(traversal) +
                        L" (" + std::to_wstring(traversal) + L")");
                    AppendLine(state.results, state.Choose(
                        L"CHECK: Creation and access are separate. Confirm the target exists, its ACLs, and the client's R2R/R2L/L2R symlink traversal policy (read-only: fsutil behavior query SymlinkEvaluation).",
                        L"ПРОВЕРИТЬ: создание и переход по ссылке — разные операции. Проверьте наличие цели, ACL и политику R2R/R2L/L2R на клиенте (только чтение: fsutil behavior query SymlinkEvaluation).",
                        L"בדיקה: יצירה וגישה לקישור הן פעולות שונות. יש לבדוק קיום היעד, הרשאות ומדיניות R2R/R2L/L2R (קריאה בלבד: fsutil behavior query SymlinkEvaluation)."));
                }
            }
        } else {
            ++state.failed;
            if (error == ERROR_PRIVILEGE_NOT_HELD) state.privilegeError = true;
            AppendLine(state.results, L"ERR  " + link + L": " +
                tcs::WindowsErrorMessage(error) + L" (" + std::to_wstring(error) + L")");
            AppendFailureDetails(state, error, source, link, sourceDirectory);
        }
    }
    if (state.nextItem == total) { FinishCreate(hwnd, state, false); return; }
    SetWindowTextW(hwnd, (std::wstring(L"TC Symbolic Links v0.2.21 - ") +
        std::to_wstring(state.nextItem) + L" / " + std::to_wstring(total)).c_str());
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

void BeginCreate(HWND hwnd, Ui& state) {
    if (state.creating) return;
    const auto destination = tcs::FullPath(GetText(state.destination));
    RefreshCreateSafety(state);
    if (state.unsafeDestination) {
        MessageBeep(MB_ICONWARNING);
        SetFocus(state.destination);
        return;
    }
    if (!tcs::IsExistingDirectory(destination)) {
        MessageBoxW(hwnd, state.Choose(L"Choose an existing destination folder.",
            L"Укажите существующую папку назначения.", L"בחר תיקיית יעד קיימת."),
            L"TC Symbolic Links", MB_OK | MB_ICONWARNING);
        SetFocus(state.destination);
        return;
    }
    const auto sel = SendMessageW(state.kind, CB_GETCURSEL, 0, 0);
    state.batchKind = (sel >= 0 && sel <= 4) ? static_cast<tcs::LinkKind>(sel) :
        tcs::LinkKind::Automatic;
    const auto scan = tcs::InspectSelection(state.selected);
    const bool folderMode = state.batchKind == tcs::LinkKind::DirectorySymbolic ||
                            state.batchKind == tcs::LinkKind::Junction;
    const bool fileMode = state.batchKind == tcs::LinkKind::FileSymbolic ||
                          state.batchKind == tcs::LinkKind::Hardlink;
    std::wstring warning;
    if (scan.unavailable) {
        warning = state.Choose(
            L"Some selected items cannot be accessed. No links were created.",
            L"Некоторые выбранные объекты недоступны. Ссылки не созданы.",
            L"חלק מהפריטים שנבחרו אינם נגישים. לא נוצרו קישורים.");
        warning += L"\r\n" + scan.firstUnavailable;
    } else if (folderMode && scan.files) {
        warning = state.Choose(
            L"This link type requires folders only. Choose folders or switch to Automatic / File Symlink. No links were created.",
            L"Этот тип ссылок предназначен только для папок. Выберите папки либо режим Automatic / File Symlink. Ссылки не созданы.",
            L"סוג קישור זה מיועד לתיקיות בלבד. יש לבחור תיקיות או לשנות את הסוג.");
    } else if (fileMode && scan.folders) {
        warning = state.Choose(
            L"This link type requires files only. Choose files or switch to Automatic / Directory Symlink. No links were created.",
            L"Этот тип ссылок предназначен только для файлов. Выберите файлы либо режим Automatic / Directory Symlink. Ссылки не созданы.",
            L"סוג קישור זה מיועד לקבצים בלבד. יש לבחור קבצים או לשנות את הסוג.");
    }
    if (!warning.empty()) {
        const std::wstring counts = state.Choose(L"Files: ", L"Файлов: ", L"קבצים: ") +
            std::to_wstring(scan.files) +
            state.Choose(L"; folders: ", L"; папок: ", L"; תיקיות: ") +
            std::to_wstring(scan.folders);
        AppendLine(state.results, L"WARN  " + warning + L"\r\n" + counts);
        SetWindowTextW(state.status, state.Choose(L"Preflight rejected selection.",
            L"Проверка выборки: операция отменена.", L"בדיקת הבחירה נכשלה."));
        InvalidateRect(hwnd, nullptr, FALSE);
        MessageBoxW(hwnd, (warning + L"\r\n" + counts).c_str(),
            L"TC Symbolic Links", MB_OK | MB_ICONWARNING);
        return;
    }
    // Full read-only batch preflight: exact names, duplicate names, existing
    // entries and mismatched file/directory types are shown in one table.
    state.batchRows=BuildBatchPlan(state,state.selected.size());
    const bool allReady=tcs::batch::AllReady(state.batchRows);
    // Ordinary single-item F5 keeps the established Total Commander workflow:
    // no extra modal confirmation when the preflight is clean. Multi-selection,
    // conflicting names and templates get an explicit full batch preview.
    const bool needsConfirmation=state.batchRows.size()>1 || !allReady ||
        GetText(state.namePattern)!=L"{Name}";
    if(needsConfirmation &&
       !tcs::batch::ConfirmPreview(hwnd,state.batchRows,allReady)){
        if(!allReady)SetWindowTextW(state.status,state.Choose(
            L"Batch conflicts found. No links created.",
            L"Найдены конфликты. Ссылки не созданы.",
            L"נמצאו התנגשויות. לא נוצרו קישורים."));
        return;
    }
    state.undoItems.clear();
    if(state.undoLast)EnableWindow(state.undoLast,FALSE);
    // Automatic mode intentionally accepts mixed file/folder selections.
    state.batchRelative = sel < 3 && SendMessageW(state.relative, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state.batchDestination = destination;
    state.nextItem = 0;
    state.completed = 0;
    state.failed = 0;
    state.warnings = 0;
    state.privilegeError = false;
    state.creating = true;
    state.animationFrame = 0;
    SetTimer(hwnd, ID_ANIMATION_TIMER, 80, nullptr);
    if (state.close) InvalidateRect(state.close, nullptr, FALSE);
    if (state.statusGlyph) InvalidateRect(state.statusGlyph, nullptr, TRUE);
    state.hasRun = true;
    SetWindowTextW(state.results, L"");
    EnableWindow(state.create, FALSE);
    if (state.browse) EnableWindow(state.browse, FALSE);
    if (state.update) EnableWindow(state.update, FALSE);
    for(HWND button:{state.namePattern,state.batchPreview,state.linkManager,
                     state.compactToggle,state.undoLast})
        if(button)EnableWindow(button,FALSE);
    EnableWindow(state.relative, FALSE);
    SetWindowTextW(state.status, state.Choose(L"Creating links...",
        L"Создание ссылок...", L"יוצר קישורים..."));
    SetWindowTextW(state.close, state.Choose(L"Esc  Stop", L"Esc  Остановить", L"Esc  עצור"));
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_TCS_TEST_TOOLTIPS:
            return state ? state->tooltipCount : 0;
        case WM_NCCREATE: {
            auto* data = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(data->lpCreateParams));
            // DefWindowProc stores lpWindowName as the native caption.
            // Returning TRUE directly left our versioned window title blank.
            return DefWindowProcW(hwnd,msg,wParam,lParam);
        }
        case WM_CREATE:
            if (state) {
                state->dpi = GetDpiForWindow(hwnd);
                state->compact = LoadCompactMode();
                state->backgroundBrush = CreateSolidBrush(kCanvas);
                state->cardBrush = CreateSolidBrush(kSurface);
                state->fieldBrush = CreateSolidBrush(tcs::visual::Field);
                // Windows 11: match the NI-style navy banner in the title bar.
                // DWM rejects unsupported attributes on older Windows safely.
                const COLORREF caption = tcs::visual::Navy;
                const COLORREF captionText = tcs::visual::White;
                DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
                DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
                SHFILEINFOW folderInfo{};
                if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY,
                        &folderInfo, sizeof(folderInfo),
                        SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON))
                    state->folderIcon = folderInfo.hIcon;
                CreateControls(hwnd, *state);
            }
            return 0;
        case WM_SIZE:
            if (state && state->sources) Layout(hwnd, *state);
            return 0;
        case WM_GETMINMAXINFO: {
            const UINT dpi = state ? state->dpi : 96;
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT usable{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &usable, 0);
            info->ptMinTrackSize.x = (std::min)(MulDiv(760, dpi, 96), static_cast<int>(usable.right - usable.left) - 24);
            info->ptMinTrackSize.y = (std::min)(MulDiv(540, dpi, 96), static_cast<int>(usable.bottom - usable.top) - 24);
            return 0;
        }
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                const auto* rect = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                    rect->right - rect->left, rect->bottom - rect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateFont(*state);
                Layout(hwnd, *state);
            }
            return 0;
        case WM_ERASEBKGND:
            // The whole surface is double-buffered in WM_PAINT, including
            // anti-aliased controls, so erasing separately causes flicker.
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC screenDc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int cw = (std::max)(1L, client.right - client.left);
            const int ch = (std::max)(1L, client.bottom - client.top);
            HDC bufferDc = CreateCompatibleDC(screenDc);
            HBITMAP buffer = bufferDc ?
                CreateCompatibleBitmap(screenDc, cw, ch) : nullptr;
            HGDIOBJ oldBitmap = buffer ? SelectObject(bufferDc, buffer) : nullptr;
            HDC dc = buffer && oldBitmap ? bufferDc : screenDc;
            HBRUSH canvas = CreateSolidBrush(kCanvas);
            FillRect(dc, &client, canvas);
            DeleteObject(canvas);
            if (state) {
                const auto layout=tcs::maker::density::Resolve(
                    state->compact,cw,ch,static_cast<int>(state->dpi));
                const int margin=Dp(*state,layout.margin);
                const int gap=Dp(*state,layout.gap);
                const std::wstring subtitle =
                    std::to_wstring(state->selected.size()) + L" " + state->Choose(
                    L"selected  |  Existing items are never overwritten",
                    L"выбрано  |  Существующие объекты не перезаписываются",
                    L"נבחרו  |  קבצים קיימים לעולם לא יוחלפו");
                const RECT banner{margin, Dp(*state,layout.bannerTop),
                                  client.right - margin,
                                  Dp(*state,layout.bannerBottom)};
                const HICON logo = reinterpret_cast<HICON>(
                    GetClassLongPtrW(hwnd, GCLP_HICON));
                tcs::visual::Banner(dc, banner, state->dpi, logo,
                    L"TC Symbolic Links v0.2.21", subtitle,
                    state->he,layout.compact);
                const std::wstring status = L"●  " + GetText(state->status);
                RECT actionRect{};
                int actionLeft = -1;
                if (state->create && GetWindowRect(state->create, &actionRect)) {
                    MapWindowPoints(nullptr, hwnd,
                        reinterpret_cast<POINT*>(&actionRect), 2);
                    actionLeft = actionRect.left;
                }
                tcs::visual::FormPanels(dc, client, state->dpi,
                    margin, gap, Dp(*state,layout.cardTop),
                    client.bottom - Dp(*state,layout.cardBottomInset),
                    Dp(*state,layout.toolsTop),
                    Dp(*state,layout.toolsHeight),
                    state->hasRun, state->failed != 0,
                    state->unsafeDestination, state->creating,
                    status, state->he, actionLeft);

                // Rounded Ni-style edit shells behind the real Win32 text
                // controls. Native EDIT owns caret, selection, IME and Unicode.
                const auto drawField = [&](HWND edit) {
                    if (!edit) return;
                    RECT editRect{};
                    if (!GetWindowRect(edit, &editRect)) return;
                    MapWindowPoints(nullptr, hwnd,
                        reinterpret_cast<POINT*>(&editRect), 2);
                    InflateRect(&editRect, Dp(*state, 10), Dp(*state, 6));
                    tcs::visual::InputField(dc, editRect, state->dpi,
                                            GetFocus() == edit);
                };
                drawField(state->sourcePath);
                drawField(state->destination);

                // Thin animated progress rail at the top of the status card.
                // Keeps results visible, without taking space from the panes.
                const int progressTop = client.bottom - Dp(*state, 68);
                RECT track{margin + Dp(*state, 12), progressTop,
                           client.right - margin - Dp(*state, 12),
                           progressTop + Dp(*state, 3)};
                HBRUSH trackBrush = CreateSolidBrush(kLine);
                FillRect(dc, &track, trackBrush);
                DeleteObject(trackBrush);
                if (state->creating || state->hasRun) {
                    const auto total = (std::max)(std::size_t{1}, state->batchRows.empty()?
                    state->selected.size():state->batchRows.size());
                    const auto done = (std::min)(total,
                        static_cast<std::size_t>(state->completed + state->failed));
                    const int width = track.right - track.left;
                    const int filled = (std::min)(width,
                        static_cast<int>(static_cast<unsigned long long>(width) * done / total));
                    RECT bar{track.left, track.top,
                             track.left + filled, track.bottom};
                    HBRUSH progress = CreateSolidBrush(state->creating ? kAccent :
                        state->failed ? kDanger : kPositive);
                    FillRect(dc, &bar, progress);
                    DeleteObject(progress);
                    if (state->creating && filled > 0) {
                        const int stripe = Dp(*state, 27);
                        const int offset = static_cast<int>(
                            (state->animationFrame * Dp(*state, 12)) %
                            static_cast<unsigned>((std::max)(1, filled + stripe)));
                        RECT shine{track.left + (std::max)(0, offset - stripe),
                            track.top, track.left + (std::min)(filled, offset),
                            track.bottom};
                        if (shine.right > shine.left) {
                            HBRUSH light = CreateSolidBrush(RGB(140, 203, 251));
                            FillRect(dc, &shine, light);
                            DeleteObject(light);
                        }
                    }
                }
            }
            if (dc == bufferDc)
                BitBlt(screenDc, 0, 0, cw, ch, bufferDc, 0, 0, SRCCOPY);
            if (oldBitmap) SelectObject(bufferDc, oldBitmap);
            if (buffer) DeleteObject(buffer);
            if (bufferDc) DeleteDC(bufferDc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DRAWITEM:
            if (state && lParam) {
                const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (item->CtlID == ID_CREATE || item->CtlID == ID_CLOSE ||
                    item->CtlID == ID_BROWSE || item->CtlID == ID_UPDATE) {
                    PaintAction(*state, *item);
                    return TRUE;
                }
                if (item->CtlID == ID_SOURCE_GLYPH ||
                    item->CtlID == ID_TARGET_GLYPH ||
                    item->CtlID == ID_STATUS_GLYPH) {
                    PaintPanelSymbol(*state, *item);
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLOREDIT:
            if (state && state->fieldBrush &&
                reinterpret_cast<HWND>(lParam) == state->destination) {
                HDC editDc = reinterpret_cast<HDC>(wParam);
                SetBkMode(editDc, OPAQUE);
                SetBkColor(editDc, tcs::visual::Field);
                SetTextColor(editDc, tcs::visual::Ink);
                return reinterpret_cast<LRESULT>(state->fieldBrush);
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (state && state->backgroundBrush) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                const HWND ctl = reinterpret_cast<HWND>(lParam);
                SetTextColor(dc, ctl == state->status || ctl == state->preview ?
                    (state->unsafeDestination || state->failed ? kDanger :
                     state->hasRun ? kPositive : kSecondary) :
                    (ctl == state->subtitle || ctl == state->modeHint ||
                     ctl == state->relativeHint ? kSecondary : kInk));
                if ((ctl == state->sourcePath || ctl == state->results ||
                     ctl == state->modeHint || ctl == state->relativeHint ||
                     ctl == state->preview) && state->fieldBrush) {
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, tcs::visual::Field);
                    SetTextColor(dc, tcs::visual::Ink);
                    return reinterpret_cast<LRESULT>(state->fieldBrush);
                }
                if ((ctl == state->labelSources || ctl == state->labelResults ||
                     ctl == state->labelKind) && state->cardBrush)
                    return reinterpret_cast<LRESULT>(state->cardBrush);
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLORBTN:
            if (state && state->cardBrush)
                return reinterpret_cast<LRESULT>(state->cardBrush);
            break;
        case WM_TIMER:
            if (state && wParam == ID_ANIMATION_TIMER && state->creating) {
                state->animationFrame = (state->animationFrame + 1) % 60;
                RECT area{};
                GetClientRect(hwnd, &area);
                area.top = area.bottom - Dp(*state, 42);
                area.bottom = area.top + Dp(*state, 8);
                InvalidateRect(hwnd, &area, FALSE);
                return 0;
            }
            break;
        case WM_TCS_CREATE_NEXT:
            if (state) ContinueCreate(hwnd, *state);
            return 0;
        case WM_COMMAND:
            if(state && LOWORD(wParam)==ID_NAME_PATTERN && HIWORD(wParam)==EN_CHANGE){
                if(!state->creating)UpdatePreview(*state);
                return 0;
            }
            if(state && LOWORD(wParam)==ID_NAME_PATTERN && HIWORD(wParam)==EN_KILLFOCUS){
                SaveMakerPreferences(*state);
                return 0;
            }
            if(state && LOWORD(wParam)==ID_BATCH_PREVIEW &&
               HIWORD(wParam)==BN_CLICKED){
                if(!state->creating){
                    const auto plan=BuildBatchPlan(*state,state->selected.size());
                    tcs::batch::ConfirmPreview(hwnd,plan,false);
                }
                return 0;
            }
            if(state && LOWORD(wParam)==ID_LINK_MANAGER &&
               HIWORD(wParam)==BN_CLICKED){
                if(!state->creating){
                    tcs::manager::ShowLinkManager(hwnd,GetText(state->destination),
                                                  state->selected);
                    UpdatePreview(*state);
                }
                return 0;
            }
            if(state && LOWORD(wParam)==ID_UNDO_LAST && HIWORD(wParam)==BN_CLICKED){
                if(!state->creating&&!state->undoItems.empty()){
                    if(MessageBoxW(hwnd,state->Choose(
                        L"Undo links from the last batch only if they are unchanged?\nOriginal files and folders will not be removed.",
                        L"Удалить ссылки последней операции, только если они не изменились?\nИсходные файлы и папки не будут затронуты.",
                        L"לבטל רק קישורים שלא השתנו מהפעולה האחרונה?\nקבצי המקור לא יימחקו."),
                        L"TC Symbolic Links - Undo Last Batch",
                        MB_YESNO|MB_ICONQUESTION)==IDYES){
                        const auto summary=tcs::manager::Undo(state->undoItems);
                        state->undoItems.clear();
                        EnableWindow(state->undoLast,FALSE);
                        AppendLine(state->results,std::wstring(L"UNDO: ")+
                            std::to_wstring(summary.deleted)+
                            L" removed, "+std::to_wstring(summary.skipped)+
                            L" skipped (modified/locked).");
                        if(summary.firstError!=ERROR_SUCCESS)
                            AppendLine(state->results,L"UNDO Win32: "+
                                std::to_wstring(summary.firstError));
                    }
                }
                return 0;
            }
            if(state && LOWORD(wParam)==ID_COMPACT && HIWORD(wParam)==BN_CLICKED){
                state->compact=SendMessageW(state->compactToggle,
                    BM_GETCHECK,0,0)==BST_CHECKED;
                Layout(hwnd,*state);
                InvalidateRect(hwnd,nullptr,FALSE);
                UpdateWindow(hwnd);
                SaveMakerPreferences(*state);
                return 0;
            }
            if (state && LOWORD(wParam) == ID_UPDATE && HIWORD(wParam) == BN_CLICKED) {
                if (!state->creating)
                    tcs::update::StartUpdateManager(nullptr, false, hwnd);
            } else if (state && LOWORD(wParam) == ID_BROWSE && HIWORD(wParam) == BN_CLICKED) {
                ChooseDestination(hwnd, *state);
            } else if (state && LOWORD(wParam) == ID_KIND && HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateModeHelp(*state);
            } else if (state && LOWORD(wParam) == ID_DEST && HIWORD(wParam) == EN_CHANGE) {
                UpdatePreview(*state);
            } else if (state && LOWORD(wParam) == ID_DEST &&
                (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)) {
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (state && LOWORD(wParam) == ID_CREATE &&
                       (HIWORD(wParam) == BN_CLICKED || lParam == 0)) {
                BeginCreate(hwnd, *state);
            } else if (LOWORD(wParam) == ID_CLOSE &&
                       (HIWORD(wParam) == BN_CLICKED || lParam == 0)) {
                if (state && state->creating) FinishCreate(hwnd, *state, true);
                else { if(state)SaveMakerPreferences(*state); DestroyWindow(hwnd); }
            }
            return 0;
        case WM_CLOSE:
            if (state && state->creating) { FinishCreate(hwnd, *state, true); return 0; }
            if(state)SaveMakerPreferences(*state);
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete state;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::vector<std::wstring> ReadSelectedList(const std::wstring& fileName) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows must use UTF-16 wchar_t");
    HANDLE h = CreateFileW(fileName.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 8 * 1024 * 1024 || size.QuadPart < 2) {
        CloseHandle(h);
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != bytes.size()) return {};
    const auto raw = tcs::ParseTcWideList(bytes);
    std::vector<std::wstring> selected;
    selected.reserve(raw.size());
    for (const auto& value : raw) selected.emplace_back(value.begin(), value.end());
    return selected;
}
}

int tcs::RunMakerFromArguments(HINSTANCE instance, std::wstring_view arguments, int show) {
    (void)instance; // rundll32 passes the EXE instance, never the WDX resource module.
    std::wstring listFile;
    std::wstring destination;
    int requestedMode = 0; // 0=automatic, 1=file, 2=directory
    int count = 0;
    // rundll32 passes only arguments after the exported entry point, not an EXE path.
    // Add a synthetic argv[0] so CommandLineToArgvW can safely process quoting.
    std::wstring withProgram = L"tcsplugin.exe ";
    withProgram.append(arguments);
    LPWSTR* commandLine = CommandLineToArgvW(withProgram.c_str(), &count);
    if (commandLine) {
        for (int i = 1; i < count; ++i) {
            const std::wstring_view argument = commandLine[i];
            if (argument == L"--list" && i + 1 < count) listFile = commandLine[++i];
            else if (argument == L"--dest" && i + 1 < count) destination = commandLine[++i];
            else if (argument == L"--mode" && i + 1 < count) {
                const std::wstring_view mode = commandLine[++i];
                if (mode == L"file") requestedMode = 1;
                else if (mode == L"directory") requestedMode = 2;
                else requestedMode = 0;
            }
        }
        LocalFree(commandLine);
    }
    if (listFile.empty() || destination.empty()) {
        MessageBoxW(nullptr,
            L"Use the installed TC Symbolic Links button in Total Commander.\n"
            L"The button calls this WDX through Windows rundll32.exe.",
            L"TC Symbolic Links", MB_ICONINFORMATION);
        return 2;
    }
    std::vector<std::wstring> selected;
    try { selected = ReadSelectedList(listFile); }
    catch (const std::exception&) {
        MessageBoxW(nullptr, L"Cannot read the Total Commander %WL file list. "
            L"It must be UTF-16LE with a BOM and contain no malformed UTF-16.",
            L"TC Symbolic Links", MB_ICONERROR);
        return 3;
    }
    if (selected.empty()) {
        MessageBoxW(nullptr, L"No selected files were found in the TC %WL list.",
            L"TC Symbolic Links", MB_ICONINFORMATION);
        return 4;
    }
    auto* state = new Ui;
    const LANGID uiLanguage = GetUserDefaultUILanguage();
    state->ru = PRIMARYLANGID(uiLanguage) == LANG_RUSSIAN;
    state->he = PRIMARYLANGID(uiLanguage) == LANG_HEBREW;
    state->selected = std::move(selected);
    state->initialDestination = std::move(destination);
    state->initialMode = requestedMode;
    // The Maker also uses the Win32 tooltip and button common-control classes.
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&common)) { delete state; return 5; }
    GdiPlusScope graphics;
    if (!graphics.Ready()) {
        MessageBoxW(nullptr, L"Windows GDI+ initialization failed. "
            L"The native Total Commander UI cannot be drawn.",
            L"TC Symbolic Links", MB_OK | MB_ICONERROR);
        delete state;
        return 7;
    }
    // IMPORTANT: rundll32 passes the host EXE as the entrypoint HINSTANCE.
    // Icons 101/102/103 are embedded in the WDX, NOT in rundll32.exe.
    // Looking them up in the host resulted in the generic Windows taskbar,
    // title-bar and banner icons reported in Total Commander 11.58 screenshots.
    HMODULE pluginModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&tcs::RunMakerFromArguments),
            &pluginModule) || !pluginModule) {
        delete state;
        return 5;
    }
    // Only our dedicated rundll32 host receives this taskbar identity.
    // Never change Total Commander's AppUserModelID if hosted in its process.
    wchar_t hostName[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, hostName, MAX_PATH)) {
        const wchar_t* name = wcsrchr(hostName, L'\\');
        if (name && _wcsicmp(name + 1, L"rundll32.exe") == 0)
            SetCurrentProcessExplicitAppUserModelID(L"TC.SymbolicLinks.LinkMaker");
    }
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = pluginModule;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(pluginModule, MAKEINTRESOURCEW(101),
        IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    cls.hIconSm = static_cast<HICON>(LoadImageW(pluginModule, MAKEINTRESOURCEW(101),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (!cls.hIcon) cls.hIcon = LoadIconW(pluginModule, MAKEINTRESOURCEW(101));
    if (!cls.hIconSm) cls.hIconSm = cls.hIcon;
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = kClassName;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        delete state;
        return 5;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int winWidth = (std::min)(MulDiv(960, static_cast<int>(GetDpiForSystem()), 96),
                                    static_cast<int>(work.right - work.left) - 24);
    const int winHeight = (std::min)(MulDiv(640, static_cast<int>(GetDpiForSystem()), 96),
                                     static_cast<int>(work.bottom - work.top) - 24);
    // CW_USEDEFAULT can place the bottom F5/Esc strip under the taskbar on
    // compact desktops. Center inside the actual usable work area instead.
    const int centeredX = work.left + (work.right - work.left - winWidth) / 2;
    const int centeredY = work.top + (work.bottom - work.top - winHeight) / 2;
    HWND window = CreateWindowExW(0, kClassName,
        state->he ? L"TC Symbolic Links v0.2.21 — יצירת קישורים" :
        (state->ru ? L"TC Symbolic Links v0.2.21 — создание ссылок" :
                     L"TC Symbolic Links v0.2.21 — Create Links"), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        centeredX, centeredY, winWidth, winHeight, nullptr, nullptr, pluginModule, state);
    if (!window) {
        // WM_NCDESTROY may already have freed state during failed creation.
        return 6;
    }
    // Explicit per-window icons matter to the Windows taskbar in rundll32
    // hosts, even if class icons were registered before the Maker opened.
    if (cls.hIcon) SendMessageW(window, WM_SETICON, ICON_BIG,
                               reinterpret_cast<LPARAM>(cls.hIcon));
    if (cls.hIconSm) SendMessageW(window, WM_SETICON, ICON_SMALL,
                                 reinterpret_cast<LPARAM>(cls.hIconSm));
    ShowWindow(window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    UpdateWindow(window);
    // The exact same F5 and Esc shortcuts appear on the bottom buttons.
    ACCEL mappings[] = {
        {FVIRTKEY, VK_F5, static_cast<WORD>(ID_CREATE)},
        {FVIRTKEY, VK_ESCAPE, static_cast<WORD>(ID_CLOSE)}
    };
    HACCEL shortcuts = CreateAcceleratorTableW(mappings, 2);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (shortcuts && TranslateAcceleratorW(window, shortcuts, &msg)) continue;
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (shortcuts) DestroyAcceleratorTable(shortcuts);
    return static_cast<int>(msg.wParam);
}
