// Windows-only end-to-end link tests. Run on Windows CI with actual Win32 APIs.
#include "LinkCore.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void WriteTestFile(const fs::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Unable to create Hebrew source file");
    constexpr char content[] = "Unicode file path test";
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content, sizeof(content), &written, nullptr);
    CloseHandle(file);
    Require(ok && written == sizeof(content), "Unable to write source file");
}

struct TestDirectory {
    fs::path path;
    ~TestDirectory() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    }
};
}

int main() {
    try {
        wchar_t tempDir[MAX_PATH + 1]{};
        const DWORD length = GetTempPathW(MAX_PATH + 1, tempDir);
        Require(length != 0 && length <= MAX_PATH, "GetTempPathW failed");
        wchar_t tempFile[MAX_PATH + 1]{};
        Require(GetTempFileNameW(tempDir, L"TCS", 0, tempFile), "GetTempFileNameW failed");
        Require(DeleteFileW(tempFile), "Unable to delete temp filename placeholder");
        TestDirectory cleanup{fs::path(tempFile)};

        const fs::path sourceFolder = cleanup.path / L"מקורות בעברית";
        const fs::path targetFolder = cleanup.path / L"יעד 2026 English";
        fs::create_directories(sourceFolder);
        fs::create_directories(targetFolder);
        const fs::path original = sourceFolder / L"קובץ ש\u05B8לו\u05B9ם 42.txt";
        const fs::path hardlink = targetFolder / original.filename();
        WriteTestFile(original);

        // A real NTFS hardlink from a Hebrew filename to a Hebrew destination.
        Require(tcs::CreateLink(original.wstring(), hardlink.wstring(),
                tcs::LinkKind::Hardlink, false) == ERROR_SUCCESS,
                "Hebrew hardlink creation failed");
        Require(fs::exists(hardlink), "Hebrew hardlink not accessible");
        const auto info = tcs::InspectLink(hardlink.wstring(), true);
        Require(info.hardlinkCount >= 2, "Hardlink count not reported correctly");

        // Must not overwrite a pre-existing Hebrew-named destination.
        Require(tcs::CreateLink(original.wstring(), hardlink.wstring(),
                tcs::LinkKind::Hardlink, false) != ERROR_SUCCESS,
                "Existing destination was unexpectedly overwritten");

        // TC %WL directory selections end with a backslash. Regression:
        // v0.2.5 reported "Invalid source name" instead of creating a link.
        const auto directoryWithSlash = sourceFolder.wstring() + L"\\";
        Require(tcs::BaseName(directoryWithSlash) == sourceFolder.filename().wstring(),
                "Folder selection ending in backslash has an empty link name");
        Require(tcs::BaseName(sourceFolder.wstring()) == tcs::BaseName(directoryWithSlash),
                "Trailing-slash and ordinary folder names must match");
        Require(tcs::BaseName(L"C:\\").empty(),
                "Drive root should not be given a fabricated link name");
        // Directory junction with mixed Hebrew/Latin directory names.
        const fs::path junction = targetFolder / L"קישור תיקייה 2026";
        const DWORD junctionError = tcs::CreateLink(directoryWithSlash, junction.wstring(),
                                                       tcs::LinkKind::Junction, false);
        Require(junctionError == ERROR_SUCCESS, "Hebrew directory junction creation failed");
        const auto junctionDetails = tcs::InspectLink(junction.wstring());
        Require(junctionDetails.kind == tcs::ReparseKind::Junction,
                "Hebrew junction not detected as junction");
        Require(junctionDetails.targetExists, "Hebrew junction target not resolved");
        Require(fs::exists(junction / original.filename()), "Cannot traverse Hebrew junction");
        Require(tcs::CreateLink(sourceFolder.wstring(), junction.wstring(),
                tcs::LinkKind::Junction, false) != ERROR_SUCCESS,
                "A pre-existing Hebrew junction was overwritten");

        // Wrong link kinds must not be applied to files/directories.
        Require(tcs::CreateLink(original.wstring(), (targetFolder / L"wrong junction").wstring(),
                tcs::LinkKind::Junction, false) == ERROR_DIRECTORY,
                "Junction unexpectedly accepted a file source");
        Require(tcs::CreateLink(sourceFolder.wstring(), (targetFolder / L"wrong hardlink").wstring(),
                tcs::LinkKind::Hardlink, false) == ERROR_DIRECTORY,
                "Hardlink unexpectedly accepted a directory source");

        // Symbolic links require Developer Mode or the symlink privilege.
        const fs::path symlink = targetFolder / L"סימלינק קובץ 42.txt";
        const DWORD symlinkError = tcs::CreateLink(original.wstring(), symlink.wstring(),
                                                      tcs::LinkKind::FileSymbolic, true);
        if (symlinkError == ERROR_PRIVILEGE_NOT_HELD) {
            std::cout << "SKIP: file symlink needs Developer Mode or elevated privilege\n";
        } else {
            Require(symlinkError == ERROR_SUCCESS, "Hebrew symbolic link creation failed");
            const auto symlinkDetails = tcs::InspectLink(symlink.wstring());
            Require(symlinkDetails.kind == tcs::ReparseKind::SymbolicFile,
                    "Hebrew file symlink not detected");
            Require(symlinkDetails.relative, "Relative Hebrew target path was not retained");
            Require(symlinkDetails.targetExists, "Relative Hebrew target does not resolve");
            Require(fs::exists(symlink), "Hebrew symbolic link not accessible");
            Require(tcs::CreateLink(original.wstring(), symlink.wstring(),
                tcs::LinkKind::FileSymbolic, false) != ERROR_SUCCESS,
                "A pre-existing Hebrew symbolic link was overwritten");
        }

        const fs::path directorySymlink = targetFolder / L"סימלינק תיקייה";
        const DWORD dirSymbolError = tcs::CreateLink(directoryWithSlash, directorySymlink.wstring(),
                                                     tcs::LinkKind::DirectorySymbolic, true);
        if (dirSymbolError == ERROR_PRIVILEGE_NOT_HELD) {
            std::cout << "SKIP: directory symlink needs Developer Mode or elevated privilege\n";
        } else {
            Require(dirSymbolError == ERROR_SUCCESS, "Hebrew directory symlink creation failed");
            const auto dirInfo = tcs::InspectLink(directorySymlink.wstring());
            Require(dirInfo.kind == tcs::ReparseKind::SymbolicDirectory,
                    "Hebrew directory symlink was not detected");
            Require(dirInfo.relative, "Directory symlink did not retain relative path");
            Require(dirInfo.targetExists, "Directory symlink target does not resolve");
        }
        std::cout << "PASS: Hebrew Unicode Win32 hardlink, junction, symlink checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
