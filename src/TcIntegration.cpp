// One-DLL Total Commander integration (0.2.5).
// NEVER change [Buttonbar] Buttonbar or activate another toolbar. If no key
// exists, use the original DEFAULT.BAR only when writable; otherwise refuse.
// Absolute WDX paths in button params: Total Commander expands %C in
// %COMMANDER_PATH% as an item substitution, corrupting DLL filenames.
// On failure all modified files are restored byte-for-byte.
#include "TcIntegration.h"
#include "TcCustomView.h"
#include "UpdateManager.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kTitle[] = L"TC Symbolic Links";
constexpr std::array<const wchar_t*, 3> kButtonLabels{
    L"TC Symbolic Links - Create links",
    L"TC Symbolic Links - File symlink",
    L"TC Symbolic Links - Directory symlink"
};
// In the common Maker each button chooses an initial mode, which the user
// may change before pressing F5. No companion executable is necessary.
constexpr std::array<const wchar_t*, 3> kModeArguments{
    L"", L" --mode file", L" --mode directory"
};
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
    // Preserve the ORIGINAL pre-plugin backup across upgrades. Never replace it.
    const auto firstBackup = path + L".tcslinks.bak";
    if (!IsFile(firstBackup) &&
        !CopyFileW(path.c_str(), firstBackup.c_str(), TRUE)) return false;

    // Also preserve the IMMEDIATE pre-change state on EVERY setup attempt.
    // This permits recovery of commands added after the very first install.
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const std::uint64_t stamp =
        (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const auto snapshot = path + L".tcslinks.preupdate." +
        std::to_wstring(stamp) + L"." + std::to_wstring(GetCurrentProcessId()) + L".bak";
    return CopyFileW(path.c_str(), snapshot.c_str(), TRUE) != FALSE;
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
// A %COMMANDER_PATH% macro is safe in wincmd.ini but NOT inside a TC
// button's parameter string: TC scans %C before the environment variable.
// Only fully-qualified plugin paths are safe here.
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
// Recognize *both* Windows loader paths. A profile can be shared between
// x86 (SysWOW64\rundll32.exe) and x64 (System32\rundll32.exe) TC.
// Comparing against only the current process host would reject our own
// button on the next architecture switch.
bool IsSystemRundll32(const std::wstring& cmd) {
    wchar_t root[32768]{};
    const auto n = GetWindowsDirectoryW(root, 32768);
    if (!n || n >= 32768) return false;
    const std::wstring prefix(root, n);
    for (const wchar_t* folder : {L"System32", L"SysWOW64"}) {
        const auto expected = Join(Join(prefix, folder), L"rundll32.exe");
        if (_wcsicmp(expected.c_str(), cmd.c_str()) == 0) return true;
    }
    return false;
}

bool OwnWdxName(std::wstring_view path) {
    const auto sep = path.find_last_of(L"\\/");
    if (sep == std::wstring_view::npos || sep + 1 >= path.size()) return false;
    const std::wstring leaf(path.substr(sep + 1));
    return _wcsicmp(leaf.c_str(), L"SymlinkInspector.wdx") == 0 ||
           _wcsicmp(leaf.c_str(), L"SymlinkInspector.wdx64") == 0;
}

int CountRegisteredWdx(const std::wstring& ini) {
    // One [ContentPlugins] numeric entry denotes one logical plugin.
    // A same-number [ContentPlugins64] flag is not a second registration.
    std::vector<wchar_t> values(262144, L'\0');
    const DWORD n = GetPrivateProfileSectionW(L"ContentPlugins", values.data(),
                       static_cast<DWORD>(values.size()), ini.c_str());
    if (!n || n >= values.size() - 2) return 0; // fail closed on truncated INI
    int count = 0;
    for (const wchar_t* p = values.data(); *p; p += wcslen(p) + 1) {
        const wchar_t* equal = wcschr(p, L'=');
        if (!equal || equal == p) continue;
        bool numeric = true;
        for (const wchar_t* key = p; key != equal; ++key)
            if (*key < L'0' || *key > L'9') { numeric = false; break; }
        if (numeric && OwnWdxName(equal + 1)) ++count;
    }
    return count;
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
int DuplicateRegistrationCount(const std::wstring& ini) {
    return CountRegisteredWdx(ini);
}

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
    const bool implicitDefaultBar = barValue.empty();
    // An absent Buttonbar= setting means TC starts with DEFAULT.BAR.
    // NEVER substitute a copied toolbar into wincmd.ini; this previously hid
    // a user's custom buttons and broke profiles with an OPENBAR override.
    // If the real DEFAULT.BAR isn't writable, decline rather than switch.
    const std::wstring sourceBar = implicitDefaultBar ? Join(tcDir, L"DEFAULT.BAR") :
        Resolve(barValue, iniDir, tcDir);
    if (!IsFile(sourceBar)) {
        status = implicitDefaultBar ?
            L"Total Commander uses its default toolbar, but DEFAULT.BAR was not found. "
            L"No settings changed." :
            L"The configured TC .bar file could not be found. No settings changed.";
        return false;
    }
    const std::wstring barFile = sourceBar;
    // Never make the old 0.2.1 or 0.2.4 shadow bars permanent. Recovery
    // must first restore the original startup selection from an old backup.
    if (_wcsicmp(sourceBar.c_str(), Join(iniDir, L"TCSymbolicLinks.bar").c_str()) == 0 ||
        _wcsicmp(sourceBar.c_str(), Join(iniDir, L"TC-Symbolic-Links-Default.bar").c_str()) == 0) {
        status = L"An older installer replaced your startup toolbar. "
                 L"Use Restore-TC-Toolbar-Only.ps1 to recover your original "
                 L"Buttonbar setting before installing this version. "
                 L"No settings were changed.";
        return false;
    }
    const auto columnsIni = ResolveCustomFieldsIni(tcIni, tcDir);
    if (columnsIni.empty() || !IsFile(columnsIni)) {
        status = L"CustomFields configuration could not be resolved safely.";
        return false;
    }
    // Do not use %COMMANDER_PATH% or %COMMANDER_INI_PATH% here. Total
    // Commander interprets the leading %C as its current-item macro.
    // This was reproduced from a real 0.2.4 user screenshot.
    if (dllPath.find(L'%') != std::wstring::npos ||
        (dllPath.find(L':') != 1 && dllPath.rfind(L"\\\\", 0) != 0)) {
        status = L"The WDX path must be a full path without percent signs. "
                 L"Refusing to create an unsafe Total Commander button.";
        return false;
    }
    const auto& actualDll = dllPath;
    // IMPORTANT: GetPrivateProfileStringW removes the *outer* quote pair if
    // an INI value both starts and ends with '"'. Our command MUST keep the
    // quotes around the DLL and the target directory, otherwise rundll32 can
    // silently open an error dialog instead of our Maker window. Finish with
    // an ignored but harmless marker so the final character is not a quote.
    // RunMakerFromArguments deliberately ignores unknown --tc-launch tokens.
    std::array<std::wstring, 3> arguments;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        arguments[i] = L"\"" + actualDll +
            L"\",TCLinkCreate --list \"%WL\" --dest \"%T.\"" +
            kModeArguments[i] + L" --tc-launch";
        if (!AcpRoundTrip(arguments[i])) {
            status = L"This plugin path is not representable in the active bar's INI encoding. "
                     L"Install the WDX under COMMANDER_PATH or the TC INI folder.";
            return false;
        }
    }
     wchar_t systemDir[32768]{};
    const UINT sysLen = GetSystemDirectoryW(systemDir, 32768);
    if (!sysLen || sysLen >= 32768) { status = L"Cannot find system rundll32.exe."; return false; }
    const auto host = Join(systemDir, L"rundll32.exe");
    if (!IsFile(host)) { status = L"System rundll32.exe was not found."; return false; }
    const auto rawCount = ReadIni(sourceBar, L"Buttonbar", L"Buttoncount");
    if (rawCount.empty()) { status = L"Startup .bar lacks a valid Buttoncount."; return false; }
    wchar_t* last = nullptr;
    long count = wcstol(rawCount.c_str(), &last, 10);
    if (!last || *last != 0 || count < 0 || count >= 10000) {
        status = L"Active button count is invalid or too large.";
        return false;
    }
    std::array<int, 3> slots{};
    for (int i = 1; i <= count; ++i) {
        const auto n = std::to_wstring(i);
        const auto cmd = ReadIni(sourceBar, L"Buttonbar", (L"cmd" + n).c_str());
        const auto menu = ReadIni(sourceBar, L"Buttonbar", (L"menu" + n).c_str());
        for (std::size_t mode = 0; mode < slots.size(); ++mode) {
            if (menu != kButtonLabels[mode]) continue;
            const auto param = ReadIni(sourceBar, L"Buttonbar", (L"param" + n).c_str());
            const bool legacy = mode == 0 &&
                _wcsicmp(cmd.c_str(), L"em_TCSymbolicLinks_Create") == 0;
            const bool thisWdx = IsSystemRundll32(cmd) &&
                param.find(L"SymlinkInspector.wdx") != std::wstring::npos &&
                param.find(L",TCLinkCreate ") != std::wstring::npos;
            const bool matchingMode = mode == 0 ?
                (param.find(L"--mode file") == std::wstring::npos &&
                 param.find(L"--mode directory") == std::wstring::npos) :
                param.find(kModeArguments[mode]) != std::wstring::npos;
            if (!legacy && !(thisWdx && matchingMode)) {
                status = L"A toolbar button has a plugin label but does not belong "
                         L"to this WDX mode. No settings changed.";
                return false;
            }
            // Duplicate legacy entries are preserved, not overwritten or
            // deleted. We repair only the first recognized plugin entry.
            if (!slots[mode]) slots[mode] = i;
        }
    }
    int missing = 0;
    for (int slot : slots) if (!slot) ++missing;
    if (count + missing >= 10000) {
        status = L"Not enough free slots in the existing toolbar.";
        return false;
    }
    if (!CanOpenForWrite(barFile) || !CanOpenForWrite(tcIni) ||
        (columnsIni != tcIni && !CanOpenForWrite(columnsIni))) {
        status = implicitDefaultBar ?
            L"TC uses DEFAULT.BAR, but it is not writable. The installer "
            L"will NOT switch your toolbar or remove your custom icons. "
            L"Choose a writable personal .bar in TC Configuration first." :
            L"Active TC configuration is read-only or in use. No changes made.";
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
    int nextSlot = static_cast<int>(count);
    bool ok = true;
    for (std::size_t mode = 0; mode < slots.size() && ok; ++mode) {
        const int targetSlot = slots[mode] ? slots[mode] : ++nextSlot;
        const auto number = std::to_wstring(targetSlot);
        ok = WriteIni(barFile, L"Buttonbar", L"cmd" + number, host) &&
             WriteIni(barFile, L"Buttonbar", L"param" + number, arguments[mode]) &&
             WriteIni(barFile, L"Buttonbar", L"button" + number,
                      actualDll + L"," + std::to_wstring(mode)) &&
             WriteIni(barFile, L"Buttonbar", L"menu" + number, kButtonLabels[mode]);
        // Verify the serialized command before continuing to the next mode.
        if (ok && ReadIni(barFile, L"Buttonbar", (L"param" + number).c_str()) != arguments[mode])
            ok = false;
    }
    if (ok && nextSlot != count)
        ok = WriteIni(barFile, L"Buttonbar", L"Buttoncount", std::to_wstring(nextSlot));
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
    // NEVER write [Buttonbar] Buttonbar in any successful install. It is a
    // user preference and WDX has no API to identify a temporary active bar.
    if (!ok) {
        status = rollback() ? L"Configuration failed; original files were restored in place." :
            L"Configuration failed AND rollback was incomplete. Consult .tcslinks.bak files.";
        return false;
    }
    status = implicitDefaultBar ?
        L"Three symlink buttons (Auto / File / Directory) were configured in "
        L"the writable DEFAULT.BAR without switching toolbars. An original "
        L"and pre-update backup exist; WDX columns are enabled. Restart TC." :
        L"Three symlink buttons (Auto / File / Directory) were configured "
        L"in the existing startup bar. The toolbar selection and usercmd.ini "
        L"were NOT changed. WDX columns are enabled. Restart TC.";
    return true;
}

void AutoConfigureOnFirstLoad(const char* suggestedIniPath) {
    if (g_attempted.test_and_set()) return;
    wchar_t exe[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
    if (!n || n >= 32768) return;
    const auto exeName = std::wstring(exe).substr(std::wstring(exe).find_last_of(L"\\/") + 1);
    const bool is64 = _wcsicmp(exeName.c_str(), L"TOTALCMD64.EXE") == 0;
    const bool is32 = _wcsicmp(exeName.c_str(), L"TOTALCMD.EXE") == 0;
    if (!is64 && !is32) return;
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
    const int registeredCopies = DuplicateRegistrationCount(ini);
    if (registeredCopies > 1 &&
        GetPrivateProfileIntW(L"Setup", L"DuplicateRepairDismissed", 0,
                              marker.c_str()) != registeredCopies) {
        const HWND owner = FindWindowW(L"TTOTAL_CMD", nullptr);
        const LANGID language = GetUserDefaultUILanguage();
        const bool ru = PRIMARYLANGID(language) == LANG_RUSSIAN;
        const bool he = PRIMARYLANGID(language) == LANG_HEBREW;
        const wchar_t* prompt = he ?
            L"נמצאו מספר רישומים של SymlinkInspector. לבצע בדיקה ותיקון "
            L"לאחר סגירת Total Commander? יישמר גיבוי. אם הסדר מורכב, "
            L"לא יבוצע שום שינוי." :
            ru ?
            L"Найдены повторные регистрации SymlinkInspector в [ContentPlugins]. "
            L"Проверить и исправить после закрытия всех окон Total Commander? "
            L"Будет создана резервная копия wincmd.ini. Если исправление "
            L"может затронуть другие плагины, настройки останутся без изменений." :
            L"Multiple SymlinkInspector registrations found in [ContentPlugins]. "
            L"Check and repair after all Total Commander windows close? "
            L"wincmd.ini will be backed up. Ambiguous layouts will not be changed.";
        if (MessageBoxW(owner, prompt, L"TC Symbolic Links",
                        MB_YESNO | MB_ICONWARNING) == IDYES) {
            HMODULE own = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&AutoConfigureOnFirstLoad), &own))
                tcs::update::StartRegistrationRepair(own, ini, owner);
        } else {
            WriteIni(marker, L"Setup", L"DuplicateRepairDismissed",
                     std::to_wstring(registeredCopies));
        }
    }
    // Installers select one WDX architecture; setup remains architecture-
    // specific because TC x86 and x64 can share a user profile or toolbar.
    const wchar_t* completedKey = is64 ? L"Completed" : L"Completed32";
    const wchar_t* versionKey = is64 ? L"Version" : L"Version32";
    const wchar_t* declinedKey = is64 ? L"Declined" : L"Declined32";
    const int currentArchitecture = is64 ? 64 : 32;
    const bool matchingLauncher = GetPrivateProfileIntW(
        L"Setup", L"LastConfiguredArchitecture", 0, marker.c_str()) == currentArchitecture;
    // TC x86 and x64 may share one .bar file. The button always points to
    // one actual DLL; prompt for a safe rebind on architecture switches.
    if ((matchingLauncher &&
         GetPrivateProfileIntW(L"Setup", completedKey, 0, marker.c_str()) &&
         GetPrivateProfileIntW(L"Setup", versionKey, 0, marker.c_str()) >= 216) ||
        GetPrivateProfileIntW(L"Setup", declinedKey, 0, marker.c_str())) return;
    const HWND owner = FindWindowW(L"TTOTAL_CMD", nullptr);
    const LANGID lang = GetUserDefaultUILanguage();
    const bool ru = PRIMARYLANGID(lang) == LANG_RUSSIAN;
    const bool he = PRIMARYLANGID(lang) == LANG_HEBREW;
    const wchar_t* prompt = he ?
        L"להוסיף שלושה כפתורי קישורים (אוטומטי/קובץ/תיקייה) לסרגל הקיים? "
        L"ההגדרות והכפתורים הקיימים יגובו." :
        ru ? L"Добавить три кнопки (универсальная / файл / папка) в стартовую панель, "
             L"не заменяя её? Сохраним резервные копии кнопок и настроек." :
             L"Add three symlink buttons (Auto / File / Directory) to the existing "
             L"startup toolbar without replacing it? Backups will be preserved.";
    if (MessageBoxW(owner, prompt, L"TC Symbolic Links", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        WriteIni(marker, L"Setup", declinedKey, L"1");
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
        WriteIni(marker, L"Setup", completedKey, L"1");
        WriteIni(marker, L"Setup", versionKey, L"216");
        WriteIni(marker, L"Setup", L"LastConfiguredArchitecture",
                 is64 ? L"64" : L"32");
    }
    MessageBoxW(owner, status.c_str(), L"TC Symbolic Links", MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
}
} // namespace tcs
