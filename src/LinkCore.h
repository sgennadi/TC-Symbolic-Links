#pragma once
#include <windows.h>
#include <string>
#include <string_view>

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
std::wstring RelativeTarget(const std::wstring& destinationLink, const std::wstring& source);
LinkDetails InspectLink(const std::wstring& path, bool countHardlinks = false);
DWORD CreateLink(const std::wstring& source, const std::wstring& destination,
                 LinkKind kind, bool preferRelative);
}
