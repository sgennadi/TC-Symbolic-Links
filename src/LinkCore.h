#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace tcs {
enum class LinkKind { Automatic, FileSymbolic, DirectorySymbolic, Junction, Hardlink };
enum class ReparseKind { None, SymbolicFile, SymbolicDirectory, Junction, Other };

struct LinkDetails {
    ReparseKind kind = ReparseKind::None;
    std::wstring target;
    bool relative = false;
    bool targetExists = false;
    DWORD hardlinkCount = 0;
    DWORD error = ERROR_SUCCESS;
};

std::wstring FullPath(std::wstring_view input);
bool IsExistingDirectory(const std::wstring& path);
std::wstring ParentPath(const std::wstring& path);
std::wstring BaseName(const std::wstring& path);
std::wstring Join(const std::wstring& folder, const std::wstring& name);
std::wstring WindowsErrorMessage(DWORD error);
// Diagnostic classification is deliberately read-only: never retry with elevated
// privileges, create a directory as a substitute, or change SMB policy.
enum class LinkOutcomeDiagnostic {
    None,
    RemoteDirectoryAccessDenied,
    RemoteFileAccessDenied,
    RemoteToLocalCreated
};
bool IsRemoteSmbPath(std::wstring_view path);
LinkOutcomeDiagnostic DiagnoseLinkOutcome(std::wstring_view source,
    std::wstring_view destination, LinkKind kind, bool sourceIsDirectory,
    DWORD windowsResult);

// Preflight: inspect the complete TC %WL selection before mutating anything.
// Missing/inaccessible sources and explicit link-type mismatches stop the batch.
struct SelectionSummary {
    std::size_t files = 0;
    std::size_t folders = 0;
    std::size_t unavailable = 0;
    std::wstring firstUnavailable;
};
SelectionSummary InspectSelection(const std::vector<std::wstring>& selected);

std::wstring RelativeTarget(const std::wstring& destinationLink, const std::wstring& source);
LinkDetails InspectLink(const std::wstring& path, bool countHardlinks = false);
DWORD CreateLink(const std::wstring& source, const std::wstring& destination,
                 LinkKind kind, bool preferRelative);
}
