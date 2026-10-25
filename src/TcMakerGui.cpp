// Native HiDPI Unicode link maker hosted inside the WDX DLL.
// One window for file/directory symlinks, directory junctions and hardlinks.
#include "LinkCore.h"
#include "ListParser.h"
#include "TcMakerGui.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
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
constexpr UINT WM_TCS_CREATE_NEXT = WM_APP + 41;
constexpr std::size_t kCreateBatchSize = 12;
constexpr COLORREF kBackground = RGB(248, 250, 253);
constexpr COLORREF kInk = RGB(35, 48, 65);
constexpr COLORREF kSecondary = RGB(86, 103, 126);
constexpr COLORREF kAccent = RGB(29, 104, 186);

struct Ui {
    bool ru = false;
    bool he = false;
    UINT dpi = 96;
    HFONT font = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontLabel = nullptr;
    HFONT fontSmall = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND labelSources = nullptr;
    HWND sources = nullptr;
    HWND sourcePath = nullptr;
    HWND labelDestination = nullptr;
    HWND destination = nullptr;
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
    std::vector<std::wstring> selected;
    std::wstring initialDestination;
    bool creating = false;
    bool privilegeError = false;
    bool hasRun = false;
    std::size_t nextItem = 0;
    int completed = 0;
    int failed = 0;
    tcs::LinkKind batchKind = tcs::LinkKind::Automatic;
    bool batchRelative = false;
    std::wstring batchDestination;
    ~Ui() {
        for (HFONT f : {font, fontTitle, fontLabel, fontSmall}) if (f) DeleteObject(f);
        if (backgroundBrush) DeleteObject(backgroundBrush);
    }
    const wchar_t* Choose(const wchar_t* english, const wchar_t* russian,
                          const wchar_t* hebrew = nullptr) const {
        if (he && hebrew) return hebrew;
        return ru ? russian : english;
    }
};

int Dp(const Ui& state, int n) { return MulDiv(n, static_cast<int>(state.dpi), 96); }

std::wstring GetText(HWND control) {
    const auto n = GetWindowTextLengthW(control);
    if (n <= 0) return {};
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    const int actual = GetWindowTextW(control, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(actual));
    return s;
}

