#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// UTF-16 CSV builder; the native UI converts it to UTF-8 with a BOM.
// Escapes CSV quotes and neutralizes spreadsheet formula injection.
namespace tcs::audit {
struct Record {
    std::wstring path, kind, health, target, resolvedTarget;
    bool relative = false;
    std::uint32_t hardlinks = 0;
    std::uint32_t error = 0;
};
inline std::wstring Cell(std::wstring_view text) {
    std::wstring value;
    value.reserve(text.size() + 3);
    for (wchar_t ch : text)
        value.push_back(ch == L'\r' || ch == L'\n' || ch == L'\t' ||
                        ch == L'\0' ? L' ' : ch);
    const auto first = value.find_first_not_of(L' ');
    if (first != std::wstring::npos &&
        (value[first] == L'=' || value[first] == L'+' ||
         value[first] == L'-' || value[first] == L'@'))
        value.insert(first, 1, L'\'');
    std::wstring csv = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') csv.push_back(L'\"');
        csv.push_back(ch);
    }
    csv += L'\"';
    return csv;
}
inline std::wstring Build(const std::vector<Record>& rows) {
    std::wstring csv = L"Path,Type,Health,Target,Resolved Target,Relative,Hardlinks,Win32 Error\r\n";
    for (const auto& row : rows) {
        csv += Cell(row.path) + L"," + Cell(row.kind) + L"," +
            Cell(row.health) + L"," + Cell(row.target) + L"," +
            Cell(row.resolvedTarget) + L"," +
            (row.relative ? L"Yes" : L"No") + L"," +
            std::to_wstring(row.hardlinks) + L"," +
            std::to_wstring(row.error) + L"\r\n";
    }
    return csv;
}
} // namespace tcs::audit
