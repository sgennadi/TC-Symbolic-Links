#include "TcIntegration.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
namespace fs = std::filesystem;
namespace {
void Require(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
void WriteText(const fs::path& path, std::string text) {
    std::ofstream o(path, std::ios::binary);
    Require(static_cast<bool>(o), "open test file");
    o << text;
    Require(static_cast<bool>(o), "write test file");
}
std::wstring Ini(const std::wstring& file, const wchar_t* section, const wchar_t* key) {
    wchar_t out[16384]{};
    GetPrivateProfileStringW(section, key, L"", out, 16384, file.c_str());
    return out;
}
struct TempFolder {
    fs::path value;
    ~TempFolder() { std::error_code ec; fs::remove_all(value, ec); }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Pass WDX module path");
        const fs::path module = argv[1];
        Require(fs::exists(module), "WDX module not built");
        wchar_t tmp[MAX_PATH+1]{};
        Require(GetTempPathW(MAX_PATH+1, tmp) > 0, "GetTempPath failed");
        wchar_t tempFilename[MAX_PATH+1]{};
        Require(GetTempFileNameW(tmp, L"TSL", 0, tempFilename) != 0, "GetTempFileName failed");
        Require(DeleteFileW(tempFilename), "DeleteFile failed");
        TempFolder root{fs::path(tempFilename)};
        fs::create_directories(root.value);
        const auto local = root.value / L"עברית עם ניקוד שָלוֹם";
        fs::create_directories(local);
        const auto ini = local / L"wincmd.ini";
        const auto bar = local / L"oldbuttons.bar";
        WriteText(bar, "[Buttonbar]\r\nButtoncount=2\r\ncmd1=cm_Copy\r\nbutton1=WCMICONS.DLL,1\r\nmenu1=Copy\r\ncmd2=cm_Delete\r\nmenu2=Delete\r\n");
        WriteText(ini, "[CustomFields]\r\nTitles=Music|Photos\r\nHeaders1=Foo\r\nContents1=[=tc.name]\r\n[Buttonbar]\r\nButtonbar=\r\n[left]\r\nSpecialView=1\r\n[right]\r\nSpecialView=2\r\n");
        // TC's ASCII built-in variable prevents loss of Hebrew folder names
        // when the user's existing wincmd.ini is ANSI.
        Require(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar",
                L"%COMMANDER_INI_PATH%\\oldbuttons.bar", ini.c_str()), "set portable bar path");
        std::wstring message;
        Require(tcs::ConfigureForTotalCommander(ini.wstring(), module.wstring(), message), "first install failed");
        Require(Ini(ini.wstring(), L"CustomFields", L"Titles") == L"Music|Photos|TC Symbolic Links",
                "column names not appended correctly");
        Require(Ini(ini.wstring(), L"CustomFields", L"Contents3").find(L"SymlinkInspector.Link target") != std::wstring::npos,
                "WDX contents definition not configured");
        Require(Ini(ini.wstring(), L"left", L"SpecialView") == L"5" &&
                Ini(ini.wstring(), L"right", L"SpecialView") == L"5", "wrong TC SpecialView offset (preset #3 must be view #5)");
        Require(Ini(ini.wstring(), L"left", L"ShowAllDetails") == L"1" &&
                Ini(ini.wstring(), L"right", L"ShowAllDetails") == L"1", "panels do not use detailed view");
        Require(Ini(ini.wstring(), L"CustomFields", L"Headers1") == L"Foo", "overwrote pre-existing column set");
        const auto newBar = local / L"TCSymbolicLinks.bar";
        Require(Ini(newBar.wstring(), L"Buttonbar", L"Buttoncount") == L"3", "toolbar count mismatch");
        Require(Ini(newBar.wstring(), L"Buttonbar", L"cmd1") == L"cm_Copy", "old toolbar changed");
        Require(Ini(newBar.wstring(), L"Buttonbar", L"cmd3") == L"em_TCSymbolicLinks_Create", "new button missing");
        Require(Ini(ini.wstring(), L"Buttonbar", L"Buttonbar") ==
                L"%COMMANDER_INI_PATH%\\TCSymbolicLinks.bar", "toolbar not switched");
        const auto userCmd = local / L"usercmd.ini";
        Require(Ini(userCmd.wstring(), L"em_TCSymbolicLinks_Create", L"cmd").find(L"\\rundll32.exe") != std::wstring::npos,
                "the user command does not target the trusted system rundll32.exe");
        auto params = Ini(userCmd.wstring(), L"em_TCSymbolicLinks_Create", L"param");
        Require(params.find(module.wstring()) != std::wstring::npos, "WDX path not in command");
        Require(params.find(L"%WL") != std::wstring::npos && params.find(L"%T.") != std::wstring::npos,
                "TC substitution variables missing");
        Require(fs::exists(ini.wstring() + L".tcslinks.bak"), "no wincmd.ini backup");
        Require(fs::exists(bar), "existing toolbar damaged");
        Require(tcs::ConfigureForTotalCommander(ini.wstring(), module.wstring(), message), "repeat install failed");
        Require(Ini(newBar.wstring(), L"Buttonbar", L"Buttoncount") == L"3", "duplicate toolbar button added");
        Require(Ini(ini.wstring(), L"CustomFields", L"Titles") == L"Music|Photos|TC Symbolic Links",
                "duplicated column title");
        Require(Ini(ini.wstring(), L"left", L"SpecialView") == L"5" &&
                Ini(ini.wstring(), L"right", L"SpecialView") == L"5", "wrong view after repeat setup");

        const auto conflictDir = root.value / L"command-conflict";
        fs::create_directories(conflictDir);
        const auto conflictIni = conflictDir / L"wincmd.ini";
        WriteText(conflictIni, "[Buttonbar]\r\nButtonbar=\r\n");
        const auto conflictBar = conflictDir / L"oldbuttons.bar";
        WriteText(conflictBar, "[Buttonbar]\r\nButtoncount=0\r\n");
        Require(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar",
                conflictBar.wstring().c_str(), conflictIni.wstring().c_str()), "setup conflict bar");
        const auto conflictCmd = conflictDir / L"usercmd.ini";
        WriteText(conflictCmd,
            "[em_TCSymbolicLinks_Create]\r\ncmd=rundll32.exe\r\nparam=unrelated.dll,UnrelatedEntry\r\n");
        const auto oldCommand = Ini(conflictCmd.wstring(), L"em_TCSymbolicLinks_Create", L"param");
        Require(!tcs::ConfigureForTotalCommander(conflictIni.wstring(), module.wstring(), message),
                "unrelated user command was accepted");
        Require(Ini(conflictCmd.wstring(), L"em_TCSymbolicLinks_Create", L"param") == oldCommand,
                "unrelated user command was overwritten");

        // Verify Win32's built-in rundll32 can load the WDX and call its
        // Unicode export without a separate TCSymlinkMaker.exe.
        fs::path marker = local / L"בדיקת rundll32 עִבְרִית.txt";
        wchar_t systemDir[MAX_PATH + 1]{};
        Require(GetSystemDirectoryW(systemDir, MAX_PATH + 1) > 0, "system directory");
        auto cmd = L"\"" + std::wstring(systemDir) + L"\\rundll32.exe\" \"" +
                   module.wstring() + L"\",TCLinkProbeW \"" + marker.wstring() + L"\"";
        STARTUPINFOW start{sizeof(start)};
        PROCESS_INFORMATION proc{};
        Require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &start, &proc),
                "cannot launch native rundll32.exe");
        Require(WaitForSingleObject(proc.hProcess, 12000) == WAIT_OBJECT_0,
                "rundll32 did not finish");
        CloseHandle(proc.hThread);
        CloseHandle(proc.hProcess);
        Require(fs::exists(marker), "rundll32 failed to call WDX Unicode entry point");
        std::cout << "PASS: one WDX64 module; Unicode rundll32 export; automatic toolbar + columns; backups; idempotence\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
