// One-DLL Total Commander integration. Explicit one-time consent is required
// before modifying a user's toolbar and column layouts. pluginst.inf itself
// cannot add a toolbar button or column preset: this runs on WDX initialization.
#include "TcIntegration.h"
#include "TcCustomView.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kCommand[] = L"em_TCSymbolicLinks_Create";
constexpr wchar_t kTitle[] = L"TC Symbolic Links";
std::atomic_flag g_attempted = ATOMIC_FLAG_INIT;

std::wstring Parent(std::wstring path) {
    auto n = path.find_last_of(L"\\/");
    return n == std::wstring::npos ? L"" : path.substr(0, n);
}
std::wstring Join(std::wstring a, const std::wstring& name) {
    if (!a.empty() && a.back() != L'\\' && a.back() != L'/') a += L'\\';
    return a + name;
}
std::wstring GetEnv(const wchar_t* key) {
    std::wstring value(32768, L'\0');
    const DWORD n = GetEnvironmentVariableW(key, value.data(), static_cast<DWORD>(value.size()));
    if (!n || n >= value.size()) return {};
    value.resize(n);
    return value;
}
std::wstring TcDirectory() {
    auto path = GetEnv(L"COMMANDER_PATH");
    if (!path.empty()) return path;
    wchar_t filename[32768]{};
    auto n = GetModuleFileNameW(nullptr, filename, 32768);
    if (!n || n >= 32768) return {};
    return Parent(filename);
}
std::wstring Resolve(std::wstring value, const std::wstring& iniDir,
                     const std::wstring& tcDir) {
    auto replaceAll = [&](std::wstring_view what, const std::wstring& with) {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
        std::wstring target(what);
        std::transform(target.begin(), target.end(), target.begin(), towlower);
        std::size_t pos = 0;
        while ((pos = lowered.find(target, pos)) != std::wstring::npos) {
            value.replace(pos, target.size(), with);
            lowered.replace(pos, target.size(), with);
            pos += with.size();
        }
    };
    replaceAll(L"%COMMANDER_PATH%", tcDir);
    replaceAll(L"%COMMANDER_INI_PATH%", iniDir);
    std::wstring expanded(32768, L'\0');
    DWORD size = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), 32768);
    if (size > 0 && size <= 32768) value.assign(expanded.data());
    // Relative paths in wincmd.ini and .bar resolve relative to TC install.
    if (!value.empty() && value[0] != L'\\' && (value.size() < 2 || value[1] != L':'))
        value = Join(tcDir, value);
    return value;
}
std::wstring ReadIni(const std::wstring& path, const wchar_t* section,
                     const wchar_t* key, const wchar_t* fallback = L"") {
    std::wstring s(65536, L'\0');
    DWORD n = GetPrivateProfileStringW(section, key, fallback, s.data(),
                                     static_cast<DWORD>(s.size()), path.c_str());
    if (n >= s.size() - 1) return {};
    s.resize(n);
    return s;
}
bool WriteIni(const std::wstring& path, const wchar_t* section,
              const std::wstring& key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key.c_str(), value.c_str(), path.c_str()) != FALSE;
}
bool IsFile(const std::wstring& file) {
    DWORD a = GetFileAttributesW(file.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
bool Backup(const std::wstring& path) {
    if (!IsFile(path)) return true;
    std::wstring back = path + L".tcslinks.bak";
    return IsFile(back) || CopyFileW(path.c_str(), back.c_str(), TRUE) != FALSE;
}

bool LoadRaw(const std::wstring& path, std::vector<unsigned char>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < 16 * 1024 * 1024;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = bytes.empty() || (ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
                               && read == bytes.size());
    }
    CloseHandle(h);
    return ok;
}
bool WriteRaw(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
              && written == bytes.size();
    CloseHandle(h);
    return ok;
}
// Ensure the user-defined-command INI supports a Unicode absolute DLL path.
// All pre-existing bytes are backed up first and decoded as UTF-8 BOM or the
// legacy Windows ANSI codepage before conversion to UTF-16LE.
bool EnsureUtf16Ini(const std::wstring& path) {
    std::vector<unsigned char> bytes;
    if (IsFile(path)) {
        if (!LoadRaw(path, bytes) || !Backup(path)) return false;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) return true;
    UINT cp = CP_ACP;
    std::size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        cp = CP_UTF8; offset = 3;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) return false;
    const int size = static_cast<int>(bytes.size() - offset);
    int length = 0;
    if (size) {
        length = MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
            reinterpret_cast<const char*>(bytes.data() + offset), size, nullptr, 0);
        if (!length) return false;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (length && MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
        reinterpret_cast<const char*>(bytes.data() + offset), size, wide.data(), length) != length)
        return false;
    std::vector<unsigned char> converted{0xFF, 0xFE};
    for (wchar_t c : wide) {
        converted.push_back(static_cast<unsigned char>(c & 0xFF));
        converted.push_back(static_cast<unsigned char>((c >> 8) & 0xFF));
    }
    return WriteRaw(path, converted);
}
std::wstring ResolveCustomFieldsIni(const std::wstring& ini, const std::wstring& config,
                                    const std::wstring& tcDir) {
    const auto baseDir = Parent(ini);
    auto redirected = ReadIni(ini, L"CustomFields", L"RedirectSection");
    if (redirected.empty()) {
        auto alternate = ReadIni(config, L"Configuration", L"AlternateUserIni");
        if (alternate.empty()) return ini;
        return Resolve(alternate, baseDir, tcDir);
    }
    if (redirected == L"0") return ini;
    if (redirected == L"1") {
        auto alternate = ReadIni(config, L"Configuration", L"AlternateUserIni");
        return alternate.empty() ? L"" : Resolve(alternate, baseDir, tcDir);
    }
    return Resolve(redirected, baseDir, tcDir);
}
}
namespace tcs {
bool ConfigureForTotalCommander(const std::wstring& tcIni, const std::wstring& dllPath,
                                std::wstring& status) {
    if (!IsFile(tcIni) || !IsFile(dllPath) || Parent(tcIni).empty()) {
        status = L"Total Commander INI or WDX module is missing.";
        return false;
    }
    const auto baseDir = Parent(tcIni);
    const auto tcDir = TcDirectory();
    const auto columnsIni = ResolveCustomFieldsIni(tcIni, tcIni, tcDir);
    if (columnsIni.empty()) { status = L"CustomFields redirection could not be resolved."; return false; }
    if (!IsFile(columnsIni) && columnsIni != tcIni) {
        status = L"Redirected CustomFields file was not found."; return false;
    }
    const auto barIni = [&] {
        auto redirected = ReadIni(tcIni, L"Buttonbar", L"RedirectSection");
        return redirected.empty() || redirected == L"0" ? tcIni : std::wstring();
    }();
    if (barIni.empty()) {
        status = L"Buttonbar uses RedirectSection. Refusing to modify an unknown profile.";
        return false;
    }
    auto barConfig = ReadIni(tcIni, L"Buttonbar", L"Buttonbar");
    if (barConfig.empty()) barConfig = Join(tcDir, L"DEFAULT.BAR");
    const auto sourceBar = Resolve(barConfig, baseDir, tcDir);
    if (!IsFile(sourceBar)) {
        status = L"Existing TC toolbar was not found. No settings changed.";
        return false;
    }
    const auto userCmd = Join(baseDir, L"usercmd.ini");
    const auto priorCommand = ReadIni(userCmd, kCommand, L"cmd");
    const auto priorParameters = ReadIni(userCmd, kCommand, L"param");
    // Never overwrite a user-defined command just because it happens to
    // start rundll32. Older versions used an unqualified rundll32.exe.
    if (!priorCommand.empty()) {
        constexpr wchar_t kHostName[] = L"rundll32.exe";
        constexpr std::size_t kHostChars = (sizeof(kHostName) / sizeof(wchar_t)) - 1;
        const bool exactHost = _wcsicmp(priorCommand.c_str(), kHostName) == 0;
        const bool absoluteHost = priorCommand.size() > kHostChars &&
            priorCommand[priorCommand.size() - kHostChars - 1] == L'\\' &&
            _wcsicmp(priorCommand.c_str() + (priorCommand.size() - kHostChars),
                      kHostName) == 0;
        const bool oldLauncher = exactHost || absoluteHost;
        const bool ownExport = priorParameters.find(L",TCLinkCreate ") != std::wstring::npos ||
            priorParameters.find(L",TCLinkCreateW ") != std::wstring::npos;
        if (!oldLauncher || !ownExport ||
            priorParameters.find(L"SymlinkInspector.wdx64") == std::wstring::npos) {
            status = L"An unrelated user command already has the reserved name. No changes made.";
            return false;
        }
    }
    // Find a free preset index, or reuse OUR existing preset. Never replace
    // someone else's preset simply because its title looks similar.
    const auto titles = ReadIni(columnsIni, L"CustomFields", L"Titles");
    std::vector<std::wstring> names;
    std::size_t pos = 0;
    while (pos <= titles.size() && !titles.empty()) {
        const auto end = titles.find(L'|', pos);
        names.push_back(titles.substr(pos, end == std::wstring::npos ? end : end - pos));
        if (end == std::wstring::npos) break;
        pos = end + 1;
    }
    int index = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (_wcsicmp(names[i].c_str(), kTitle) != 0) continue;
        const auto previous = ReadIni(columnsIni, L"CustomFields", (L"Contents" + std::to_wstring(i+1)).c_str());
        if (previous.find(L"SymlinkInspector.") != std::wstring::npos) { index = static_cast<int>(i) + 1; break; }
        status = L"An existing column set uses the same name. No settings changed.";
        return false;
    }
    const bool newColumns = index == 0;
    if (newColumns) {
        if (names.size() >= 90) { status = L"Too many existing TC column sets."; return false; }
        index = static_cast<int>(names.size()) + 1;
    }
    // Keep original toolbar untouched: clone once to the user's INI folder,
    // adding exactly one button to that copy. This also works in Program Files.
    const std::wstring localBar = Join(baseDir, L"TCSymbolicLinks.bar");
    if (IsFile(localBar)) {
        // Only edit our own cloned bar. Do not clobber a different file.
        if (ReadIni(localBar, L"Buttonbar", L"TCSymbolicLinksOwned") != L"1") {
            status = L"A file named TCSymbolicLinks.bar already exists and is not owned by this plugin.";
            return false;
        }
    } else if (!CopyFileW(sourceBar.c_str(), localBar.c_str(), TRUE)) {
        status = L"Could not clone the existing toolbar into the TC configuration folder.";
        return false;
    }
    if (!Backup(tcIni) || !Backup(columnsIni) || !Backup(userCmd) || !Backup(localBar)) {
        status = L"Could not back up existing settings. Configuration has stopped.";
        return false;
    }
    if (!EnsureUtf16Ini(userCmd)) {
        status = L"Could not preserve existing usercmd.ini as Unicode.";
        return false;
    }
    // TC %T always ends with a backslash. Appending '.' inside the quotes
    // avoids CommandLineToArgvW treating that trailing slash as an escape of
    // the closing quotation mark (e.g. "C:\destination\").
    // rundll32 first tries <requested_name>W and only calls that candidate
    // with a UTF-16 command line. Request TCLinkCreate, NOT TCLinkCreateW:
    // the latter would search TCLinkCreateWW before falling back to an ANSI
    // invocation of the exact name, corrupting Hebrew and other Unicode paths.
    const std::wstring arguments = L"\"" + dllPath + L"\",TCLinkCreate --list \"%WL\" --dest \"%T.\"";
    wchar_t systemFolder[32768]{};
    const UINT systemChars = GetSystemDirectoryW(systemFolder, 32768);
    if (!systemChars || systemChars >= 32768) {
        status = L"Unable to locate the trusted Windows system directory.";
        return false;
    }
    const std::wstring trustedRundll32 = Join(systemFolder, L"rundll32.exe");
    if (!IsFile(trustedRundll32)) {
        status = L"Trusted Windows rundll32.exe was not found.";
        return false;
    }
    if (!WriteIni(userCmd, kCommand, L"cmd", trustedRundll32) ||
        !WriteIni(userCmd, kCommand, L"param", arguments) ||
        !WriteIni(userCmd, kCommand, L"menu", L"TC Symbolic Links - Create links")) {
        status = L"Unable to register the unicode extended command in usercmd.ini.";
        return false;
    }
    // Use an ASCII-only button referencing the Unicode usercmd.ini entry.
    int count = GetPrivateProfileIntW(L"Buttonbar", L"Buttoncount", 0, localBar.c_str());
    bool existingButton = false;
    for (int i = 1; i <= count && i < 10000; ++i)
        if (_wcsicmp(ReadIni(localBar, L"Buttonbar", (L"cmd" + std::to_wstring(i)).c_str()).c_str(),
                     kCommand) == 0) existingButton = true;
    if (!existingButton) {
        if (count < 0 || count >= 10000) { status = L"Unsupported button count."; return false; }
        ++count;
        const auto suffix = std::to_wstring(count);
        if (!WriteIni(localBar, L"Buttonbar", L"button" + suffix, L"WCMICONS.DLL,44") ||
            !WriteIni(localBar, L"Buttonbar", L"cmd" + suffix, kCommand) ||
            !WriteIni(localBar, L"Buttonbar", L"menu" + suffix, L"TC Symbolic Links - Create links") ||
            !WriteIni(localBar, L"Buttonbar", L"Buttoncount", suffix)) {
            status = L"Could not append the new toolbar button.";
            return false;
        }
    }
    if (!WriteIni(localBar, L"Buttonbar", L"TCSymbolicLinksOwned", L"1") ||
        // Keep the existing wincmd.ini encoding untouched: WritePrivateProfileStringW
        // cannot store arbitrary Hebrew path names in an ANSI INI. TC expands
        // this built-in variable itself, preserving a Unicode profile folder.
        !WriteIni(tcIni, L"Buttonbar", L"Buttonbar", L"%COMMANDER_INI_PATH%\\TCSymbolicLinks.bar")) {
        status = L"Could not activate the preserving toolbar copy.";
        return false;
    }
    if (newColumns && !WriteIni(columnsIni, L"CustomFields", L"Titles",
        titles.empty() ? kTitle : titles + L"|" + kTitle)) {
        status = L"Could not append the custom column set title.";
        return false;
    }
    const auto suffix = std::to_wstring(index);
    constexpr wchar_t headers[] = L"Name\\nExt\\nLink type\\nLink target\\nExists\\nHardlinks";
    constexpr wchar_t widths[] = L"160,60,140,310,62,-85";
    constexpr wchar_t contents[] = L"[=tc.name]\\n[=tc.ext]\\n[=SymlinkInspector.Link type]\\n"
                                   L"[=SymlinkInspector.Link target]\\n[=SymlinkInspector.Target exists]\\n"
                                   L"[=SymlinkInspector.Hardlink count]";
    if (!WriteIni(columnsIni, L"CustomFields", L"Headers" + suffix, headers) ||
        !WriteIni(columnsIni, L"CustomFields", L"Widths" + suffix, widths) ||
        !WriteIni(columnsIni, L"CustomFields", L"Contents" + suffix, contents) ||
        !WriteIni(columnsIni, L"CustomFields", L"Options" + suffix, L"-1|1")) {
        status = L"Could not write the WDX column definitions.";
        return false;
    }
    // User requested the columns to be enabled automatically, not just listed
    // in TC options. The previous view is preserved in wincmd.ini backup.
    // In Total Commander, SpecialView 0 means standard, 1 comments,
    // 2 thumbnails, and 3 is CUSTOM PRESET #1. Thus preset n maps to n+2.
    const int specialView = SpecialViewForCustomPreset(index);
    if (!specialView) { status = L"Invalid custom view index."; return false; }
    const std::wstring selectedView = std::to_wstring(specialView);
    if (!WriteIni(tcIni, L"left", L"ShowAllDetails", L"1") ||
        !WriteIni(tcIni, L"right", L"ShowAllDetails", L"1") ||
        !WriteIni(tcIni, L"left", L"SpecialView", selectedView) ||
        !WriteIni(tcIni, L"right", L"SpecialView", selectedView)) {
        status = L"Could not activate the WDX columns for both panes.";
        return false;
    }
    status = L"TC Symbolic Links integrated. The original settings have .tcslinks.bak backups.\n"
             L"A Create Links button was added and the WDX column preset activated in both panels.\n"
             L"Restart Total Commander to reload its toolbar and column settings.";
    return true;
}

