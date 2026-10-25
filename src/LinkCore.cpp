#include "LinkCore.h"
#include "ReparseData.h"
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <string_view>
#include <vector>

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

namespace tcs {
namespace {
std::wstring ApiPath(std::wstring path) {
    // Explicit prefixes let NTFS operations work with paths longer than MAX_PATH.
    if (path.starts_with(L"\\\\?\\")) return path;
    if (path.size() < MAX_PATH - 15) return path;
    if (path.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\')
        return L"\\\\?\\" + path;
    return path;
}

std::wstring Unprefix(std::wstring path) {
    if (path.starts_with(L"\\\\?\\UNC\\")) return L"\\\\" + path.substr(8);
    if (path.starts_with(L"\\\\?\\")) return path.substr(4);
    if (path.starts_with(L"\\??\\UNC\\")) return L"\\\\" + path.substr(8);
    if (path.starts_with(L"\\??\\")) return path.substr(4);
    return path;
}

bool IsAbsolute(std::wstring_view p) {
    return (p.size() >= 3 && ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z'))
            && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) ||
           p.starts_with(L"\\\\") || p.starts_with(L"\\??\\");
}

bool ReadReparse(const std::wstring& path, DWORD& tag,
                 std::wstring& target, bool& relative) {
    auto handle = CreateFileW(ApiPath(path).c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::array<BYTE, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer{};
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                      buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr);
    CloseHandle(handle);
    if (!ok || got > buffer.size()) return false;

    tcs::reparse::Details parsed;
    if (!tcs::reparse::Parse(std::as_bytes(std::span(buffer.data(), got)), parsed))
        return false;
    tag = parsed.kind == tcs::reparse::Kind::SymbolicLink ? IO_REPARSE_TAG_SYMLINK :
        parsed.kind == tcs::reparse::Kind::MountPoint ? IO_REPARSE_TAG_MOUNT_POINT : 0;
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    target.assign(parsed.target.begin(), parsed.target.end());
    relative = parsed.relative;
    return true;
}

DWORD MakeJunction(const std::wstring& source, const std::wstring& destination) {
    // Directory junctions must point to a local, fully qualified volume path.
    const auto actual = FullPath(source);
    if (actual.size() < 3 || actual[1] != L':' || actual[2] != L'\\')
        return ERROR_NOT_SUPPORTED;
    const std::wstring substitute = L"\\??\\" + actual;
    const std::wstring print = actual;
    const auto subBytes = substitute.size() * sizeof(WCHAR);
    const auto printBytes = print.size() * sizeof(WCHAR);
    const auto length = 8 + subBytes + 2 + printBytes + 2;
    if (length > std::numeric_limits<WORD>::max() ||
        8 + length > MAXIMUM_REPARSE_DATA_BUFFER_SIZE) return ERROR_FILENAME_EXCED_RANGE;
    if (!CreateDirectoryW(ApiPath(destination).c_str(), nullptr)) return GetLastError();
    HANDLE h = CreateFileW(ApiPath(destination).c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        RemoveDirectoryW(ApiPath(destination).c_str());
        return err;
    }
    std::vector<BYTE> data(8 + length, 0);
    auto set16 = [&](std::size_t off, WORD value) {
        data[off] = static_cast<BYTE>(value & 0xff);
        data[off + 1] = static_cast<BYTE>((value >> 8) & 0xff);
    };
    auto set32 = [&](std::size_t off, DWORD value) {
        set16(off, static_cast<WORD>(value & 0xffff));
        set16(off + 2, static_cast<WORD>(value >> 16));
    };
    set32(0, IO_REPARSE_TAG_MOUNT_POINT);
    set16(4, static_cast<WORD>(length));
    set16(8, 0);
    set16(10, static_cast<WORD>(subBytes));
    set16(12, static_cast<WORD>(subBytes + 2));
    set16(14, static_cast<WORD>(printBytes));
    std::memcpy(data.data() + 16, substitute.c_str(), subBytes);
    std::memcpy(data.data() + 16 + subBytes + 2, print.c_str(), printBytes);
    DWORD returned = 0;
    const BOOL success = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, data.data(),
                                          static_cast<DWORD>(data.size()), nullptr, 0, &returned, nullptr);
    const DWORD error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    if (!success) RemoveDirectoryW(ApiPath(destination).c_str());
    return error;
}
}

std::wstring FullPath(std::wstring_view input) {
    if (input.empty()) return {};
    std::wstring str(input);
    const auto capacity = GetFullPathNameW(str.c_str(), 0, nullptr, nullptr);
    if (!capacity || capacity > 32767) return {};
    std::wstring result(capacity, L'\0');
    DWORD actual = GetFullPathNameW(str.c_str(), capacity, result.data(), nullptr);
    if (!actual || actual >= capacity) return {};
    result.resize(actual);
    return Unprefix(std::move(result));
}
bool IsExistingDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(ApiPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring ParentPath(const std::wstring& path) {
    return std::filesystem::path(path).parent_path().wstring();
}
std::wstring BaseName(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}
std::wstring Join(const std::wstring& folder, const std::wstring& name) {
    return (std::filesystem::path(folder) / name).wstring();
}
std::wstring WindowsErrorMessage(DWORD error) {
    LPWSTR message = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring result = (n && message) ? std::wstring(message, n) : L"Windows error " + std::to_wstring(error);
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        result.pop_back();
    return result;
}

std::wstring RelativeTarget(const std::wstring& destinationLink, const std::wstring& source) {
    namespace fs = std::filesystem;
    const fs::path from(FullPath(ParentPath(destinationLink)));
    const fs::path to(FullPath(source));
    if (from.empty() || to.empty() ||
        _wcsicmp(from.root_name().c_str(), to.root_name().c_str()) != 0 ||
        from.has_root_directory() != to.has_root_directory()) return source;
    const auto fromRelative = from.relative_path();
    const auto toRelative = to.relative_path();
    auto a = fromRelative.begin();
    auto b = toRelative.begin();
    const auto aEnd = fromRelative.end();
    const auto bEnd = toRelative.end();
    // Compare components without case sensitivity, as on usual Windows volumes.
    while (a != aEnd && b != bEnd && _wcsicmp(a->c_str(), b->c_str()) == 0) {
        ++a; ++b;
    }
    fs::path result;
    for (; a != aEnd; ++a) result /= L"..";
    for (; b != bEnd; ++b) result /= *b;
    return result.empty() ? source : result.wstring();
}

LinkDetails InspectLink(const std::wstring& path, bool countHardlinks) {
    LinkDetails result;
    const auto attr = GetFileAttributesW(ApiPath(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        result.error = GetLastError();
        return result;
    }
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        DWORD tag = 0;
        if (ReadReparse(path, tag, result.target, result.relative)) {
            if (tag == IO_REPARSE_TAG_SYMLINK)
                result.kind = (attr & FILE_ATTRIBUTE_DIRECTORY) ?
                    ReparseKind::SymbolicDirectory : ReparseKind::SymbolicFile;
            else if (tag == IO_REPARSE_TAG_MOUNT_POINT) result.kind = ReparseKind::Junction;
            else result.kind = ReparseKind::Other;
            if (!result.target.empty()) {
                auto resolved = result.relative ? Join(ParentPath(path), result.target) : result.target;
                result.targetExists = GetFileAttributesW(ApiPath(FullPath(resolved)).c_str()) != INVALID_FILE_ATTRIBUTES;
            }
        } else {
            result.kind = ReparseKind::Other;
            result.error = ERROR_INVALID_REPARSE_DATA;
        }
    }
    if (countHardlinks && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        HANDLE h = CreateFileW(ApiPath(path).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (GetFileInformationByHandle(h, &info)) result.hardlinkCount = info.nNumberOfLinks;
            CloseHandle(h);
        }
    }
    return result;
}

DWORD CreateLink(const std::wstring& source, const std::wstring& destination,
                 LinkKind kind, bool preferRelative) {
    const auto src = FullPath(source);
    const auto dst = FullPath(destination);
    if (src.empty() || dst.empty() || !IsAbsolute(src) || !IsAbsolute(dst))
        return ERROR_INVALID_NAME;
    if (_wcsicmp(src.c_str(), dst.c_str()) == 0) return ERROR_ALREADY_EXISTS;
    const DWORD attributes = GetFileAttributesW(ApiPath(src).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (kind == LinkKind::Automatic)
        kind = directory ? LinkKind::DirectorySymbolic : LinkKind::FileSymbolic;
    if ((kind == LinkKind::DirectorySymbolic || kind == LinkKind::Junction) && !directory)
        return ERROR_DIRECTORY;
    if ((kind == LinkKind::FileSymbolic || kind == LinkKind::Hardlink) && directory)
        return ERROR_DIRECTORY;
    if (kind == LinkKind::Hardlink) {
        if (CreateHardLinkW(ApiPath(dst).c_str(), ApiPath(src).c_str(), nullptr)) return ERROR_SUCCESS;
        return GetLastError();
    }
    if (kind == LinkKind::Junction) return MakeJunction(src, dst);
    const bool dirlink = kind == LinkKind::DirectorySymbolic;
    const std::wstring target = preferRelative ? RelativeTarget(dst, src) : src;
    const DWORD flags = (dirlink ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0) |
                         SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkW(ApiPath(dst).c_str(),
        IsAbsolute(target) ? ApiPath(target).c_str() : target.c_str(), flags)) return ERROR_SUCCESS;
    DWORD error = GetLastError();
    // Older Windows versions do not recognize ALLOW_UNPRIVILEGED_CREATE.
    if (error == ERROR_INVALID_PARAMETER) {
        if (CreateSymbolicLinkW(ApiPath(dst).c_str(),
            IsAbsolute(target) ? ApiPath(target).c_str() : target.c_str(),
            (dirlink ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0))) return ERROR_SUCCESS;
        error = GetLastError();
    }
    return error;
}
}
