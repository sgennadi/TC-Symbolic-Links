// One-DLL Total Commander integration (0.2.2).
// Critical rule: NEVER redirect the user's active button bar. Append to the
// explicitly configured bar in place, with backups and rollback on failure.
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
constexpr wchar_t kTitle[] = L"TC Symbolic Links";
constexpr wchar_t kButtonLabel[] = L"TC Symbolic Links - Create links";
std::atomic_flag g_attempted = ATOMIC_FLAG_INIT;

std::wstring Parent(std::wstring path) {
    const auto n = path.find_last_of(L"\\/");
    return n == std::wstring::npos ? L"" : path.substr(0, n);
}
std::wstring Join(std::wstring dir, const std::wstring& name) {
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    return dir + name;
}
std::wstring GetEnv(const wchar_t* key) {
    std::wstring result(32768, L'\0');
    const DWORD n = GetEnvironmentVariableW(key, result.data(), static_cast<DWORD>(result.size()));
    if (!n || n >= result.size()) return {};
    result.resize(n);
    return result;
}
std::wstring TcDirectory() {
    auto configured = GetEnv(L"COMMANDER_PATH");
    if (!configured.empty()) return configured;
    wchar_t filename[32768]{};
    const auto n = GetModuleFileNameW(nullptr, filename, 32768);
    if (!n || n >= 32768) return {};
    return Parent(filename);
}
std::wstring Resolve(std::wstring value, const std::wstring& iniDir, const std::wstring& tcDir) {
    auto replaceAll = [&](std::wstring_view what, const std::wstring& with) {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
        std::wstring needle(what);
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        std::size_t pos = 0;
        while ((pos = lowered.find(needle, pos)) != std::wstring::npos) {
            value.replace(pos, needle.size(), with);
            lowered.replace(pos, needle.size(), with);
            pos += with.size();
        }
    };
    replaceAll(L"%COMMANDER_PATH%", tcDir);
    replaceAll(L"%COMMANDER_INI_PATH%", iniDir);
    std::wstring expanded(32768, L'\0');
    const DWORD size = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), 32768);
    if (size > 0 && size <= 32768) value.assign(expanded.data());
    if (!value.empty() && value[0] != L'\\' && (value.size() < 2 || value[1] != L':'))
        value = Join(tcDir, value);
    return value;
}
std::wstring ReadIni(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                     const wchar_t* fallback = L"") {
    std::wstring s(65536, L'\0');
    const DWORD n = GetPrivateProfileStringW(section, key, fallback, s.data(),
                                            static_cast<DWORD>(s.size()), path.c_str());
    if (n >= s.size() - 1) return {};
    s.resize(n);
    return s;
}
bool WriteIni(const std::wstring& path, const wchar_t* section,
              const std::wstring& key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key.c_str(), value.c_str(), path.c_str()) != FALSE;
}
bool IsFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool Backup(const std::wstring& path) {
    if (!IsFile(path)) return true;
    const auto backup = path + L".tcslinks.bak";
    return IsFile(backup) || CopyFileW(path.c_str(), backup.c_str(), TRUE) != FALSE;
}
bool ReadBytes(const std::wstring& path, std::vector<unsigned char>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < 16 * 1024 * 1024;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD n = 0;
        ok = bytes.empty() || (ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr)
                               && n == bytes.size());
    }
    CloseHandle(h);
    return ok;
}
bool WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    const bool ok = bytes.empty() || (WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr)
                                      && n == bytes.size());
    CloseHandle(h);
    return ok;
}
struct Snapshot {
    std::wstring path;
    std::vector<unsigned char> original;
};
std::wstring ResolveCustomFieldsIni(const std::wstring& ini, const std::wstring& tcDir) {
    const auto baseDir = Parent(ini);
    const auto redirected = ReadIni(ini, L"CustomFields", L"RedirectSection");
    if (redirected.empty()) {
        const auto alternate = ReadIni(ini, L"Configuration", L"AlternateUserIni");
        return alternate.empty() ? ini : Resolve(alternate, baseDir, tcDir);
    }
    if (redirected == L"0") return ini;
    if (redirected == L"1") {
        const auto alternate = ReadIni(ini, L"Configuration", L"AlternateUserIni");
        return alternate.empty() ? L"" : Resolve(alternate, baseDir, tcDir);
    }
    return Resolve(redirected, baseDir, tcDir);
}
// Prefer portable TC variables. If the plugin lives elsewhere, require its
// absolute DLL path to survive the INI file's legacy ANSI code page unchanged.
std::wstring MacroPath(const std::wstring& path, const std::wstring& iniDir,
                       const std::wstring& tcDir) {
    auto under = [&](const std::wstring& dir, const wchar_t* macro) -> std::wstring {
        if (dir.empty() || path.size() <= dir.size() ||
            _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) != 0 ||
            (path[dir.size()] != L'\\' && path[dir.size()] != L'/')) return {};
        return std::wstring(macro) + path.substr(dir.size());
    };
    auto result = under(tcDir, L"%COMMANDER_PATH%");
    if (!result.empty()) return result;
    result = under(iniDir, L"%COMMANDER_INI_PATH%");
    return result.empty() ? path : result;
}
bool AcpRoundTrip(std::wstring_view value) {
    if (value.empty()) return true;
    BOOL defaultUsed = FALSE;
    const UINT cp = GetACP();
    const DWORD flags = cp == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL* defaultFlag = cp == CP_UTF8 ? nullptr : &defaultUsed;
    const int length = WideCharToMultiByte(cp, flags,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, defaultFlag);
    if (length <= 0 || defaultUsed) return false;
    std::string multibyte(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(cp, flags,
        value.data(), static_cast<int>(value.size()), multibyte.data(), length, nullptr, defaultFlag) != length || defaultUsed)
        return false;
    const int wideLength = MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, multibyte.data(), length, nullptr, 0);
    if (wideLength != value.size()) return false;
    std::wstring roundTrip(static_cast<std::size_t>(wideLength), L'\0');
    return MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, multibyte.data(), length, roundTrip.data(), wideLength) == wideLength
        && roundTrip == value;
}
bool CanOpenForWrite(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
} // namespace

