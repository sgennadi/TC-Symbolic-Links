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
    if (!hwnd) return {};
    // The WDX maker is hosted in another rundll32.exe process. Cross-process
    // GetWindowText cannot read EDIT controls; WM_GETTEXT is marshalled by
    // USER32 and is required for genuine integration-test assertions.
    const auto length = SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
    if (length <= 0 || length > 200000) return {};
    std::wstring result(static_cast<std::size_t>(length + 1), L'\0');
    const auto count = SendMessageW(hwnd, WM_GETTEXT,
        static_cast<WPARAM>(length + 1),
        reinterpret_cast<LPARAM>(result.data()));
    if (count < 0) return {};
    result.resize(static_cast<std::size_t>(count));
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
struct ToolTipCount {
    HWND owner=nullptr;
    int count=0;
};
BOOL CALLBACK CountOwnerToolTips(HWND candidate, LPARAM context) {
    auto& result=*reinterpret_cast<ToolTipCount*>(context);
    wchar_t name[80]{};
    if(GetClassNameW(candidate,name,80) &&
       _wcsicmp(name,TOOLTIPS_CLASSW)==0 &&
       GetWindow(candidate,GW_OWNER)==result.owner) {
        const auto n=SendMessageW(candidate,TTM_GETTOOLCOUNT,0,0);
        if(n>0) result.count+=static_cast<int>(n);
    }
    return TRUE;
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
            output.wstring() + L"\" --mode directory --tc-launch";
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
            if (test && owner == process.dwProcessId) {
                // The top-level window becomes visible during WM_CREATE,
                // before x86 rundll32 has necessarily created all controls.
                // Wait for the native list and source row before inspecting
                // geometry or sending an F5 command to the Maker.
                window = test;
                const HWND source = GetDlgItem(test, 100);
                const HWND results = GetDlgItem(test, 104);
                if (source && results && GetDlgItem(test, 101) &&
                    GetDlgItem(test, 105) && GetDlgItem(test, 111) &&
                    SendMessageW(source, LVM_GETITEMCOUNT, 0, 0) == 1) {
                    RECT leftPane{}, rightPane{};
                    if (GetWindowRect(source, &leftPane) &&
                        GetWindowRect(results, &rightPane) &&
                        leftPane.right <= rightPane.left &&
                        leftPane.top == rightPane.top) break;
                }
            }
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool created = false;
        bool presetDirectoryMode = false;
        bool helpPresent = false;
        bool relativeDisabled = false;
        bool wrongHint = false;
        bool commanderLayout = false;
        bool actionStripOnScreen = false;
        bool understandableControls = false;
        bool wdxTaskbarIcons = false;
        bool hoverHelpInstalled = false;
        bool nipkgControlsFit = false;
        bool rejectsSameFolder = false;
        bool rejectsChildDestination = false;
        bool allowsSafeTarget = false;
        if (window) {
            // Regress the user screenshot: rundll32 HINSTANCE is NOT the
            // SymlinkInspector module. Both taskbar/title icons must be
            // explicitly assigned from the WDX and not generic Windows.
            DWORD_PTR largeIcon=0,smallIcon=0;
            if(SendMessageTimeoutW(window,WM_GETICON,ICON_BIG,0,
                   SMTO_ABORTIFHUNG,2000,&largeIcon) &&
               SendMessageTimeoutW(window,WM_GETICON,ICON_SMALL,0,
                   SMTO_ABORTIFHUNG,2000,&smallIcon))
                wdxTaskbarIcons=largeIcon!=0 && smallIcon!=0 &&
                    largeIcon!=reinterpret_cast<DWORD_PTR>(LoadIconW(nullptr,IDI_APPLICATION));
            ToolTipCount tipCount{window,0};
            const DWORD uiThread=GetWindowThreadProcessId(window,nullptr);
            if(uiThread)EnumThreadWindows(uiThread,CountOwnerToolTips,
                            reinterpret_cast<LPARAM>(&tipCount));
            hoverHelpInstalled=tipCount.count>=12;
            // TC-style regression: two resizable file panels and familiar
            // classic list view must exist while retaining the old WDX IDs.
            HWND sourceList = GetDlgItem(window, 100);
            HWND sourcePath = GetDlgItem(window, 111);
            HWND destinationPath = GetDlgItem(window, 101);
            HWND resultPane = GetDlgItem(window, 104);
            HWND action = GetDlgItem(window, 105);
            HWND folderGlyph = GetDlgItem(window, 112);
            HWND linkGlyph = GetDlgItem(window, 113);
            HWND statusGlyph = GetDlgItem(window, 114);
            HWND browseFolder = GetDlgItem(window, 115);
            HWND closeAction = GetDlgItem(window, 106);
            HWND checkUpdates = GetDlgItem(window, 116);
            if (!sourceList || !sourcePath || !destinationPath ||
                !resultPane || !action)
                std::wcerr << L"Maker controls not ready: src=" << (sourceList != nullptr)
                    << L", srcPath=" << (sourcePath != nullptr)
                    << L", destPath=" << (destinationPath != nullptr)
                    << L", results=" << (resultPane != nullptr)
                    << L", action=" << (action != nullptr) << L"\n";
            RECT folderBounds{}, buttonBounds{};
            if (folderGlyph && linkGlyph && statusGlyph && browseFolder &&
                closeAction && checkUpdates && action &&
                GetWindowRect(folderGlyph, &folderBounds) &&
                GetWindowRect(browseFolder, &buttonBounds)) {
                const DWORD makeStyle = static_cast<DWORD>(
                    GetWindowLongPtrW(action, GWL_STYLE));
                const DWORD browseStyle = static_cast<DWORD>(
                    GetWindowLongPtrW(browseFolder, GWL_STYLE));
                const DWORD closeStyle = static_cast<DWORD>(
                    GetWindowLongPtrW(closeAction, GWL_STYLE));
                understandableControls =
                    (makeStyle & BS_TYPEMASK) == BS_OWNERDRAW &&
                    (browseStyle & BS_TYPEMASK) == BS_OWNERDRAW &&
                    (closeStyle & BS_TYPEMASK) == BS_OWNERDRAW &&
                    folderBounds.right > folderBounds.left &&
                    buttonBounds.right > buttonBounds.left &&
                    ControlText(action).find(L"Create") != std::wstring::npos &&
                    ControlText(closeAction).find(L"Close") != std::wstring::npos;
            }
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
                const auto rows = SendMessageW(sourceList, LVM_GETITEMCOUNT, 0, 0);
                const auto f5Label = ControlText(action);
                commanderLayout = _wcsicmp(className, L"SysListView32") == 0 &&
                    rows == 1 &&
                    left.right <= right.left && left.top == right.top &&
                    srcLine.right <= dstLine.left && srcLine.top == dstLine.top &&
                    f5Label.find(L"F5") != std::wstring::npos;
                if (!commanderLayout) {
                    std::wcerr << L"Maker layout diagnostics: class=" << className
                        << L", rows=" << rows << L", action=" << f5Label
                        << L", source=[" << left.left << L"," << left.top
                        << L"," << left.right << L"," << left.bottom << L"]"
                        << L", results=[" << right.left << L"," << right.top
                        << L"," << right.right << L"," << right.bottom << L"]"
                        << L", srcPath=[" << srcLine.left << L"," << srcLine.top
                        << L"," << srcLine.right << L"," << srcLine.bottom << L"]"
                        << L", dstPath=[" << dstLine.left << L"," << dstLine.top
                        << L"," << dstLine.right << L"," << dstLine.bottom << L"]\n";
                }
                // NiPackageCreator regression: Browse is a labelled button,
                // text fields have room, and actions do not touch each other.
                RECT browseRect{}, createRect{}, closeRect{}, updateRect{};
                if (browseFolder && closeAction &&
                    GetWindowRect(browseFolder, &browseRect) &&
                    GetWindowRect(action, &createRect) &&
                    GetWindowRect(closeAction, &closeRect) &&
                    checkUpdates && GetWindowRect(checkUpdates, &updateRect)) {
                    const UINT dpi = GetDpiForWindow(window);
                    nipkgControlsFit = browseRect.left >= dstLine.right &&
                        browseRect.top <= dstLine.top &&
                        browseRect.bottom >= dstLine.bottom &&
                        browseRect.right <= right.right &&
                        updateRect.right < createRect.left &&
                        updateRect.left >= left.left &&
                        !ControlText(checkUpdates).empty() &&
                        createRect.right < closeRect.left &&
                        (browseRect.right - browseRect.left) >=
                            MulDiv(108, static_cast<int>(dpi), 96) &&
                        !ControlText(browseFolder).empty();
                }
            }
            // User screenshot regression: source Downloads\PS1, target
            // Downloads\. must NOT attempt to create Downloads\PS1 again.
            HWND destination = GetDlgItem(window, 101);
            HWND make = GetDlgItem(window, 105);
            if (destination && make) {
                const auto identical = (root.dir / L".").wstring();
                auto refresh = [&](const std::wstring& value) {
                    Check(SendMessageW(destination, WM_SETTEXT, 0,
                                       reinterpret_cast<LPARAM>(value.c_str())) != 0,
                          "Cross-process WM_SETTEXT could not update destination");
                    // Simulate the exact EN_CHANGE event emitted when a user
                    // edits the path in the destination panel.
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(101, EN_CHANGE),
                                 reinterpret_cast<LPARAM>(destination));
                };
                refresh(identical);
                rejectsSameFolder = !IsWindowEnabled(make) &&
                    ControlText(GetDlgItem(window, 110)).find(L"same folder") != std::wstring::npos;
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
            // The dedicated Directory toolbar button must preselect its
            // mode before any user interaction. The same flag parser drives
            // --mode file for the File Symlink button.
            presetDirectoryMode = combo && SendMessageW(combo, CB_GETCURSEL, 0, 0) == 2;
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
        Check(wdxTaskbarIcons, "WDX chain icon missing: rundll32 default taskbar or title-bar icon was used");
        Check(hoverHelpInstalled, "Missing Win32 hover help on relative target, link type and destination controls");
        Check(presetDirectoryMode, "Directory toolbar mode was not preselected");
        Check(commanderLayout, "TC-style dual file panes, native file table, path strips or F5 button missing");
        Check(understandableControls, "Source/target/status glyphs, browse icon or labeled Create/Close buttons missing");
        Check(nipkgControlsFit, "Ni-package-style Browse or action buttons clipped or overlap file panes");
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
