// Isolated UNC/SMB smoke test. Designed to run against an ephemeral share
// provisioned by CI. All created items live inside a unique child directory.
// No SMB, filesystem, or Windows symlink policy is modified by this process.
#include "LinkCore.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace {
void Require(bool value, const char* description) {
    if (!value) throw std::runtime_error(description);
}
void WriteOneFile(const fs::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Could not create SMB test source file");
    const char data[] = "tcs isolated smb smoke";
    DWORD written = 0;
    const BOOL success = WriteFile(file, data, sizeof(data), &written, nullptr);
    CloseHandle(file);
    Require(success && written == sizeof(data), "Could not write SMB source file");
}
bool IsPolicyOrCapabilityFailure(DWORD error) {
    return error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD ||
           error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION ||
           error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_REPARSE_DATA ||
           error == ERROR_BAD_NETPATH || error == ERROR_BAD_NET_NAME ||
           error == ERROR_NETWORK_UNREACHABLE;
}
struct OwnDirectory {
    fs::path root;
    fs::path fileLink;
    fs::path dirLink;
    fs::path mappedFileLink;
    ~OwnDirectory() {
        // Delete links without following them, before deleting our sources.
        if (!fileLink.empty()) DeleteFileW(fileLink.c_str());
        if (!dirLink.empty()) RemoveDirectoryW(dirLink.c_str());
        if (!mappedFileLink.empty()) DeleteFileW(mappedFileLink.c_str());
        if (!root.empty()) {
            std::error_code error;
            fs::remove_all(root, error);
            if (error)
                std::cerr << "CLEANUP_WARNING: " << error.message() << '\n';
        }
    }
};
struct Result {
    bool created = false;
    bool traversable = false;
    DWORD error = ERROR_SUCCESS;
};
Result TestLink(const fs::path& source, const fs::path& destination,
                tcs::LinkKind kind, tcs::ReparseKind expected) {
    Result result;
    result.error = tcs::CreateLink(source.wstring(), destination.wstring(), kind, true);
    if (result.error != ERROR_SUCCESS) {
        if (!IsPolicyOrCapabilityFailure(result.error)) {
            std::cerr << "CREATE_ERROR=" << result.error << " type="
                      << (kind == tcs::LinkKind::DirectorySymbolic ? "directory" : "file") << '\n';
            Require(false, "Unexpected SMB symlink creation error");
        }
        std::cout << (kind == tcs::LinkKind::DirectorySymbolic ?
            "SMB_DIRECTORY=" : "SMB_FILE=") << "BLOCKED (" << result.error << ")\n";
        return result;
    }
    result.created = true;
    const auto verified = tcs::InspectLink(destination.wstring());
    Require(verified.kind == expected,
            "SMB reported successful creation but reparse type was wrong");
    Require(verified.relative, "Same-share SMB symlink was not stored as relative");
    const DWORD traversal = tcs::CheckLinkTraversal(destination.wstring());
    result.traversable = traversal == ERROR_SUCCESS;
    std::cout << (kind == tcs::LinkKind::DirectorySymbolic ?
        "SMB_DIRECTORY=" : "SMB_FILE=") << "CREATED; READBACK=PASS; TRAVERSAL="
              << (result.traversable ? "PASS" : "BLOCKED")
              << " (" << traversal << ")\n";
    return result;
}
}

int wmain() {
    try {
        wchar_t unc[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"TC_SMB_SMOKE_ROOT", unc, 32768);
        if (!length || length >= 32768) {
            std::cout << "SKIP: TC_SMB_SMOKE_ROOT not configured\n";
            return 77;
        }
        const fs::path share(unc);
        Require(tcs::IsRemoteSmbPath(share.wstring()),
                "TC_SMB_SMOKE_ROOT must be a UNC share path");
        Require(tcs::IsExistingDirectory(share.wstring()),
                "SMB smoke share cannot be accessed");
        const std::wstring unique = L"TCS-" + std::to_wstring(GetCurrentProcessId()) +
                                    L"-" + std::to_wstring(GetTickCount64());
        OwnDirectory own;
        own.root = share / unique;
        Require(fs::create_directory(own.root), "Cannot create isolated SMB test directory");
        const fs::path sourceDir = own.root / L"Hebrew שלום source";
        const fs::path targetDir = own.root / L"Links קישורים";
        fs::create_directory(sourceDir);
        fs::create_directory(targetDir);
        const fs::path original = sourceDir / L"file שלום.txt";
        WriteOneFile(original);
        own.fileLink = targetDir / L"file-link שלום.txt";
        own.dirLink = targetDir / L"folder-link שלום";
        const Result file = TestLink(original, own.fileLink, tcs::LinkKind::FileSymbolic,
                                     tcs::ReparseKind::SymbolicFile);
        const Result dir = TestLink(sourceDir, own.dirLink, tcs::LinkKind::DirectorySymbolic,
                                    tcs::ReparseKind::SymbolicDirectory);
        // An optional ephemeral drive-letter mapping exercises the same-share
        // normalization (source via UNC, destination via e.g. Z:). CI owns
        // and removes this mapping outside this test executable.
        bool mappedCreated = false;
        wchar_t mappedRoot[32768]{};
        const DWORD mappedLen = GetEnvironmentVariableW(
            L"TC_SMB_MAPPED_ROOT", mappedRoot, 32768);
        if (mappedLen > 0 && mappedLen < 32768) {
            const fs::path mappedTarget = fs::path(mappedRoot) / unique /
                L"Links קישורים" / L"mapped-file שלום.txt";
            const std::wstring target = tcs::RelativeTarget(
                mappedTarget.wstring(), original.wstring());
            Require(target == L"..\\Hebrew שלום source\\file שלום.txt",
                "Mapped-drive to UNC source did not produce same-share relative path");
            own.mappedFileLink = mappedTarget;
            const Result mapped = TestLink(original, mappedTarget,
                tcs::LinkKind::FileSymbolic, tcs::ReparseKind::SymbolicFile);
            mappedCreated = mapped.created;
            std::cout << "SMB_MAPPED=" << (mappedCreated ? "CREATED" : "BLOCKED")
                      << '
';
        } else {
            std::cout << "SMB_MAPPED=NOT_CONFIGURED
";
        }
        // Remote junction must never leave a placeholder directory behind.
        const fs::path forbidden = targetDir / L"Junction-must-not-exist";
        const DWORD junction = tcs::CreateLink(sourceDir.wstring(), forbidden.wstring(),
                                                tcs::LinkKind::Junction, false);
        Require(junction == ERROR_NOT_SUPPORTED, "Remote junction was unexpectedly permitted");
        Require(GetFileAttributesW(forbidden.c_str()) == INVALID_FILE_ATTRIBUTES,
                "Remote junction failed but left an orphaned directory");
        if (!file.created && !dir.created && !mappedCreated) {
            std::cout << "SKIP: No SMB symlink creation permitted by this test endpoint\n";
            return 77;
        }
        std::cout << "PASS: Isolated SMB link creation/readback tested; "
                  << "unsupported types are reported separately\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