namespace tcs {
bool ConfigureForTotalCommander(const std::wstring& tcIni, const std::wstring& dllPath,
                                std::wstring& status) {
    if (!IsFile(tcIni) || !IsFile(dllPath) || Parent(tcIni).empty()) {
        status = L"Total Commander INI or WDX module is missing.";
        return false;
    }
    const auto iniDir = Parent(tcIni);
    const auto tcDir = TcDirectory();
    if (tcDir.empty()) { status = L"Unable to locate the Total Commander directory."; return false; }
    if (!ReadIni(tcIni, L"Buttonbar", L"RedirectSection").empty() &&
        ReadIni(tcIni, L"Buttonbar", L"RedirectSection") != L"0") {
        status = L"Buttonbar uses RedirectSection; refusing to edit an unknown profile.";
        return false;
    }
    const auto barValue = ReadIni(tcIni, L"Buttonbar", L"Buttonbar");
    // TC can load another toolbar dynamically. Guessing DEFAULT.BAR here used
    // to replace a user's button bar. Never silently guess the active bar.
    if (barValue.empty()) {
        status = L"No explicit active button bar in wincmd.ini; refusing to guess. "
                 L"Set the active bar in TC Configuration > Change button bar.";
        return false;
    }
    const auto barFile = Resolve(barValue, iniDir, tcDir);
    if (!IsFile(barFile)) {
        status = L"The active TC .bar file could not be found. No settings changed.";
        return false;
    }
    // Old versions forced TC to use a cloned toolbar, hiding its real buttons.
    // Require a recovery from wincmd.ini.tcslinks.bak first; never try to
    // promote this damaged clone to the new active bar.
    if (_wcsicmp(barFile.c_str(), Join(iniDir, L"TCSymbolicLinks.bar").c_str()) == 0) {
        status = L"0.2.1 changed your active button bar. Restore the original "
                 L"wincmd.ini.tcslinks.bak and restart TC before configuring 0.2.2.";
        return false;
    }
    const auto columnsIni = ResolveCustomFieldsIni(tcIni, tcDir);
    if (columnsIni.empty() || !IsFile(columnsIni)) {
        status = L"CustomFields configuration could not be resolved safely.";
        return false;
    }
    const auto actualDll = MacroPath(dllPath, iniDir, tcDir);
    // IMPORTANT: GetPrivateProfileStringW removes the *outer* quote pair if
    // an INI value both starts and ends with '"'. Our command MUST keep the
    // quotes around the DLL and the target directory, otherwise rundll32 can
    // silently open an error dialog instead of our Maker window. Finish with
    // an ignored but harmless marker so the final character is not a quote.
    // RunMakerFromArguments deliberately ignores unknown --tc-launch tokens.
    const std::wstring arguments = L"\"" + actualDll +
        L"\",TCLinkCreate --list \"%WL\" --dest \"%T.\" --tc-launch";
    if (!AcpRoundTrip(arguments)) {
        status = L"This plugin path is not representable in the active bar's INI encoding. "
                 L"Install the WDX under COMMANDER_PATH or the TC INI folder.";
        return false;
    }
    wchar_t systemDir[32768]{};
    const UINT sysLen = GetSystemDirectoryW(systemDir, 32768);
    if (!sysLen || sysLen >= 32768) { status = L"Cannot find system rundll32.exe."; return false; }
    const auto host = Join(systemDir, L"rundll32.exe");
    if (!IsFile(host)) { status = L"System rundll32.exe was not found."; return false; }
    const auto rawCount = ReadIni(barFile, L"Buttonbar", L"Buttoncount");
    if (rawCount.empty()) { status = L"Active .bar lacks a valid Buttoncount."; return false; }
    wchar_t* last = nullptr;
    long count = wcstol(rawCount.c_str(), &last, 10);
    if (!last || *last != 0 || count < 0 || count >= 10000) {
        status = L"Active button count is invalid or too large.";
        return false;
    }
    int slot = 0;
    for (int i = 1; i <= count; ++i) {
        const auto n = std::to_wstring(i);
        const auto cmd = ReadIni(barFile, L"Buttonbar", (L"cmd" + n).c_str());
        const auto menu = ReadIni(barFile, L"Buttonbar", (L"menu" + n).c_str());
        if (menu == kButtonLabel) {
            // Treat only a command created by our versions as ours.
            if (_wcsicmp(cmd.c_str(), L"em_TCSymbolicLinks_Create") != 0 &&
                (_wcsicmp(cmd.c_str(), host.c_str()) != 0 ||
                 ReadIni(barFile, L"Buttonbar", (L"param" + n).c_str()).find(L"SymlinkInspector.wdx64") == std::wstring::npos)) {
                status = L"An unrelated button has the same label; refusing to replace it.";
                return false;
            }
            slot = i;
            break;
        }
    }
    if (!CanOpenForWrite(barFile) || !CanOpenForWrite(tcIni) ||
        (columnsIni != tcIni && !CanOpenForWrite(columnsIni))) {
        status = L"Active TC configuration is read-only or in use. No changes made.";
        return false;
    }
    const auto titles = ReadIni(columnsIni, L"CustomFields", L"Titles");
    std::vector<std::wstring> names;
    for (std::size_t pos = 0; pos < titles.size();) {
        const auto end = titles.find(L'|', pos);
        names.push_back(titles.substr(pos, end == std::wstring::npos ? end : end - pos));
        if (end == std::wstring::npos) break;
        pos = end + 1;
    }
    int preset = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (_wcsicmp(names[i].c_str(), kTitle) != 0) continue;
        const auto old = ReadIni(columnsIni, L"CustomFields", (L"Contents" + std::to_wstring(i + 1)).c_str());
        if (old.find(L"SymlinkInspector.") == std::wstring::npos) {
            status = L"Unrelated custom column set has the same name. No settings changed.";
            return false;
        }
        preset = static_cast<int>(i) + 1;
        break;
    }
    const bool newPreset = preset == 0;
    if (newPreset) {
        if (names.size() >= 90) { status = L"Too many custom column sets."; return false; }
        preset = static_cast<int>(names.size()) + 1;
    }
    const int view = SpecialViewForCustomPreset(preset);
    if (!view) { status = L"Invalid custom view index."; return false; }
    std::vector<Snapshot> originals;
    auto remember = [&](const std::wstring& path) {
        for (const auto& snap : originals) if (_wcsicmp(snap.path.c_str(), path.c_str()) == 0) return true;
        Snapshot snap{path, {}};
        if (!ReadBytes(path, snap.original) || !Backup(path)) return false;
        originals.push_back(std::move(snap));
        return true;
    };
    if (!remember(barFile) || !remember(tcIni) || !remember(columnsIni)) {
        status = L"Cannot back up or inspect current configuration. Nothing changed.";
        return false;
    }
    auto rollback = [&] {
        bool okay = true;
        for (auto it = originals.rbegin(); it != originals.rend(); ++it)
            if (!WriteBytes(it->path, it->original)) okay = false;
        return okay;
    };
    const int targetSlot = slot ? slot : static_cast<int>(count) + 1;
    const auto number = std::to_wstring(targetSlot);
    bool ok = WriteIni(barFile, L"Buttonbar", L"cmd" + number, host) &&
        WriteIni(barFile, L"Buttonbar", L"param" + number, arguments) &&
        WriteIni(barFile, L"Buttonbar", L"button" + number, L"WCMICONS.DLL,44") &&
        WriteIni(barFile, L"Buttonbar", L"menu" + number, kButtonLabel);
    if (ok && !slot) ok = WriteIni(barFile, L"Buttonbar", L"Buttoncount", number);
    // Test the exact reader used by our toolbar parser. Quote-loss is a
    // dangerous no-op-button regression and must fail transactionally.
    if (ok && ReadIni(barFile, L"Buttonbar", (L"param" + number).c_str()) != arguments)
        ok = false;
    const auto suffix = std::to_wstring(preset);
    constexpr wchar_t headers[] = L"Name\\nExt\\nLink type\\nLink target\\nExists\\nHardlinks";
    constexpr wchar_t widths[] = L"160,60,140,310,62,-85";
    constexpr wchar_t contents[] = L"[=tc.name]\\n[=tc.ext]\\n[=SymlinkInspector.Link type]\\n"
                                   L"[=SymlinkInspector.Link target]\\n[=SymlinkInspector.Target exists]\\n"
                                   L"[=SymlinkInspector.Hardlink count]";
    if (ok && newPreset) ok = WriteIni(columnsIni, L"CustomFields", L"Titles",
        titles.empty() ? kTitle : titles + L"|" + kTitle);
    if (ok) ok = WriteIni(columnsIni, L"CustomFields", L"Headers" + suffix, headers) &&
            WriteIni(columnsIni, L"CustomFields", L"Widths" + suffix, widths) &&
            WriteIni(columnsIni, L"CustomFields", L"Contents" + suffix, contents) &&
            WriteIni(columnsIni, L"CustomFields", L"Options" + suffix, L"-1|1");
    if (ok) {
        const auto v = std::to_wstring(view);
        ok = WriteIni(tcIni, L"left", L"ShowAllDetails", L"1") &&
             WriteIni(tcIni, L"right", L"ShowAllDetails", L"1") &&
             WriteIni(tcIni, L"left", L"SpecialView", v) &&
             WriteIni(tcIni, L"right", L"SpecialView", v);
    }
    // The original [Buttonbar] Buttonbar entry and usercmd.ini are NEVER changed.
    if (!ok) {
        status = rollback() ? L"Configuration failed; original files were restored in place." :
            L"Configuration failed AND rollback was incomplete. Consult .tcslinks.bak files.";
        return false;
    }
    status = L"Create Links was appended to your existing toolbar; no toolbar was switched. "
             L"The WDX columns are enabled in both panels. .tcslinks.bak backups are available. "
             L"Restart all Total Commander windows to load the button and columns.";
    return true;
}

