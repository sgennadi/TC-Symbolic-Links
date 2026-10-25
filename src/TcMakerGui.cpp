// Win32 Unicode / HiDPI creation UI now compiled directly into the WDX DLL.
// Uses %WL (UTF-16 list) and %T (destination panel) without cmd.exe.
#include "LinkCore.h"
#include "ListParser.h"
#include "TcMakerGui.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>
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
constexpr UINT WM_TCS_CREATE_NEXT = WM_APP + 41;
constexpr std::size_t kCreateBatchSize = 12;

struct Ui {
    bool ru = false;
    bool he = false;
    UINT dpi = 96;
    HFONT font = nullptr;
    HWND labelSources = nullptr;
    HWND sources = nullptr;
    HWND labelDestination = nullptr;
    HWND destination = nullptr;
    HWND labelKind = nullptr;
    HWND kind = nullptr;
    HWND relative = nullptr;
    HWND labelResults = nullptr;
    HWND results = nullptr;
    HWND create = nullptr;
    HWND close = nullptr;
    std::vector<std::wstring> selected;
    std::wstring initialDestination;
    bool creating = false;
    std::size_t nextItem = 0;
    int completed = 0;
    int failed = 0;
    tcs::LinkKind batchKind = tcs::LinkKind::Automatic;
    bool batchRelative = false;
    std::wstring batchDestination;
    ~Ui() { if (font) DeleteObject(font); }
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

void RecreateFont(Ui& state) {
    const HFONT oldFont = state.font;
    state.font = CreateFontW(-MulDiv(10, static_cast<int>(state.dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    for (HWND item : {state.labelSources, state.sources, state.labelDestination,
                      state.destination, state.labelKind, state.kind, state.relative,
                      state.labelResults, state.results, state.create, state.close}) {
        if (item && state.font) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    if (oldFont && state.font) DeleteObject(oldFont);
    if (!state.font) state.font = oldFont;
}

HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* title, DWORD style,
                int id, DWORD extended = 0) {
    return CreateWindowExW(extended, cls, title, style | WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

void Layout(HWND hwnd, Ui& s) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    const int margin = Dp(s, 14);
    const int gap = Dp(s, 7);
    const int labelH = Dp(s, 20);
    const int inputH = Dp(s, 27);
    const int wide = (std::max)(Dp(s, 300), w - margin * 2);
    int y = margin;
    auto pos = [&](HWND control, int x, int yy, int width, int height) {
        MoveWindow(control, x, yy, (std::max)(1, width), (std::max)(1, height), TRUE);
    };
    pos(s.labelSources, margin, y, wide, labelH); y += labelH + Dp(s, 3);
    pos(s.sources, margin, y, wide, Dp(s, 110)); y += Dp(s, 110) + gap;
    pos(s.labelDestination, margin, y, wide, labelH); y += labelH + Dp(s, 3);
    pos(s.destination, margin, y, wide, inputH); y += inputH + gap;
    pos(s.labelKind, margin, y, Dp(s, 125), labelH + Dp(s, 5));
    pos(s.kind, margin + Dp(s, 133), y, (std::max)(Dp(s, 240), wide - Dp(s, 133)), Dp(s, 300));
    y += inputH + Dp(s, 2);
    pos(s.relative, margin, y, wide, Dp(s, 29)); y += Dp(s, 29) + gap;
    pos(s.labelResults, margin, y, wide, labelH); y += labelH + Dp(s, 3);
    const int bottom = h - margin - Dp(s, 35) - gap;
    pos(s.results, margin, y, wide, (std::max)(Dp(s, 70), bottom - y));
    pos(s.create, (std::max)(margin, w - margin - Dp(s, 270)), h - margin - Dp(s, 35),
        Dp(s, 160), Dp(s, 35));
    pos(s.close, (std::max)(margin + Dp(s, 170), w - margin - Dp(s, 100)),
        h - margin - Dp(s, 35), Dp(s, 100), Dp(s, 35));
}

void CreateControls(HWND hwnd, Ui& s) {
    const DWORD captionEx = s.he ? WS_EX_RTLREADING : 0;
    const DWORD captionStyle = s.he ? SS_RIGHT : 0;
    s.labelSources = AddControl(hwnd, L"STATIC", s.Choose(L"Selected source files and folders:",
        L"Выбранные файлы и папки:", L"קבצים ותיקיות שנבחרו:"), captionStyle, 0, captionEx);
    s.sources = AddControl(hwnd, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
        WS_BORDER | LBS_NOINTEGRALHEIGHT, ID_SOURCES, WS_EX_CLIENTEDGE);
    s.labelDestination = AddControl(hwnd, L"STATIC", s.Choose(L"Destination (opposite panel):",
        L"Папка назначения (другая панель):", L"תיקיית יעד (בחלונית השנייה):"), captionStyle, 0, captionEx);
    s.destination = AddControl(hwnd, L"EDIT", s.initialDestination.c_str(),
        WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_DEST, WS_EX_CLIENTEDGE);
    s.labelKind = AddControl(hwnd, L"STATIC", s.Choose(L"Link type:",
        L"Тип ссылки:", L"סוג קישור:"), captionStyle, 0, captionEx);
    s.kind = AddControl(hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST, ID_KIND);
    s.relative = AddControl(hwnd, L"BUTTON", s.Choose(L"Use relative targets when possible (symlinks only)",
        L"Относительные пути, когда возможно (только symlink)",
        L"השתמש בנתיבים יחסיים כשאפשר (קישורים סמליים בלבד)"),
        WS_TABSTOP | BS_AUTOCHECKBOX | (s.he ? BS_RIGHTBUTTON : 0), ID_RELATIVE, captionEx);
    s.labelResults = AddControl(hwnd, L"STATIC", s.Choose(L"Operation results:",
        L"Результаты операции:", L"תוצאות הפעולה:"), captionStyle, 0, captionEx);
    s.results = AddControl(hwnd, L"EDIT", L"", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
        ES_READONLY | ES_AUTOVSCROLL | WS_BORDER, ID_RESULTS, WS_EX_CLIENTEDGE);
    s.create = AddControl(hwnd, L"BUTTON", s.Choose(L"Create links", L"Создать ссылки",
        L"צור קישורים"), WS_TABSTOP | BS_DEFPUSHBUTTON, ID_CREATE, captionEx);
    s.close = AddControl(hwnd, L"BUTTON", s.Choose(L"Close", L"Закрыть", L"סגור"),
        WS_TABSTOP, ID_CLOSE, captionEx);

    for (auto& file : s.selected) SendMessageW(s.sources, LB_ADDSTRING, 0,
                                               reinterpret_cast<LPARAM>(file.c_str()));
    for (const wchar_t* value : {s.Choose(L"Automatic: file / directory symlink",
            L"Автоматически: файл / папка", L"אוטומטי: קישור סמלי לקובץ / תיקייה"),
           s.Choose(L"File symbolic link", L"Символическая ссылка на файл",
            L"קישור סמלי לקובץ"),
           s.Choose(L"Directory symbolic link", L"Символическая ссылка на папку",
            L"קישור סמלי לתיקייה"),
           s.Choose(L"Directory junction (NTFS)", L"Junction для папки (NTFS)",
            L"צומת תיקיות (NTFS)"),
           s.Choose(L"File hard link (same volume)", L"Жёсткая ссылка (один том)",
            L"קישור קשיח לקובץ (אותו כונן)")}) {
        SendMessageW(s.kind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    SendMessageW(s.kind, CB_SETCURSEL, 0, 0);
    SendMessageW(s.relative, BM_SETCHECK, BST_UNCHECKED, 0);
    RecreateFont(s);
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
        L"Остановлено пользователем. Созданные ссылки остаются на месте.",
        L"נעצר על ידי המשתמש. קישורים שכבר נוצרו נשארים במקומם."));
    if (state.failed) AppendLine(state.results, state.Choose(
        L"Existing destinations are never overwritten. Error 1314: enable Developer Mode or run elevated.",
        L"Существующие объекты не перезаписываются. Ошибка 1314: включите режим разработчика или права администратора.",
        L"יעדים קיימים אינם מוחלפים. שגיאה 1314: הפעל מצב מפתח או הרץ כמנהל מערכת."));
    EnableWindow(state.create, TRUE);
    SetWindowTextW(state.close, state.Choose(L"Close", L"Закрыть", L"סגור"));
    SetWindowTextW(hwnd, state.Choose(L"TC Symbolic Links - Create Links",
        L"TC Symbolic Links - создание ссылок", L"TC Symbolic Links - יצירת קישורים"));
}

void ContinueCreate(HWND hwnd, Ui& state) {
    if (!state.creating) return; // Ignore queued messages after the user presses Stop.
    const auto total = state.selected.size();
    const auto end = (std::min)(total, state.nextItem + kCreateBatchSize);
    for (; state.nextItem < end; ++state.nextItem) {
        const auto& source = state.selected[state.nextItem];
        const std::wstring filename = tcs::BaseName(source);
        if (filename.empty() || filename == L"." || filename == L"..") {
            ++state.failed;
            AppendLine(state.results, source + L": " + state.Choose(
                L"Invalid source name", L"Недопустимое имя", L"שם מקור לא תקין"));
            continue;
        }
        const auto target = tcs::Join(state.batchDestination, filename);
        const DWORD status = tcs::CreateLink(source, target, state.batchKind, state.batchRelative);
        if (status == ERROR_SUCCESS) {
            ++state.completed;
            AppendLine(state.results, L"OK  " + target + L"  ->  " + source);
        } else {
            ++state.failed;
            AppendLine(state.results, L"ERR " + target + L": " +
                tcs::WindowsErrorMessage(status) + L" (" + std::to_wstring(status) + L")");
        }
    }
    if (state.nextItem == total) {
        FinishCreate(hwnd, state, false);
        return;
    }
    const auto progress = std::to_wstring(state.nextItem) + L" / " + std::to_wstring(total);
    SetWindowTextW(hwnd, (std::wstring(L"TC Symbolic Links - ") + progress).c_str());
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

void BeginCreate(HWND hwnd, Ui& state) {
    using tcs::LinkKind;
    if (state.creating) return;
    const auto dest = tcs::FullPath(GetText(state.destination));
    if (!tcs::IsExistingDirectory(dest)) {
        MessageBoxW(hwnd, state.Choose(L"Choose an existing destination folder.",
            L"Укажите существующую папку назначения.", L"בחר תיקיית יעד קיימת."),
            L"TC Symbolic Links", MB_OK | MB_ICONWARNING);
        SetFocus(state.destination);
        return;
    }
    const auto selection = SendMessageW(state.kind, CB_GETCURSEL, 0, 0);
    state.batchKind = (selection >= 0 && selection <= 4) ?
        static_cast<LinkKind>(selection) : LinkKind::Automatic;
    state.batchRelative = SendMessageW(state.relative, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state.batchDestination = dest; // Snapshot settings for the entire operation.
    state.nextItem = 0;
    state.completed = 0;
    state.failed = 0;
    state.creating = true;
    SetWindowTextW(state.results, L"");
    EnableWindow(state.create, FALSE);
    SetWindowTextW(state.close, state.Choose(L"Stop", L"Остановить", L"עצור"));
    if (!PostMessageW(hwnd, WM_TCS_CREATE_NEXT, 0, 0)) FinishCreate(hwnd, state, true);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* data = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
            if (state) { state->dpi = GetDpiForWindow(hwnd); CreateControls(hwnd, *state); }
            return 0;
        case WM_SIZE:
            if (state && state->sources) Layout(hwnd, *state);
            return 0;
        case WM_GETMINMAXINFO: {
            const auto dpi = state ? state->dpi : 96;
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {MulDiv(600, static_cast<int>(dpi), 96),
                                     MulDiv(510, static_cast<int>(dpi), 96)};
            return 0;
        }
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                const auto* rect = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                    rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateFont(*state);
                Layout(hwnd, *state);
            }
            return 0;
        case WM_TCS_CREATE_NEXT:
            if (state) ContinueCreate(hwnd, *state);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_CREATE && HIWORD(wParam) == BN_CLICKED && state)
                BeginCreate(hwnd, *state);
            else if (LOWORD(wParam) == ID_CLOSE && HIWORD(wParam) == BN_CLICKED) {
                if (state && state->creating) FinishCreate(hwnd, *state, true);
                else DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete state;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
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
    const int winWidth = MulDiv(810, static_cast<int>(GetDpiForSystem()), 96);
    const int winHeight = MulDiv(650, static_cast<int>(GetDpiForSystem()), 96);
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
