#include "TcIntegration.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
namespace fs = std::filesystem;
namespace {
void Require(bool ok, const char* description) { if (!ok) throw std::runtime_error(description); }
void WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    Require(static_cast<bool>(out), "unable to open test fixture");
    out << text;
    Require(static_cast<bool>(out), "unable to write test fixture");
}
std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    Require(static_cast<bool>(in), "unable to open fixture for comparison");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::wstring Ini(const fs::path& file, const wchar_t* section, const wchar_t* key) {
    wchar_t value[16384]{};
    GetPrivateProfileStringW(section, key, L"", value, 16384, file.wstring().c_str());
    return value;
}
struct TempFolder {
    fs::path path;
    ~TempFolder() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Pass WDX module path");
        const fs::path module = argv[1];
        Require(fs::exists(module), "Built WDX64 missing");
        wchar_t tempDir[MAX_PATH+1]{};
        Require(GetTempPathW(MAX_PATH+1, tempDir) > 0, "GetTempPathW failed");
        wchar_t path[MAX_PATH+1]{};
        Require(GetTempFileNameW(tempDir, L"TCS", 0, path) != 0, "GetTempFileNameW failed");
        Require(DeleteFileW(path), "Delete temp filename failed");
        TempFolder temp{fs::path(path)};
        fs::create_directories(temp.path);
        const auto profile = temp.path / L"עברית שָלוֹם";
        fs::create_directories(profile);
        const auto ini = profile / L"wincmd.ini";
        const auto bar = profile / L"personal-custom-buttons.bar";
        const auto usercmd = profile / L"usercmd.ini";
        const std::string originalUserCommands =
            "[em_User_Custom_DoNotTouch]\r\ncmd=notepad.exe\r\nparam=hello\r\n"
            "[em_TCSymbolicLinks_Create]\r\ncmd=notepad.exe\r\nparam=NOT_OUR_COMMAND\r\n";
        const std::string originalBar =
            "[Buttonbar]\r\nButtoncount=3\r\n"
            "cmd1=cm_Copy\r\nbutton1=WCMICONS.DLL,1\r\nmenu1=Copy\r\n"
            "cmd2=cm_Delete\r\nmenu2=Delete\r\n"
            "cmd3=%COMMANDER_PATH%\\MySubbar.bar\r\nmenu3=Custom submenu\r\n"
            "[MyUnrelatedSection]\r\nkeep=this value\r\n";
        WriteText(bar, originalBar);
        WriteText(usercmd, originalUserCommands);
        WriteText(ini,
            "[CustomFields]\r\nTitles=Music|Photos\r\nHeaders1=Foo\r\n"
            "Contents1=[=tc.name]\\n[=tc.size]\r\n"
            "[Buttonbar]\r\nButtonbar=placeholder\r\n"
            "[left]\r\nSpecialView=1\r\n"
            "[right]\r\nSpecialView=2\r\n");
        const auto selectedPath = L"%COMMANDER_INI_PATH%\\personal-custom-buttons.bar";
        Require(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar", selectedPath,
                                           ini.wstring().c_str()) != FALSE, "set personal toolbar");
        const auto iniBefore = ReadText(ini);
        std::wstring status;
        Require(tcs::ConfigureForTotalCommander(ini.wstring(), module.wstring(), status),
                "safe initial integration must succeed");
        // Regression: 0.2.1 overwrote the active buttonbar reference and users lost buttons.
        Require(Ini(ini, L"Buttonbar", L"Buttonbar") == selectedPath,
                "REGRESSION: active personal button bar was replaced");
        Require(!fs::exists(profile / L"TCSymbolicLinks.bar"),
                "REGRESSION: must not create/activate a shadow toolbar");
        Require(Ini(bar, L"Buttonbar", L"Buttoncount") == L"4", "did not append one button");
        for (int n = 1; n <= 3; ++n) {
            const auto num = std::to_wstring(n);
            if (n == 1) Require(Ini(bar, L"Buttonbar", (L"cmd"+num).c_str()) == L"cm_Copy", "Copy changed");
            if (n == 2) Require(Ini(bar, L"Buttonbar", (L"cmd"+num).c_str()) == L"cm_Delete", "Delete changed");
            if (n == 3) Require(Ini(bar, L"Buttonbar", (L"menu"+num).c_str()) == L"Custom submenu", "custom submenu lost");
        }
        Require(Ini(bar, L"MyUnrelatedSection", L"keep") == L"this value", "other section lost");
        Require(Ini(bar, L"Buttonbar", L"cmd4").find(L"\\rundll32.exe") != std::wstring::npos,
                "button must directly invoke system rundll32.exe");
        const auto buttonParams = Ini(bar, L"Buttonbar", L"param4");
        Require(buttonParams.find(L"SymlinkInspector.wdx64") != std::wstring::npos,
                "button missing WDX module");
        Require(buttonParams.find(L",TCLinkCreate --list") != std::wstring::npos,
                "incorrect rundll32 Unicode export invocation");
        Require(buttonParams.find(L"%WL") != std::wstring::npos &&
                buttonParams.find(L"%T.") != std::wstring::npos,
                "Total Commander source/destination placeholders missing");
        Require(ReadText(usercmd) == originalUserCommands,
                "REGRESSION: usercmd.ini was changed or re-encoded");
        Require(Ini(ini, L"CustomFields", L"Titles") == L"Music|Photos|TC Symbolic Links",
                "existing column sets must be retained");
        Require(Ini(ini, L"CustomFields", L"Headers1") == L"Foo", "previous columns changed");
        Require(Ini(ini, L"left", L"SpecialView") == L"5" &&
                Ini(ini, L"right", L"SpecialView") == L"5", "custom view #3 must map to SpecialView 5");
        Require(fs::exists(ini.wstring() + L".tcslinks.bak"), "wincmd.ini backup missing");
        Require(fs::exists(bar.wstring() + L".tcslinks.bak"), "ORIGINAL active toolbar backup missing");
        Require(ReadText(bar.wstring() + L".tcslinks.bak") == originalBar,
                "original toolbar backup does not match actual original");
        Require(ReadText(ini.wstring() + L".tcslinks.bak") == iniBefore,
                "original wincmd.ini backup mismatch");
        Require(tcs::ConfigureForTotalCommander(ini.wstring(), module.wstring(), status),
                "idempotent integration failed");
        Require(Ini(bar, L"Buttonbar", L"Buttoncount") == L"4", "duplicate added on reinstall");
        Require(Ini(ini, L"Buttonbar", L"Buttonbar") == selectedPath,
                "second invocation replaced buttonbar");
        Require(ReadText(usercmd) == originalUserCommands, "usercmd changed on re-run");

        // Another user's button named the same must not be overwritten.
        const auto conflict = temp.path / L"conflict";
        fs::create_directories(conflict);
        const auto conflictIni = conflict / L"wincmd.ini";
        const auto conflictBar = conflict / L"mybar.bar";
        WriteText(conflictIni, "[Buttonbar]\r\nButtonbar=mybar.bar\r\n");
        WriteText(conflictBar,
            "[Buttonbar]\r\nButtoncount=1\r\ncmd1=notepad.exe\r\n"
            "menu1=TC Symbolic Links - Create links\r\n");
        Require(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar",
                  conflictBar.wstring().c_str(), conflictIni.wstring().c_str()), "set conflict bar");
        const auto before = ReadText(conflictBar);
        Require(!tcs::ConfigureForTotalCommander(conflictIni.wstring(), module.wstring(), status),
                "conflicting user button must be refused");
        Require(ReadText(conflictBar) == before, "conflicting user button was modified");

        // Refuse to guess DEFAULT.BAR. 0.2.1 guessed, then silently switched.
        const auto unknown = temp.path / L"unknown";
        fs::create_directories(unknown);
        const auto unknownIni = unknown / L"wincmd.ini";
        WriteText(unknownIni, "[Buttonbar]\r\nButtonbar=\r\n");
        const auto unknownOriginal = ReadText(unknownIni);
        Require(!tcs::ConfigureForTotalCommander(unknownIni.wstring(), module.wstring(), status),
                "missing active toolbar must fail rather than guess");
        Require(ReadText(unknownIni) == unknownOriginal, "unknown toolbar profile changed");

        // Legacy 0.2.1 forced a shadow bar, which users must restore first.
        const auto legacy = temp.path / L"legacy";
        fs::create_directories(legacy);
        const auto legacyIni = legacy / L"wincmd.ini";
        const auto legacyBar = legacy / L"TCSymbolicLinks.bar";
        WriteText(legacyIni, "[Buttonbar]\r\nButtonbar=old\r\n");
        WriteText(legacyBar, "[Buttonbar]\r\nButtoncount=1\r\ncmd1=em_TCSymbolicLinks_Create\r\n");
        Require(WritePrivateProfileStringW(L"Buttonbar", L"Buttonbar", legacyBar.wstring().c_str(),
                                           legacyIni.wstring().c_str()), "set legacy clone");
        const auto legacyBefore = ReadText(legacyIni);
        Require(!tcs::ConfigureForTotalCommander(legacyIni.wstring(), module.wstring(), status),
                "legacy clone must prompt for original toolbar recovery");
        Require(ReadText(legacyIni) == legacyBefore, "legacy profile modified instead of recovered");

        // Native Unicode export via actual Windows rundll32 remains supported.
        HMODULE hWdx = LoadLibraryW(module.wstring().c_str());
        Require(hWdx != nullptr, "cannot load WDX DLL");
        const bool hasProbe = GetProcAddress(hWdx, "TCLinkProbeW") != nullptr;
        const bool hasMaker = GetProcAddress(hWdx, "TCLinkCreateW") != nullptr;
        FreeLibrary(hWdx);
        Require(hasProbe && hasMaker, "rundll32 Unicode exports missing");
        fs::path marker = profile / L"בדיקת rundll32 עִבְרִית.txt";
        // Run the actual command recorded in the bar; substitute a non-GUI
        // probe export to verify the serialization and host path as well.
        auto cmd = L"\"" + Ini(bar, L"Buttonbar", L"cmd4") + L"\" \"" +
                   module.wstring() + L"\",TCLinkProbe \"" + marker.wstring() + L"\"";
        STARTUPINFOW start{sizeof(start)};
        PROCESS_INFORMATION process{};
        Require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &start, &process),
                "cannot spawn Windows rundll32.exe");
        Require(WaitForSingleObject(process.hProcess, 12000) == WAIT_OBJECT_0,
                "rundll32 Unicode probe timeout");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Require(fs::exists(marker), "Unicode WDX function not invoked");
        std::cout << "PASS: original buttons retained, command direct and runnable, both columns, "
                     "usercmd pristine, UTF-16, backups, no duplicates, legacy refusal\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