void AppendLine(HWND edit, const std::wstring& line) {
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    std::wstring text = line + L"\r\n";
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
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
    state.font = MakeFont(state.dpi, 9, FW_NORMAL);
    state.fontTitle = MakeFont(state.dpi, 10, FW_SEMIBOLD);
    state.fontLabel = MakeFont(state.dpi, 9, FW_SEMIBOLD);
    state.fontSmall = MakeFont(state.dpi, 9, FW_NORMAL);
    if (!state.font) state.font = oldFont;
    if (!state.fontTitle) state.fontTitle = oldTitle;
    if (!state.fontLabel) state.fontLabel = oldLabel;
    if (!state.fontSmall) state.fontSmall = oldSmall;
    for (HWND item : {state.sources, state.sourcePath, state.destination, state.kind,
                      state.relative, state.results, state.create, state.close}) {
        if (item && state.font) SendMessageW(item, WM_SETFONT,
                                             reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    if (state.title && state.fontTitle) SendMessageW(state.title, WM_SETFONT,
                                                     reinterpret_cast<WPARAM>(state.fontTitle), TRUE);
    for (HWND item : {state.labelSources, state.labelDestination, state.labelKind,
                      state.labelResults}) {
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
}

HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* title,
                DWORD style, int id, DWORD extended = 0) {
    return CreateWindowExW(extended, cls, title, style | WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

void UpdatePlannedOutput(Ui& s) {
    // Right file panel previews destinations until a batch starts. Do not
    // replace actual success/error output when paths change after creation.
    if (!s.results || s.creating || s.hasRun) return;
    const std::wstring dir = tcs::FullPath(GetText(s.destination));
    std::wstring lines;
    const auto count = (std::min)(s.selected.size(), std::size_t{80});
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = tcs::BaseName(s.selected[i]);
        if (!name.empty()) lines += L"  " + name + L"  ->  " + tcs::Join(dir, name) + L"\r\n";
    }
    if (count < s.selected.size()) lines += s.Choose(
        L"...more selected items", L"...остальные выделенные элементы", L"...פריטים נוספים");
    SetWindowTextW(s.results, lines.c_str());
}

void UpdatePreview(Ui& s) {
    if (!s.preview || !s.destination || s.selected.empty()) return;
    const auto name = tcs::BaseName(s.selected.front());
    const auto destination = tcs::FullPath(GetText(s.destination));
    std::wstring value = s.Choose(L"New link: ", L"Новая ссылка: ", L"קישור חדש: ");
    value += name.empty() || destination.empty() ? s.Choose(L"(choose source and destination)",
            L"(укажите источник и назначение)", L"(בחר מקור ויעד)") :
        tcs::Join(destination, name);
    if (s.selected.size() > 1) value += L"  (+" + std::to_wstring(s.selected.size()-1) + L")";
    SetWindowTextW(s.preview, value.c_str());
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

void Layout(HWND hwnd, Ui& s) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int margin = Dp(s, 10);
    const int space = Dp(s, 8);
    const int usable = (std::max)(Dp(s, 320), width - 2 * margin);
    const int leftWidth = (usable - space) / 2;
    const int rightX = margin + leftWidth + space;
    const int rightWidth = usable - leftWidth - space;

    auto place = [&](HWND child, int x, int y, int w, int h) {
        if (child) MoveWindow(child, x, y, (std::max)(1, w),
                              (std::max)(1, h), TRUE);
    };
    // Compact tool strip above two Commander-style file panels.
    place(s.title, margin, Dp(s, 6), Dp(s, 220), Dp(s, 22));
    place(s.subtitle, margin + Dp(s, 230), Dp(s, 7),
          (std::max)(Dp(s, 80), usable - Dp(s, 230)), Dp(s, 20));
    const int toolsY = Dp(s, 37);
    const int labelWidth = Dp(s, 75);
    const int kindWidth = (std::min)(Dp(s, 360),
          (std::max)(Dp(s, 225), usable / 2 - labelWidth - Dp(s, 5)));
    place(s.labelKind, margin, toolsY + Dp(s, 5), labelWidth, Dp(s, 19));
    place(s.kind, margin + labelWidth, toolsY, kindWidth, Dp(s, 300));
    const int checkX = margin + labelWidth + kindWidth + space;
    place(s.relative, checkX, toolsY + Dp(s, 3),
          (std::max)(Dp(s, 105), width - margin - checkX), Dp(s, 24));

    const int headingY = Dp(s, 74);
    place(s.labelSources, margin, headingY, leftWidth, Dp(s, 19));
    place(s.labelResults, rightX, headingY, rightWidth, Dp(s, 19));
    const int pathY = Dp(s, 95);
    const int pathH = Dp(s, 27);
    place(s.sourcePath, margin, pathY, leftWidth, pathH);
    place(s.destination, rightX, pathY, rightWidth, pathH);

    const int paneY = pathY + pathH + Dp(s, 6);
    const bool compact = height < Dp(s, 540);
    const int paneBottom = height - Dp(s, compact ? 129 : 147);
    const int paneHeight = (std::max)(Dp(s, 72), paneBottom - paneY);
    place(s.sources, margin, paneY, leftWidth, paneHeight);
    place(s.results, rightX, paneY, rightWidth, paneHeight);

    // Help is always available, but doesn't steal file panel space.
    const int infoY = paneBottom + Dp(s, 6);
    place(s.modeHint, margin + Dp(s, 3), infoY, usable - Dp(s, 6),
          Dp(s, compact ? 28 : 35));
    place(s.relativeHint, margin + Dp(s, 3),
          infoY + Dp(s, compact ? 30 : 38), usable - Dp(s, 6),
          Dp(s, compact ? 28 : 35));
    place(s.preview, margin + Dp(s, 3), height - Dp(s, 60),
          usable - Dp(s, 6), Dp(s, 20));

    // Function-key buttons just like Total Commander's bottom command strip.
    const int actionY = height - Dp(s, 35);
    const int createWidth = Dp(s, 120);
    const int closeWidth = Dp(s, 103);
    const int actionH = Dp(s, 29);
    place(s.create, width - margin - closeWidth - space - createWidth,
          actionY, createWidth, actionH);
    place(s.close, width - margin - closeWidth,
          actionY, closeWidth, actionH);
    place(s.status, margin, actionY + Dp(s, 4),
          (std::max)(Dp(s, 80), usable - createWidth - closeWidth - 2 * space),
          Dp(s, 22));
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CreateControls(HWND hwnd, Ui& s) {
    const DWORD captionEx = s.he ? WS_EX_RTLREADING : 0;
    const DWORD alignCaption = s.he ? SS_RIGHT : SS_LEFT;
    s.title = AddControl(hwnd, L"STATIC", L"TC Symbolic Links",
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
    const std::wstring subtitle = std::to_wstring(s.selected.size()) + L" " + s.Choose(
        L"selected  |  Existing files are never overwritten",
        L"выбрано  |  Существующие файлы не перезаписываются",
        L"נבחרו  |  קבצים קיימים לא יוחלפו");
    s.subtitle = AddControl(hwnd, L"STATIC", subtitle.c_str(),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
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

    const std::wstring sourceCaption = std::wstring(s.Choose(
        L"Source panel", L"Исходная панель", L"חלונית מקור")) + L"  [" +
        std::to_wstring(s.selected.size()) + L"]";
    s.labelSources = AddControl(hwnd, L"STATIC", sourceCaption.c_str(),
        SS_ENDELLIPSIS | alignCaption, 0, captionEx);
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
        ES_READONLY | ES_AUTOHSCROLL | WS_BORDER, ID_SOURCE_PATH,
        WS_EX_CLIENTEDGE);
    s.destination = AddControl(hwnd, L"EDIT", s.initialDestination.c_str(),
        WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_DEST,
        WS_EX_CLIENTEDGE);

    // Native report-view table: familiar TC name/type/path columns and icons.
    s.sources = AddControl(hwnd, WC_LISTVIEWW, L"",
        WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS |
        LVS_SINGLESEL, ID_SOURCES, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(s.sources,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
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

    s.results = AddControl(hwnd, L"EDIT", L"",
        WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_BORDER, ID_RESULTS, WS_EX_CLIENTEDGE);
    s.modeHint = AddControl(hwnd, L"STATIC", L"",
        SS_EDITCONTROL | alignCaption, ID_MODE_HINT, captionEx);
    s.relativeHint = AddControl(hwnd, L"STATIC", L"",
        SS_EDITCONTROL | alignCaption, ID_RELATIVE_HINT, captionEx);
    s.preview = AddControl(hwnd, L"STATIC", L"",
        SS_ENDELLIPSIS | alignCaption, ID_PREVIEW, captionEx);
    s.status = AddControl(hwnd, L"STATIC", s.Choose(
        L"Ready.", L"Готово.", L"מוכן."), SS_ENDELLIPSIS | alignCaption, 0,
        captionEx);
    s.create = AddControl(hwnd, L"BUTTON", s.Choose(
        L"F5  Create", L"F5  Создать", L"F5  צור"),
        WS_TABSTOP | BS_DEFPUSHBUTTON, ID_CREATE);
    s.close = AddControl(hwnd, L"BUTTON", s.Choose(
        L"Esc  Close", L"Esc  Закрыть", L"Esc  סגור"),
        WS_TABSTOP, ID_CLOSE);

    for (const wchar_t* label : {
        s.Choose(L"Automatic (file / folder)", L"Автоматически (файл / папка)",
                 L"אוטומטי (קובץ / תיקייה)"),
        s.Choose(L"File symbolic link", L"Symlink файла", L"קישור סמלי לקובץ"),
        s.Choose(L"Directory symbolic link", L"Symlink папки", L"קישור סמלי לתיקייה"),
        s.Choose(L"Directory junction (NTFS)", L"Junction папки (NTFS)",
                 L"צומת תיקיות (NTFS)"),
        s.Choose(L"File hardlink", L"Hardlink файла", L"קישור קשיח לקובץ")
    }) SendMessageW(s.kind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));

    SendMessageW(s.kind, CB_SETCURSEL, 0, 0);
    SendMessageW(s.relative, BM_SETCHECK, BST_UNCHECKED, 0);
    RecreateFont(s);
    UpdateModeHelp(s);
    UpdatePreview(s);
    Layout(hwnd, s);
}

void FinishCreate(HWND hwnd, Ui& state, bool cancelled) {
    state.creating = false;
    AppendLine(state.results, L"------------------------------");
    AppendLine(state.results, state.Choose(L"Created: ", L"Создано: ", L"נוצרו: ") +
        std::to_wstring(state.completed) + state.Choose(L"; skipped / failed: ",
            L"; пропущено / ошибки: ", L"; דולגו / נכשלו: ") +
        std::to_wstring(state.failed));
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
         state.Choose(L"Links created successfully.", L"Ссылки успешно созданы.", L"קישורים נוצרו בהצלחה.")));
    EnableWindow(state.create, TRUE);
    SetWindowTextW(state.close, state.Choose(L"Esc  Close", L"Esc  Закрыть", L"Esc  סגור"));
    SetWindowTextW(hwnd, state.Choose(L"TC Symbolic Links - Create Links",
        L"TC Symbolic Links - создание ссылок", L"TC Symbolic Links - יצירת קישורים"));
    UpdateModeHelp(state);
}

void ContinueCreate(HWND hwnd, Ui& state) {
    if (!state.creating) return;
    const auto total = state.selected.size();
    const auto end = (std::min)(total, state.nextItem + kCreateBatchSize);
    for (; state.nextItem < end; ++state.nextItem) {
        const auto& source = state.selected[state.nextItem];
        const auto filename = tcs::BaseName(source);
        if (filename.empty() || filename == L"." || filename == L"..") {
            ++state.failed;
            AppendLine(state.results, source + L": " + state.Choose(
                L"Invalid source (drive root, share root or empty path).",
                L"Некорректный источник (корень диска / пустой путь).",
                L"מקור לא תקין (תיקיית שורש או נתיב ריק)."));
            continue;
        }
        const auto link = tcs::Join(state.batchDestination, filename);
        const DWORD error = tcs::CreateLink(source, link, state.batchKind, state.batchRelative);
        if (error == ERROR_SUCCESS) {
            ++state.completed;
            AppendLine(state.results, L"OK   " + link + L"  ->  " + tcs::FullPath(source));
        } else {
            ++state.failed;
            if (error == ERROR_PRIVILEGE_NOT_HELD) state.privilegeError = true;
            AppendLine(state.results, L"ERR  " + link + L": " +
                tcs::WindowsErrorMessage(error) + L" (" + std::to_wstring(error) + L")");
        }
    }
    if (state.nextItem == total) { FinishCreate(hwnd, state, false); return; }
    SetWindowTextW(hwnd, (std::wstring(L"TC Symbolic Links - ") +
        std::to_wstring(state.nextItem) + L" / " + std::to_wstring(total)).c_str());
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

void BeginCreate(HWND hwnd, Ui& state) {
    if (state.creating) return;
    const auto destination = tcs::FullPath(GetText(state.destination));
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
    state.batchRelative = sel < 3 && SendMessageW(state.relative, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state.batchDestination = destination;
    state.nextItem = 0;
    state.completed = 0;
    state.failed = 0;
    state.privilegeError = false;
    state.creating = true;
    state.hasRun = true;
    SetWindowTextW(state.results, L"");
    EnableWindow(state.create, FALSE);
    EnableWindow(state.relative, FALSE);
    SetWindowTextW(state.status, state.Choose(L"Creating links...",
        L"Создание ссылок...", L"יוצר קישורים..."));
    SetWindowTextW(state.close, state.Choose(L"Esc  Stop", L"Esc  Остановить", L"Esc  עצור"));
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* data = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(data->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
            if (state) {
                state->dpi = GetDpiForWindow(hwnd);
                state->backgroundBrush = CreateSolidBrush(kBackground);
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
            info->ptMinTrackSize.x = (std::min)(MulDiv(620, dpi, 96), static_cast<int>(usable.right - usable.left) - 24);
            info->ptMinTrackSize.y = (std::min)(MulDiv(505, dpi, 96), static_cast<int>(usable.bottom - usable.top) - 24);
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
            if (state && state->backgroundBrush) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(reinterpret_cast<HDC>(wParam), &rc, state->backgroundBrush);
                return TRUE;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            if (state) {
                HPEN line = CreatePen(PS_SOLID, Dp(*state, 2), kAccent);
                HPEN old = reinterpret_cast<HPEN>(SelectObject(dc, line));
                MoveToEx(dc, Dp(*state, 18), Dp(*state, 72), nullptr);
                LineTo(dc, Dp(*state, 130), Dp(*state, 72));
                SelectObject(dc, old);
                DeleteObject(line);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            if (state && state->backgroundBrush) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                const HWND ctl = reinterpret_cast<HWND>(lParam);
                SetTextColor(dc, ctl == state->title ? kAccent :
                    ((ctl == state->subtitle || ctl == state->modeHint ||
                      ctl == state->relativeHint || ctl == state->status) ? kSecondary : kInk));
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_TCS_CREATE_NEXT:
            if (state) ContinueCreate(hwnd, *state);
            return 0;
        case WM_COMMAND:
            if (state && LOWORD(wParam) == ID_KIND && HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateModeHelp(*state);
            } else if (state && LOWORD(wParam) == ID_DEST && HIWORD(wParam) == EN_CHANGE) {
                UpdatePreview(*state);
            } else if (state && LOWORD(wParam) == ID_CREATE && HIWORD(wParam) == BN_CLICKED) {
                BeginCreate(hwnd, *state);
            } else if (LOWORD(wParam) == ID_CLOSE && HIWORD(wParam) == BN_CLICKED) {
                if (state && state->creating) FinishCreate(hwnd, *state, true);
                else DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            if (state && state->creating) { FinishCreate(hwnd, *state, true); return 0; }
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
    std::wstring listFile;
    std::wstring destination;
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
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = kClassName;
    if (!RegisterClassExW(&cls)) { delete state; return 5; }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int winWidth = (std::min)(MulDiv(850, static_cast<int>(GetDpiForSystem()), 96),
                                    static_cast<int>(work.right - work.left) - 24);
    const int winHeight = (std::min)(MulDiv(665, static_cast<int>(GetDpiForSystem()), 96),
                                     static_cast<int>(work.bottom - work.top) - 24);
    HWND window = CreateWindowExW(0, kClassName,
        state->he ? L"TC Symbolic Links — יצירת קישורים" :
        (state->ru ? L"TC Symbolic Links — создание ссылок" :
                     L"TC Symbolic Links — Create Links"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winWidth, winHeight, nullptr, nullptr, instance, state);
    if (!window) {
        // WM_NCDESTROY may already have freed state during failed creation.
        return 6;
    }
    ShowWindow(window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
