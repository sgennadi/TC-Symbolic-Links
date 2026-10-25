// Regression for real Total Commander profiles with no [Buttonbar] Buttonbar.
// Those profiles use <COMMANDER_PATH>\DEFAULT.BAR at startup. The installer
// must preserve every original button, even when DEFAULT.BAR is read-only.
#include "TcIntegration.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
namespace fs = std::filesystem;
namespace {
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void Write(const fs::path& p, const std::string& content) {
    std::ofstream file(p, std::ios::binary);
    Check(static_cast<bool>(file), "Cannot create TC fixture file");
    file << content;
    Check(static_cast<bool>(file), "Cannot write TC fixture file");
}
std::string Read(const fs::path& p) {
    std::ifstream file(p, std::ios::binary);
    Check(static_cast<bool>(file), "Cannot read TC fixture file");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::wstring Ini(const fs::path& file, const wchar_t* section, const wchar_t* key) {
    wchar_t value[16384]{};
    GetPrivateProfileStringW(section, key, L"", value, 16384, file.wstring().c_str());
    return value;
}
struct TempDir {
    fs::path folder;
    ~TempDir() { std::error_code error; fs::remove_all(folder, error); }
};
struct EnvVar {
    std::wstring key;
    std::wstring before;
    bool existed;
    EnvVar(const wchar_t* name, const std::wstring& value): key(name) {
        DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        existed = needed > 0;
        if (needed > 0) {
            before.resize(needed);
            const DWORD got = GetEnvironmentVariableW(name, before.data(), needed);
            Check(got > 0 && got < needed, "GetEnvironmentVariableW failed");
            before.resize(got);
        }
        Check(SetEnvironmentVariableW(name, value.c_str()) != FALSE, "SetEnvironmentVariableW failed");
    }
    ~EnvVar() { SetEnvironmentVariableW(key.c_str(), existed ? before.c_str() : nullptr); }
};
struct ReadOnly {
    std::wstring file;
    DWORD attrs;
    explicit ReadOnly(const fs::path& path): file(path.wstring()) {
        attrs = GetFileAttributesW(file.c_str());
        Check(attrs != INVALID_FILE_ATTRIBUTES, "Failed to get original bar attributes");
        Check(SetFileAttributesW(file.c_str(), attrs | FILE_ATTRIBUTE_READONLY) != FALSE,
              "Cannot make original DEFAULT.BAR read-only");
    }
    ~ReadOnly() { SetFileAttributesW(file.c_str(), attrs); }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "Pass the built Windows WDX path");
        const fs::path compiledDll = argv[1];
        Check(fs::is_regular_file(compiledDll), "Missing SymlinkInspector.wdx64");
        wchar_t temporary[MAX_PATH + 1]{};
        Check(GetTempPathW(MAX_PATH + 1, temporary) > 0, "GetTempPathW failed");
        wchar_t candidate[MAX_PATH + 1]{};
        Check(GetTempFileNameW(temporary, L"TSD", 0, candidate) != 0, "GetTempFileNameW failed");
        Check(DeleteFileW(candidate) != FALSE, "DeleteFileW failed");
        TempDir root{fs::path(candidate)};
        const auto tcDir = root.folder / L"TotalCommander";
        const auto configDir = root.folder / L"Settings";
        const auto plugDir = tcDir / L"plugins" / L"wdx" / L"TCSymbolicLinks";
        fs::create_directories(plugDir);
        fs::create_directories(configDir);
        const auto dll = plugDir / L"SymlinkInspector.wdx64";
        fs::copy_file(compiledDll, dll);
        EnvVar tcPath(L"COMMANDER_PATH", tcDir.wstring());
        const auto defaultBar = tcDir / L"DEFAULT.BAR";
        const std::string oldButtons =
            "[Buttonbar]\r\nButtoncount=3\r\n"
            "cmd1=cm_Copy\r\nbutton1=WCMICONS.DLL,1\r\nmenu1=Copy\r\n"
            "cmd2=cm_Delete\r\nbutton2=WCMICONS.DLL,2\r\nmenu2=Delete\r\n"
            "cmd3=%COMMANDER_PATH%\\Office-Shortcuts.bar\r\n"
            "button3=WCMICONS.DLL,3\r\nmenu3=My own subbar\r\n"
            "[CustomOptions]\r\nImportant=keep me\r\n";
        Write(defaultBar, oldButtons);
        const auto ini = configDir / L"wincmd.ini";
        const auto usercmd = configDir / L"usercmd.ini";
        const std::string initialIni =
            "[Configuration]\r\nSomeUserSetting=untouched\r\n"
            "[CustomFields]\r\nTitles=Personal|Media\r\nContents1=[=tc.name]\r\n"
            "[left]\r\nSpecialView=1\r\n"
            "[right]\r\nSpecialView=2\r\n";
        const std::string oldUsercmd = "[em_Custom]\r\ncmd=notepad.exe\r\nparam=hello\r\n";
        Write(ini, initialIni);
        Write(usercmd, oldUsercmd);
        std::wstring status;
        {
            ReadOnly locked(defaultBar);
            Check(!tcs::ConfigureForTotalCommander(ini.wstring(), dll.wstring(), status),
                  "Read-only DEFAULT.BAR MUST refuse rather than switch the user's toolbar");
            Check(Read(ini) == initialIni && Read(defaultBar) == oldButtons,
                  "Refusal must not modify settings or original toolbar");
            Check(!fs::exists(configDir / L"TC-Symbolic-Links-Default.bar"),
                  "Refusal incorrectly created a shadow replacement toolbar");
        }
        Check(tcs::ConfigureForTotalCommander(ini.wstring(), dll.wstring(), status),
              "Could not append to writable DEFAULT.BAR in place");
        // CRITICAL: NEVER set or replace the startup toolbar selection.
        Check(Ini(ini, L"Buttonbar", L"Buttonbar").empty(),
              "Installer replaced default startup bar: user icons may disappear");
        Check(!fs::exists(configDir / L"TC-Symbolic-Links-Default.bar"),
              "Installer created an unrequested shadow toolbar");
        Check(Ini(defaultBar, L"Buttonbar", L"Buttoncount") == L"4",
              "Expected one button added to actual DEFAULT.BAR");
        Check(Ini(defaultBar, L"Buttonbar", L"cmd1") == L"cm_Copy" &&
              Ini(defaultBar, L"Buttonbar", L"button1") == L"WCMICONS.DLL,1",
              "Copy button/icon changed");
        Check(Ini(defaultBar, L"Buttonbar", L"cmd2") == L"cm_Delete" &&
              Ini(defaultBar, L"Buttonbar", L"button2") == L"WCMICONS.DLL,2",
              "Delete button/icon changed");
        Check(Ini(defaultBar, L"Buttonbar", L"menu3") == L"My own subbar" &&
              Ini(defaultBar, L"Buttonbar", L"button3") == L"WCMICONS.DLL,3",
              "Custom subbar/icon lost");
        Check(Ini(defaultBar, L"CustomOptions", L"Important") == L"keep me",
              "Unrelated buttonbar settings changed");
        Check(Read(usercmd) == oldUsercmd, "usercmd.ini modified");
        Check(Read(defaultBar.wstring() + L".tcslinks.bak") == oldButtons,
              "DEFAULT.BAR pre-change backup missing or incorrect");
        Check(Read(ini.wstring() + L".tcslinks.bak") == initialIni,
              "INI pre-change backup missing or incorrect");
        Check(Ini(ini, L"CustomFields", L"Titles") == L"Personal|Media|TC Symbolic Links",
              "Existing custom column sets lost");
        Check(Ini(ini, L"left", L"SpecialView") == L"5" &&
              Ini(ini, L"right", L"SpecialView") == L"5",
              "WDX columns were not enabled in both panels");
        const std::wstring params = Ini(defaultBar, L"Buttonbar", L"param4");
        const auto exportAt = params.find(L",TCLinkCreate ");
        Check(!params.empty() && params.front() == L'"' && exportAt != std::wstring::npos &&
              params.ends_with(L" --tc-launch"), "Malformed persisted WDX command");
        Check(params.find(dll.wstring()) != std::wstring::npos,
              "Installer did not write the ABSOLUTE installed WDX path");
        Check(params.find(L"%COMMANDER_PATH%") == std::wstring::npos &&
              params.find(L"%COMMANDER_INI_PATH%") == std::wstring::npos,
              "Regression: TC expands %C in %COMMANDER_PATH% as the selected file name");
        // Run the exact persisted WDX prefix through Windows rundll32.exe.
        const auto marker = configDir / L"wdx-probe.txt";
        const std::wstring host = Ini(defaultBar, L"Buttonbar", L"cmd4");
        const std::wstring probeParams = params.substr(0, exportAt) +
            L",TCLinkProbe \"" + marker.wstring() + L"\"";
        std::wstring cmdline = L"\"" + host + L"\" " + probeParams;
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        Check(CreateProcessW(host.c_str(), cmdline.data(), nullptr, nullptr,
              FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE,
              "Could not spawn saved Create Links host command");
        CloseHandle(pi.hThread);
        const DWORD wait = WaitForSingleObject(pi.hProcess, 12000);
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
        }
        CloseHandle(pi.hProcess);
        Check(wait == WAIT_OBJECT_0 && fs::exists(marker),
              "Saved button could not load the DLL from Total Commander plugins dir");
        Check(tcs::ConfigureForTotalCommander(ini.wstring(), dll.wstring(), status),
              "Idempotent reconfiguration failed");
        Check(Ini(defaultBar, L"Buttonbar", L"Buttoncount") == L"4" &&
              Ini(ini, L"Buttonbar", L"Buttonbar").empty(),
              "Reinstall duplicated buttons or switched toolbar");
        Check(Read(usercmd) == oldUsercmd, "Reinstall modified user commands");
        // Previous versions' hidden/clone bars MUST be refused until restored.
        for (const auto* clone : {L"TCSymbolicLinks.bar", L"TC-Symbolic-Links-Default.bar"}) {
            const auto otherIni = configDir / (std::wstring(L"wincmd-") + clone + L".ini");
            // Must have basename wincmd.ini? ConfigureForTotalCommander uses any INI.
            const auto brokenBar = configDir / clone;
            Write(brokenBar, "[Buttonbar]\r\nButtoncount=1\r\ncmd1=cm_Copy\r\n");
            Write(otherIni, "[Configuration]\r\nkeep=YES\r\n");
            Check(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar",
                  brokenBar.wstring().c_str(), otherIni.wstring().c_str()) != FALSE,
                  "Cannot create historical corrupted toolbar fixture");
            const auto before = Read(otherIni);
            Check(!tcs::ConfigureForTotalCommander(otherIni.wstring(), dll.wstring(), status),
                  "Legacy shadow toolbar was modified instead of refused");
            Check(Read(otherIni) == before, "Refusal changed historical profile");
        }
        const auto missingDir = root.folder / L"MissingTC";
        fs::create_directories(missingDir);
        Check(SetEnvironmentVariableW(L"COMMANDER_PATH", missingDir.wstring().c_str()) != FALSE,
              "Cannot simulate missing default toolbar");
        const auto missingIni = configDir / L"missing-wincmd.ini";
        Write(missingIni, initialIni);
        Check(!tcs::ConfigureForTotalCommander(missingIni.wstring(), dll.wstring(), status),
              "Missing DEFAULT.BAR was accepted");
        Check(Read(missingIni) == initialIni, "Missing default bar changed INI");
        std::cout << "PASS: no toolbar replacement; read-only refusal; writable DEFAULT.BAR "
                     "in-place edit; icons and subbar intact; absolute DLL path in TC install; "
                     "actual rundll32 probe; backups; columns; legacy clone refusal\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
