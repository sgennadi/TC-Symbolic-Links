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
        Check(argc == 2, "Pass built Windows WDX path");
        const fs::path dll = argv[1];
        Check(fs::is_regular_file(dll), "Expected SymlinkInspector.wdx64");
        wchar_t tempBase[MAX_PATH + 1]{};
        Check(GetTempPathW(MAX_PATH + 1, tempBase) > 0, "GetTempPathW failed");
        wchar_t uniqueFile[MAX_PATH + 1]{};
        Check(GetTempFileNameW(tempBase, L"TSD", 0, uniqueFile) != 0, "GetTempFileNameW failed");
        Check(DeleteFileW(uniqueFile) != FALSE, "DeleteFileW failed");
        TempDir root{fs::path(uniqueFile)};
        const auto tcDir = root.folder / L"TotalCommander";
        const auto configDir = root.folder / L"Settings עברית";
        fs::create_directories(tcDir);
        fs::create_directories(configDir);
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
        ReadOnly protectedBar(defaultBar);
        const auto ini = configDir / L"wincmd.ini";
        const auto usercmd = configDir / L"usercmd.ini";
        const std::string existingCmd = "[em_Custom]\r\ncmd=explorer.exe\r\nparam=C:\\\r\n";
        Write(usercmd, existingCmd);
        const std::string initialIni =
            "[Configuration]\r\nSomeUserSetting=untouched\r\n"
            "[CustomFields]\r\nTitles=Personal|Media\r\nContents1=[=tc.name]\r\n"
            "[left]\r\nSpecialView=1\r\n"
            "[right]\r\nSpecialView=2\r\n";
        // The key reported by the user is absent: no [Buttonbar] section at all.
        Write(ini, initialIni);
        std::wstring status;
        Check(tcs::ConfigureForTotalCommander(ini.wstring(), dll.wstring(), status),
              "Installing with implicit DEFAULT.BAR was rejected");
        const auto copy = configDir / L"TC-Symbolic-Links-Default.bar";
        Check(fs::exists(copy), "No user-owned copy of default toolbar created");
        Check(Ini(ini, L"Buttonbar", L"Buttonbar") ==
              L"%COMMANDER_INI_PATH%\\TC-Symbolic-Links-Default.bar",
              "Clone was not activated via TC's portable INI path variable");
        Check(Read(defaultBar) == oldButtons, "REGRESSION: global DEFAULT.BAR was modified");
        Check((GetFileAttributesW(defaultBar.wstring().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
              "REGRESSION: global DEFAULT.BAR access flags changed");
        Check(Ini(copy, L"Buttonbar", L"Buttoncount") == L"4", "Wrong button count in clone");
        Check(Ini(copy, L"Buttonbar", L"cmd1") == L"cm_Copy", "Copy button missing");
        Check(Ini(copy, L"Buttonbar", L"cmd2") == L"cm_Delete", "Delete button missing");
        Check(Ini(copy, L"Buttonbar", L"menu3") == L"My own subbar", "Personal subbar missing");
        Check(Ini(copy, L"Buttonbar", L"button3") == L"WCMICONS.DLL,3", "Personal icon changed");
        Check(Ini(copy, L"CustomOptions", L"Important") == L"keep me", "Other .bar settings changed");
        Check(Ini(copy, L"Buttonbar", L"cmd4").find(L"\\rundll32.exe") != std::wstring::npos,
              "Create Links button command missing");
        const auto params = Ini(copy, L"Buttonbar", L"param4");
        Check(!params.empty() && params.front() == L'"' &&
              params.find(L",TCLinkCreate --list") != std::wstring::npos &&
              params.ends_with(L" --tc-launch"),
              "Rundll32 DLL path was unquoted by the INI reader");
        // Exercise the exact DLL-path prefix written into the cloned button,
        // using Windows rundll32, without showing a GUI in the test runner.
        const auto creatorAt = params.find(L",TCLinkCreate ");
        Check(creatorAt != std::wstring::npos, "Cannot extract stored DLL prefix");
        const auto marker = configDir / L"מבחן בדיקת הקישור.txt";
        const std::wstring rundll = Ini(copy, L"Buttonbar", L"cmd4");
        const std::wstring probeArgs = params.substr(0, creatorAt) +
            L",TCLinkProbe \"" + marker.wstring() + L"\"";
        std::wstring probeLine = L"\"" + rundll + L"\" " + probeArgs;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION proc{};
        Check(CreateProcessW(rundll.c_str(), probeLine.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup, &proc) != FALSE,
              "Cannot invoke saved default-bar button using rundll32");
        CloseHandle(proc.hThread);
        const DWORD waited = WaitForSingleObject(proc.hProcess, 12000);
        if (waited != WAIT_OBJECT_0) {
            TerminateProcess(proc.hProcess, 1);
            WaitForSingleObject(proc.hProcess, 1500);
        }
        CloseHandle(proc.hProcess);
        Check(waited == WAIT_OBJECT_0 && fs::is_regular_file(marker),
              "Cloned default-bar WDX module was not loaded by saved command");
        Check(Read(usercmd) == existingCmd, "REGRESSION: unrelated usercmd.ini edited");
        Check(Ini(ini, L"Configuration", L"SomeUserSetting") == L"untouched",
              "REGRESSION: unrelated wincmd.ini values lost");
        Check(Ini(ini, L"CustomFields", L"Titles") == L"Personal|Media|TC Symbolic Links",
              "Previous custom column groups lost");
        Check(Ini(ini, L"left", L"SpecialView") == L"5" &&
              Ini(ini, L"right", L"SpecialView") == L"5", "Columns not enabled in both panels");
        Check(Read(ini.wstring() + L".tcslinks.bak") == initialIni,
              "Original wincmd.ini was not backed up intact");
        Check(!fs::exists(defaultBar.wstring() + L".tcslinks.bak"),
              "Global default was modified or backed up unnecessarily");
        Check(tcs::ConfigureForTotalCommander(ini.wstring(), dll.wstring(), status),
              "Installing again after default bar was cloned must succeed");
        Check(Ini(copy, L"Buttonbar", L"Buttoncount") == L"4", "Duplicate button after second install");
        Check(Read(defaultBar) == oldButtons, "Default bar mutated after repeat install");
        // Collision safety: Never overwrite an unrelated local BAR with the
        // destination name if the user still has an implicit default setting.
        const auto collisionDir = root.folder / L"Collision";
        fs::create_directories(collisionDir);
        const auto collisionIni = collisionDir / L"wincmd.ini";
        const auto collisionBar = collisionDir / L"TC-Symbolic-Links-Default.bar";
        Write(collisionIni, initialIni);
        Write(collisionBar, "my unrelated existing toolbar");
        Check(!tcs::ConfigureForTotalCommander(collisionIni.wstring(), dll.wstring(), status),
              "Installer replaced pre-existing same-name file");
        Check(Read(collisionIni) == initialIni && Read(collisionBar) == "my unrelated existing toolbar",
              "Configuration changed despite file collision");
        // Missing global DEFAULT.BAR: fail safely without writing to profile.
        const auto missingTC = root.folder / L"EmptyTcInstallation";
        fs::create_directories(missingTC);
        Check(SetEnvironmentVariableW(L"COMMANDER_PATH", missingTC.wstring().c_str()) != FALSE,
              "Cannot simulate missing default toolbar");
        const auto missingDir = root.folder / L"NoDefaultBar";
        fs::create_directories(missingDir);
        const auto missingIni = missingDir / L"wincmd.ini";
        Write(missingIni, initialIni);
        Check(!tcs::ConfigureForTotalCommander(missingIni.wstring(), dll.wstring(), status),
              "Missing DEFAULT.BAR was incorrectly accepted");
        Check(Read(missingIni) == initialIni, "Missing-default failure changed settings");
        std::cout << "PASS: implicit DEFAULT.BAR, read-only global bar, cloned buttons/icons/subbar, "
                     "Hebrew profile, columns, backups, collision refusal, missing-source refusal, idempotence\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
