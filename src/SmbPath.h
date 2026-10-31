#pragma once
#include <cwctype>
#include <string>
#include <string_view>

// UNC share boundaries must be respected when constructing relative links.
// This parser performs no network access and does not guess server vendors.
namespace tcs::smb {
inline std::wstring ShareRoot(std::wstring_view path) {
    if (path.starts_with(L"\\\\?\\UNC\\") || path.starts_with(L"\\??\\UNC\\"))
        return ShareRoot(L"\\\\" + std::wstring(path.substr(8)));
    if (!path.starts_with(L"\\\\") || path.starts_with(L"\\\\?\\") ||
        path.starts_with(L"\\\\.\\")) return {};
    const auto serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring_view::npos || serverEnd == 2 ||
        serverEnd + 1 == path.size()) return {};
    const auto shareEnd = path.find(L'\\', serverEnd + 1);
    if (shareEnd == serverEnd + 1) return {};
    return std::wstring(path.substr(0, shareEnd));
}
inline bool SameShare(std::wstring_view left, std::wstring_view right) {
    const auto a = ShareRoot(left), b = ShareRoot(right);
    if (a.empty() || b.empty() || a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}
} // namespace tcs::smb