void AutoConfigureOnFirstLoad(const char* suggestedIniPath) {
    if (g_attempted.test_and_set()) return;
    wchar_t exe[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
    if (!n || n >= 32768) return;
    const auto exeName = std::wstring(exe).substr(std::wstring(exe).find_last_of(L"\\/") + 1);
    if (_wcsicmp(exeName.c_str(), L"TOTALCMD64.EXE") != 0) return;
    std::wstring ini = GetEnv(L"COMMANDER_INI");
    if (ini.empty() && suggestedIniPath && *suggestedIniPath) {
        const int chars = MultiByteToWideChar(CP_ACP, 0, suggestedIniPath, -1, nullptr, 0);
        if (chars > 0 && chars < 32768) {
            std::wstring suggested(static_cast<std::size_t>(chars), L'\0');
            if (MultiByteToWideChar(CP_ACP, 0, suggestedIniPath, -1, suggested.data(), chars))
                ini = Join(Parent(suggested), L"wincmd.ini");
        }
    }
    if (!IsFile(ini)) return;
    const auto marker = Join(Parent(ini), L"TCSymbolicLinksInstall.ini");
    if ((GetPrivateProfileIntW(L"Setup", L"Completed", 0, marker.c_str()) &&
         GetPrivateProfileIntW(L"Setup", L"Version", 0, marker.c_str()) >= 202) ||
        GetPrivateProfileIntW(L"Setup", L"Declined", 0, marker.c_str())) return;
    const HWND owner = FindWindowW(L"TTOTAL_CMD", nullptr);
    const LANGID lang = GetUserDefaultUILanguage();
    const bool ru = PRIMARYLANGID(lang) == LANG_RUSSIAN;
    const bool he = PRIMARYLANGID(lang) == LANG_HEBREW;
    const wchar_t* prompt = he ?
        L"להוסיף כפתור קישורים לפאנל הנוכחי ולהציג עמודות בשני הצדדים? נשמור גיבוי." :
        ru ? L"Добавить кнопку в ТЕКУЩУЮ панель без её замены и настроить колонки в обеих панелях? "
             L"Будут сделаны резервные копии." :
             L"Append one button to your CURRENT toolbar (without replacing it) and enable WDX columns "
             L"in both panes? Original files will be backed up.";
    if (MessageBoxW(owner, prompt, L"TC Symbolic Links", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        WriteIni(marker, L"Setup", L"Declined", L"1");
        return;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&AutoConfigureOnFirstLoad), &self)) return;
    wchar_t module[32768]{};
    const auto nMod = GetModuleFileNameW(self, module, 32768);
    if (!nMod || nMod >= 32768) return;
    std::wstring status;
    const bool success = ConfigureForTotalCommander(ini, module, status);
    if (success) {
        WriteIni(marker, L"Setup", L"Completed", L"1");
        WriteIni(marker, L"Setup", L"Version", L"202");
    }
    MessageBoxW(owner, status.c_str(), L"TC Symbolic Links", MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
}
} // namespace tcs
