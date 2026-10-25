#include "TcIntegration.h"
#include <windows.h>
#include <chrono>
#include <thread>
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
        // IMPORTANT: the previous CI tested a hand-written rundll32 command,
        // not the actual command saved into the user's button bar. Reproduce
        // TC's %WL and %T substitutions on the persisted button arguments and
        // click the maker GUI. This catches quoting / no-op button regressions.
        const auto selection = profile / L"TC-selection-עברית.wl";
        const auto source = profile / L"test קובץ שָלוֹם.txt";
        const auto destination = profile / L"target יעד";
        fs::create_directories(destination);
        WriteText(source, "Unicode link source");
        {
            std::vector<unsigned char> data{0xFF, 0xFE};
            const std::wstring line = source.wstring() + L"\r\n";
            for (wchar_t c : line) {
                data.push_back(static_cast<unsigned char>(c & 0xFF));
                data.push_back(static_cast<unsigned char>((c >> 8) & 0xFF));
            }
            std::ofstream output(selection, std::ios::binary);
            Require(static_cast<bool>(output), "open real button %WL fixture");
            output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            Require(static_cast<bool>(output), "write real button %WL fixture");
        }
        std::wstring buttonParam = Ini(bar, L"Buttonbar", L"param4");
        auto substitute = [&](const std::wstring& from, const std::wstring& to) {
            std::size_t pos = 0;
            while ((pos = buttonParam.find(from, pos)) != std::wstring::npos) {
                buttonParam.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        // TC expands its installation and profile variables when invoking
        // a button; the test executable lives next to the plugin DLL, so
        // these are the exact directories used by its integration fixture.
        substitute(L"%COMMANDER_PATH%", module.parent_path().wstring());
        substitute(L"%COMMANDER_INI_PATH%", profile.wstring());
        substitute(L"%WL", selection.wstring());
        substitute(L"%T", destination.wstring() + L"\\");
        // In actual TC these macros are expanded by TC; they must not remain.
        Require(buttonParam.find(L"%WL") == std::wstring::npos &&
                buttonParam.find(L"%T") == std::wstring::npos,
                "button substitution left unresolved TC placeholders");
        const auto buttonHost = Ini(bar, L"Buttonbar", L"cmd4");
        // Probe the exact installed DLL path, not a hand-written path.
        const auto creator = buttonParam.find(L",TCLinkCreate ");
        Require(creator != std::wstring::npos, "persisted button export not found");
        const auto markerFromBar = profile / L"marker-for-installed-button-path.txt";
        const auto markerArgs = buttonParam.substr(0, creator) + L",TCLinkProbe \"" +
                                markerFromBar.wstring() + L"\"";
        std::wstring probeCommand = L"\"" + buttonHost + L"\" " + markerArgs;
        STARTUPINFOW probeStartup{sizeof(probeStartup)};
        PROCESS_INFORMATION probeProcess{};
        Require(CreateProcessW(buttonHost.c_str(), probeCommand.data(), nullptr, nullptr,
                               FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                               &probeStartup, &probeProcess) != FALSE,
                "persisted WDX button probe did not start rundll32");
        CloseHandle(probeProcess.hThread);
        const auto probeWait = WaitForSingleObject(probeProcess.hProcess, 7000);
        if (probeWait != WAIT_OBJECT_0) {
            TerminateProcess(probeProcess.hProcess, 1);
            WaitForSingleObject(probeProcess.hProcess, 2500);
        }
        CloseHandle(probeProcess.hProcess);
        if (!fs::exists(markerFromBar)) {
            std::wcerr << L"persisted BAR probe (fixture): " << probeCommand << L"\\n";
            Require(false, "WDX module path in persisted button did NOT load through rundll32");
        }
        std::wstring buttonCommand = L"\"" + buttonHost + L"\" " + buttonParam;
        STARTUPINFOW buttonStartup{sizeof(buttonStartup)};
        PROCESS_INFORMATION buttonProcess{};
        Require(CreateProcessW(buttonHost.c_str(), buttonCommand.data(), nullptr, nullptr,
                               FALSE, 0, nullptr, nullptr, &buttonStartup, &buttonProcess) != FALSE,
                "installed toolbar command did not launch rundll32");
        CloseHandle(buttonProcess.hThread);
        HWND makerWindow = nullptr;
        for (int i = 0; i < 100; ++i) {
            HWND candidate = FindWindowW(L"TCSymbolicLinksMakerWindow", nullptr);
            DWORD owner = 0;
            if (candidate) GetWindowThreadProcessId(candidate, &owner);
            if (candidate && owner == buttonProcess.dwProcessId) {
                makerWindow = candidate;
                break;
            }
            if (WaitForSingleObject(buttonProcess.hProcess, 0) == WAIT_OBJECT_0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool created = false;
        if (makerWindow) {
            HWND typeCombo = GetDlgItem(makerWindow, 102);
            HWND createButton = GetDlgItem(makerWindow, 105);
            if (typeCombo && createButton && SendMessageW(typeCombo, CB_SETCURSEL, 4, 0) == 4) {
                PostMessageW(makerWindow, WM_COMMAND,
                    MAKEWPARAM(105, BN_CLICKED), reinterpret_cast<LPARAM>(createButton));
                const auto targetFile = destination / source.filename();
                for (int i = 0; i < 100; ++i) {
                    if (fs::exists(targetFile)) {
                        std::error_code ec;
                        created = fs::equivalent(source, targetFile, ec) && !ec;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            PostMessageW(makerWindow, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(buttonProcess.hProcess, 4000) != WAIT_OBJECT_0) {
            TerminateProcess(buttonProcess.hProcess, 1);
            WaitForSingleObject(buttonProcess.hProcess, 4000);
        }
        CloseHandle(buttonProcess.hProcess);
        if (!makerWindow) std::wcerr << L"persisted maker command (fixture): " << buttonCommand << L"\\n";
        Require(makerWindow != nullptr, "exact persisted TC button command opened no Maker window");
        Require(created, "exact persisted TC button could not create Hebrew hardlink");
        std::cout << "PASS: original buttons retained, command direct and runnable, both columns, "
                     "usercmd pristine, UTF-16, backups, no duplicates, legacy refusal, persisted button GUI hardlink\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
