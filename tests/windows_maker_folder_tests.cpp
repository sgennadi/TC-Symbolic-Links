// Regression from real Total Commander 11.58 screenshot: %WL directory
// selection ends in '\'. The UI must create a directory junction, not
// report "Invalid source name" or suggest Error 1314 for an unrelated error.
#include "LinkCore.h"
#include <windows.h>
#include <commctrl.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace fs = std::filesystem;
namespace {
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
struct Cleanup {
    fs::path dir;
    ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); }
};
std::wstring ControlText(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring result(static_cast<std::size_t>(length + 1), L'\0');
    length = GetWindowTextW(hwnd, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}
void WideList(const fs::path& out, const std::wstring& folder) {
    HANDLE handle = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "Cannot write UTF-16LE test selection");
    const std::wstring data = L"\uFEFF" + folder + L"\\\r\n";
    DWORD written = 0;
    const DWORD needed = static_cast<DWORD>(data.size() * sizeof(wchar_t));
    const bool ok = WriteFile(handle, data.data(), needed, &written, nullptr) && written == needed;
    CloseHandle(handle);
    Check(ok, "Unable to write full UTF-16LE test selection");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "Provide SymlinkInspector.wdx64 path");
        fs::path dll = argv[1];
        Check(fs::is_regular_file(dll), "Compiled WDX DLL missing");
        wchar_t temp[MAX_PATH + 1]{};
        Check(GetTempPathW(MAX_PATH + 1, temp) != 0, "No Windows temporary directory");
        wchar_t tmpFile[MAX_PATH + 1]{};
        Check(GetTempFileNameW(temp, L"TSF", 0, tmpFile) != 0, "Cannot generate folder test location");
        Check(DeleteFileW(tmpFile) != FALSE, "Cannot remove placeholder");
        Cleanup root{fs::path(tmpFile)};
        auto folder = root.dir / L"WinPE שלום folder";
        auto output = root.dir / L"destination קישורים";
        fs::create_directories(folder);
        fs::create_directories(output);
        { std::ofstream data(folder / L"child.txt"); data << "Junction child"; }
        WideList(root.dir / L"selected.wl", folder.wstring());
        wchar_t system[MAX_PATH + 1]{};
        Check(GetSystemDirectoryW(system, MAX_PATH + 1) > 0, "No system directory");
        const fs::path rundll = fs::path(system) / L"rundll32.exe";
        std::wstring command = L"\"" + rundll.wstring() + L"\" \"" +
            dll.wstring() + L"\",TCLinkCreate --list \"" +
            (root.dir / L"selected.wl").wstring() + L"\" --dest \"" +
            output.wstring() + L"\" --tc-launch";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        Check(CreateProcessW(rundll.c_str(), command.data(), nullptr, nullptr,
                             FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE,
              "Cannot start rundll32 DLL maker");
        CloseHandle(process.hThread);
        HWND window = nullptr;
        for (int i = 0; i < 100; ++i) {
            HWND test = FindWindowW(L"TCSymbolicLinksMakerWindow", nullptr);
            DWORD owner = 0;
            if (test) GetWindowThreadProcessId(test, &owner);
            if (test && owner == process.dwProcessId) { window = test; break; }
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool created = false;
        bool helpPresent = false;
        bool relativeDisabled = false;
        bool wrongHint = false;
        bool commanderLayout = false;
        bool actionStripOnScreen = false;
        bool rejectsSameFolder = false;
        bool rejectsChildDestination = false;
        bool allowsSafeTarget = false;
        if (window) {
            // TC-style regression: two resizable file panels and familiar
            // classic list view must exist while retaining the old WDX IDs.
            HWND sourceList = GetDlgItem(window, 100);
            HWND sourcePath = GetDlgItem(window, 111);
            HWND destinationPath = GetDlgItem(window, 101);
            HWND resultPane = GetDlgItem(window, 104);
            HWND action = GetDlgItem(window, 105);
            // Test the usable desktop rectangle (taskbar excluded). The new
            // bottom action buttons must be fully visible after launch.
            RECT actionRect{}, workArea{};
            if (action && GetWindowRect(action, &actionRect) &&
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
                actionStripOnScreen = actionRect.left >= workArea.left &&
                    actionRect.right <= workArea.right &&
                    actionRect.top >= workArea.top &&
                    actionRect.bottom <= workArea.bottom;
            }
            RECT left{}, right{}, srcLine{}, dstLine{};
            wchar_t className[80]{};
            if (sourceList && sourcePath && destinationPath && resultPane && action) {
                GetClassNameW(sourceList, className, 80);
                GetWindowRect(sourceList, &left);
                GetWindowRect(resultPane, &right);
                GetWindowRect(sourcePath, &srcLine);
                GetWindowRect(destinationPath, &dstLine);
                commanderLayout = _wcsicmp(className, L"SysListView32") == 0 &&
                    SendMessageW(sourceList, LVM_GETITEMCOUNT, 0, 0) == 1 &&
                    left.right <= right.left && left.top == right.top &&
                    srcLine.right <= dstLine.left && srcLine.top == dstLine.top &&
                    ControlText(action).find(L"F5") != std::wstring::npos;
            }
            // User screenshot regression: source Downloads\PS1, target
            // Downloads\. must NOT attempt to create Downloads\PS1 again.
            HWND destination = GetDlgItem(window, 101);
            HWND make = GetDlgItem(window, 105);
            if (destination && make) {
                const auto identical = (root.dir / L".").wstring();
                auto refresh = [&](const std::wstring& value) {
                    Check(SetWindowTextW(destination, value.c_str()) != FALSE,
                          "Cannot set destination in GUI guard test");
                    // Simulate the exact EN_CHANGE event emitted when a user
                    // edits the path in the destination panel.
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(101, EN_CHANGE),
                                 reinterpret_cast<LPARAM>(destination));
                };
                refresh(identical);
                rejectsSameFolder = !IsWindowEnabled(make);
                // No recursive links: don't create a selected folder within itself.
                refresh((folder / L".").wstring());
                rejectsChildDestination = !IsWindowEnabled(make);
                refresh(output.wstring());
                allowsSafeTarget = IsWindowEnabled(make);
                if (!rejectsSameFolder || !rejectsChildDestination || !allowsSafeTarget) {
                    std::wcerr << L"Guard diagnostics: same=" << rejectsSameFolder
                        << L" nested=" << rejectsChildDestination
                        << L" safe=" << allowsSafeTarget
                        << L" status=" << ControlText(GetDlgItem(window, 0))
                        << L" preview=" << ControlText(GetDlgItem(window, 110))
                        << L"\n";
                }
            }
            HWND combo = GetDlgItem(window, 102);
            HWND createButton = GetDlgItem(window, 105);
            HWND relativeButton = GetDlgItem(window, 103);
            HWND help = GetDlgItem(window, 108);
            HWND relativeInfo = GetDlgItem(window, 109);
            if (combo && createButton && relativeButton && help && relativeInfo &&
                SendMessageW(combo, CB_SETCURSEL, 3, 0) == 3) {
                SendMessageW(window, WM_COMMAND,
                    MAKEWPARAM(102, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
                helpPresent = !ControlText(help).empty() && !ControlText(relativeInfo).empty();
                relativeDisabled = !IsWindowEnabled(relativeButton);
                // F5 must create exactly as clicking the TC-style action key.
                PostMessageW(window, WM_KEYDOWN, VK_F5, 0);
                const auto link = output / folder.filename();
                for (int i = 0; i < 100; ++i) {
                    if (fs::exists(link / L"child.txt")) {
                        created = tcs::InspectLink(link.wstring()).kind == tcs::ReparseKind::Junction;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                const auto messages = ControlText(GetDlgItem(window, 104));
                wrongHint = messages.find(L"Invalid source name") != std::wstring::npos ||
                            messages.find(L"Error 1314") != std::wstring::npos;
            }
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(process.hProcess, 4000) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 4000);
        }
        CloseHandle(process.hProcess);
        Check(window != nullptr, "Maker window not opened for %WL folder selection");
        Check(commanderLayout, "TC-style dual file panes, native file table, path strips or F5 button missing");
        Check(rejectsSameFolder && rejectsChildDestination && allowsSafeTarget,
              "Maker F5 not guarded against same-directory or recursive link destination");
        Check(actionStripOnScreen, "TC-style F5 button clipped by Windows taskbar/work area");
        Check(helpPresent && relativeDisabled, "The selected Junction mode needs useful help and disabled relative option");
        Check(created, "%WL directory ending in backslash did not create a junction");
        Check(!wrongHint, "Screenshot regression: invalid-name or 1314 error shown for working directory");
        std::cout << "PASS: TC-like two panels, file table, path bars, F5 hotkey and Hebrew Junction creation\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
