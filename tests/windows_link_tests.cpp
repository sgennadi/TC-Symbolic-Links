// Windows-only end-to-end link tests. Run on Windows CI with actual Win32 APIs.
#include "LinkCore.h"
#include "LinkFailureHelp.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <string_view>
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

        // Total Commander %Z can report a WCX/WIM virtual directory.
        // The file backing that virtual directory is NOT a filesystem folder.
        // Never substitute its containing folder when creating an NTFS link.
        const fs::path archiveFile = cleanup.path / L"boot.wim";
        WriteTestFile(archiveFile);
        const fs::path archiveInside = archiveFile / L"image1" / L"Windows";
        Require(_wcsicmp(tcs::ExistingFileAncestor(archiveInside.wstring()).c_str(),
                        archiveFile.wstring().c_str()) == 0,
                "WCX virtual directory did not detect its archive file ancestor");
        Require(_wcsicmp(tcs::ExistingFileAncestor(archiveFile.wstring()).c_str(),
                        archiveFile.wstring().c_str()) == 0,
                "An archive file was incorrectly recognized as a folder");
        Require(tcs::ExistingFileAncestor(targetFolder.wstring()).empty(),
                "Existing Windows folder misclassified as archive");
        Require(tcs::ExistingFileAncestor(
                (targetFolder / L"not-created" / L"folder").wstring()).empty(),
                "Ordinary missing folder misclassified as archive");
        Require(tcs::CreateLink(archiveFile.wstring(), archiveFile.wstring(),
                tcs::LinkKind::FileSymbolic, false) == ERROR_ALREADY_EXISTS,
                "Identical source and destination paths must not be overwritten");
        Require(fs::is_regular_file(archiveFile),
                "Protecting an archive destination unexpectedly removed its file");

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
        // Read-only traversal is different from inspecting a reparse point.
        // It must succeed for a normal directory junction on local NTFS.
        Require(tcs::CheckLinkTraversal(junction.wstring()) == ERROR_SUCCESS,
                "Read-only junction traversal must succeed");
        Require(tcs::CheckLinkTraversal((cleanup.path / L"no_such_traversal_file.txt").wstring()) == ERROR_FILE_NOT_FOUND ||
                tcs::CheckLinkTraversal((cleanup.path / L"no_such_traversal_file.txt").wstring()) == ERROR_PATH_NOT_FOUND,
                "Missing target traversal must return a Windows error");
        // From the user's v10 preflight: reject unavailable inputs before
        // a batch and detect file/directory combinations. Automatic remains mixed.
        const auto missingSource = cleanup.path / L"not-installed-file.txt";
        const std::vector<std::wstring> mixedSources{
            sourceFolder.wstring(), original.wstring(), missingSource.wstring()};
        const auto scan = tcs::InspectSelection(mixedSources);
        Require(scan.folders == 1 && scan.files == 1 && scan.unavailable == 1,
                "Preflight did not classify mixed and inaccessible items");
        Require(scan.firstUnavailable == missingSource.wstring(),
                "Preflight did not retain first unavailable source");
        const auto ready = tcs::InspectSelection(
            std::vector<std::wstring>{sourceFolder.wstring(), original.wstring()});
        Require(ready.unavailable == 0 && ready.folders == 1 && ready.files == 1,
                "Preflight rejected valid Automatic selection");

        // Relative paths must stay within a UNC share, not merely
        // the same host. These tests require no live SMB server.
        Require(tcs::RelativeTarget(L"\\\\HOST\\Share\\Links\\file.txt",
                                    L"\\\\host\\share\\Sources\\file.txt") ==
                    L"..\\Sources\\file.txt",
                "Same SMB share should use a relative symlink target");
        Require(tcs::RelativeTarget(L"\\\\HOST\\Share\\Links\\file.txt",
                                    L"\\\\host\\Other\\Sources\\file.txt") ==
                    L"\\\\host\\Other\\Sources\\file.txt",
                "Different SMB shares must use absolute UNC target");
        Require(tcs::RelativeTarget(L"\\\\HOST\\Share\\Links\\file.txt",
                                    L"C:\\Temp\\file.txt") == L"C:\\Temp\\file.txt",
                "Remote-to-local target must never become relative");
        // SMB error diagnostics are pure classification: no network server
        // is required and no permissions or symlink policies are changed.
        Require(tcs::IsRemoteSmbPath(L"\\\\fedora\\Programs\\ScientificTools\\NI"),
            "Standard SMB UNC path was not recognized");
        Require(tcs::IsRemoteSmbPath(L"\\\\?\\UNC\\fedora\\Programs"),
            "Extended SMB UNC path was not recognized");
        Require(!tcs::IsRemoteSmbPath(L"C:\\Users\\Student"),
            "Local disk incorrectly recognized as SMB");
        Require(tcs::DiagnoseLinkOutcome(L"C:\\Users\\Student\\Downloads\\Temp",
                    L"\\\\fedora\\Programs\\ScientificTools\\NI\\Temp",
                    tcs::LinkKind::Automatic, true, ERROR_ACCESS_DENIED) ==
                    tcs::LinkOutcomeDiagnostic::RemoteDirectoryAccessDenied,
                "SMB directory denial must have a precise diagnostic");
        Require(tcs::DiagnoseLinkOutcome(L"C:\\Users\\Student\\script.ps1",
                    L"\\\\fedora\\Programs\\ScientificTools\\NI\\script.ps1",
                    tcs::LinkKind::Automatic, false, ERROR_SUCCESS) ==
                    tcs::LinkOutcomeDiagnostic::RemoteToLocalCreated,
                "Successful SMB file symlinks to local drives need a traversal warning");
        Require(tcs::DiagnoseLinkOutcome(L"\\\\srv\\share\\file",
                    L"\\\\srv\\share\\links\\file", tcs::LinkKind::FileSymbolic,
                    false, ERROR_NOT_SUPPORTED) ==
                    tcs::LinkOutcomeDiagnostic::RemoteOperationUnsupported,
                "Unsupported SMB reparse operation must be classified");
        Require(tcs::DiagnoseLinkOutcome(L"\\\\srv\\share\\file",
                    L"\\\\srv\\share\\links\\file", tcs::LinkKind::FileSymbolic,
                    false, ERROR_BAD_NETPATH) ==
                    tcs::LinkOutcomeDiagnostic::RemoteNetworkUnavailable,
                "Unavailable SMB network path must be classified");
        Require(tcs::DiagnoseLinkOutcome(L"\\\\srv\\share\\file",
                    L"\\\\srv\\other\\links\\file", tcs::LinkKind::FileSymbolic,
                    false, ERROR_SUCCESS) ==
                    tcs::LinkOutcomeDiagnostic::RemoteToRemoteCreated,
                "Cross-share successful links should warn R2R");
        Require(tcs::DiagnoseLinkOutcome(L"\\\\srv\\share\\file",
                    L"\\\\srv\\share\\links\\file", tcs::LinkKind::FileSymbolic,
                    false, ERROR_SUCCESS) ==
                    tcs::LinkOutcomeDiagnostic::None,
                "Same-share successful link has no cross-share warning");
        Require(tcs::DiagnoseLinkOutcome(L"C:\\Temp", L"C:\\LocalLinks\\Temp",
                    tcs::LinkKind::Automatic, true, ERROR_ACCESS_DENIED) ==
                    tcs::LinkOutcomeDiagnostic::None,
                "Local access denied must not be misdiagnosed as SMB");
        // Error explanations are pure text. No SMB connection or ACL change is made.
        auto hasHelp = [](const std::vector<std::wstring>& lines, std::wstring_view needle) {
            return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
                return line.find(needle) != std::wstring::npos;
            });
        };
        const auto smbFolder5 = tcs::help::Explain(
            {ERROR_ACCESS_DENIED, true, true, false, false, true},
            tcs::help::Language::Russian);
        Require(hasHelp(smbFolder5, L"directory reparse point"),
                "Error 5 must explain unsupported directory reparse as a possible cause");
        Require(hasHelp(smbFolder5, L"НЕ доказывает"),
                "Error 5 must not imply file-link success proves directory-link support");
        Require(hasHelp(smbFolder5, L"не меняет настройки Samba"),
                "Error 5 must not change or recommend automatic Samba changes");
        const auto existing = tcs::help::Explain(
            {ERROR_ALREADY_EXISTS, true, true, false, false, true},
            tcs::help::Language::English);
        Require(hasHelp(existing, L"source and link path"),
                "Error 183 must explain identical source/link paths");
        Require(hasHelp(existing, L"Nothing will be overwritten"),
                "Error 183 must preserve existing link destinations");
        const auto privilege = tcs::help::Explain(
            {ERROR_PRIVILEGE_NOT_HELD, false, false, false, false, false},
            tcs::help::Language::English);
        Require(hasHelp(privilege, L"Developer Mode"),
                "Error 1314 must explain Windows Developer Mode");
        const auto invalidReparse = tcs::help::Explain(
            {ERROR_INVALID_REPARSE_DATA, true, false, false, false, true},
            tcs::help::Language::English);
        Require(hasHelp(invalidReparse, L"may already exist"),
                "Reparse verification failure must warn before blindly retrying");
        const auto sambaUnsupported = tcs::help::Explain(
            {ERROR_NOT_SUPPORTED, true, true, false, false, true},
            tcs::help::Language::Hebrew);
        Require(!sambaUnsupported.empty(), "Hebrew SMB unsupported message missing");
        Require(hasHelp(sambaUnsupported, L"Junction"),
                "SMB unsupported operation must explain junction limitation");
        const auto junction = tcs::help::Explain(
            {ERROR_NOT_SUPPORTED, true, true, true, false, true},
            tcs::help::Language::Russian);
        Require(hasHelp(junction, L"локальном томе"),
                "Junction on SMB must identify local-only constraint");
        std::cout << "PASS: Hebrew Unicode Win32 hardlink, junction, symlink checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