void AutoConfigureOnFirstLoad(const char* suggestedIniPath) {
    if (g_attempted.test_and_set()) return;
    // Never prompt/inject settings from the Linux loader or another process
    // such as tests, Explorer, PowerShell or arbitrary DLL hosts.
    wchar_t exe[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
    if (!n || n >= 32768) return;
    const auto exeName = std::wstring(exe).substr(std::wstring(exe).find_last_of(L"\\/") + 1);
    if (_wcsicmp(exeName.c_str(), L"TOTALCMD64.EXE") != 0) return;
    std::wstring ini = GetEnv(L"COMMANDER_INI");
    if (ini.empty() && suggestedIniPath && *suggestedIniPath) {
        int nChars = MultiByteToWideChar(CP_ACP, 0, suggestedIniPath, -1, nullptr, 0);
        if (nChars > 0 && nChars < 32768) {
            std::wstring suggested(static_cast<std::size_t>(nChars), L'\0');
            if (MultiByteToWideChar(CP_ACP, 0, suggestedIniPath, -1, suggested.data(), nChars))
                ini = Join(Parent(suggested), L"wincmd.ini");
        }
    }
    if (!IsFile(ini)) return;
    const auto marker = Join(Parent(ini), L"TCSymbolicLinksInstall.ini");
    if (GetPrivateProfileIntW(L"Setup", L"Completed", 0, marker.c_str()) ||
        GetPrivateProfileIntW(L"Setup", L"Declined", 0, marker.c_str())) return;
    HWND owner = FindWindowW(L"TTOTAL_CMD", nullptr);
    LANGID lang = GetUserDefaultUILanguage();
    const bool ru = PRIMARYLANGID(lang) == LANG_RUSSIAN;
    const bool he = PRIMARYLANGID(lang) == LANG_HEBREW;
    const wchar_t* prompt = he ?
        L"להוסיף אוטומטית כפתור ליצירת קישורים ועמודות מידע בשתי החלוניות?\n"
        L"נשמור גיבוי של ההגדרות. יש להפעיל מחדש את Total Commander." :
        ru ? L"Добавить автоматически кнопку создания ссылок и колонки WDX в обе панели?\n"
             L"Существующие настройки будут сохранены в резервных копиях. Потребуется перезапуск TC." :
             L"Add the Create Links button and WDX columns to both panels automatically?\n"
             L"Existing settings will be backed up. Total Commander restart is required.";
    if (MessageBoxW(owner, prompt, L"TC Symbolic Links", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        WriteIni(marker, L"Setup", L"Declined", L"1");
        return;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&AutoConfigureOnFirstLoad), &self)) return;
    wchar_t path[32768]{};
    auto len = GetModuleFileNameW(self, path, 32768);
    if (!len || len >= 32768) return;
    std::wstring status;
    const bool ok = ConfigureForTotalCommander(ini, path, status);
    if (ok) WriteIni(marker, L"Setup", L"Completed", L"1");
    MessageBoxW(owner, status.c_str(), L"TC Symbolic Links",
                MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}
} // namespace tcs
