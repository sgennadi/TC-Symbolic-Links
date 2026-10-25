// Total Commander / Double Commander WDX content plugin.
// Windows build: PE .wdx64 backed by Win32/NTFS LinkCore.
// Linux build: ELF .wdx backed by POSIX lstat/readlink/stat.
// WDX API uses UTF-16 code units on BOTH systems: Linux wchar_t is not WCHAR.
#ifdef _WIN32
#include "LinkCore.h"
#include "TcIntegration.h"
#include <windows.h>
#else
#include <cerrno>
#include <climits>
#include <cstdint>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#endif
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace {
constexpr int ft_nomorefields = 0;
constexpr int ft_numeric_32 = 1;
constexpr int ft_string = 8;
constexpr int ft_stringw = 11;
constexpr int ft_nosuchfield = -1;
constexpr int ft_fieldempty = -3;
constexpr int ft_fileerror = -2;
constexpr const char* kFields[] = {
    "Link type", "Link target", "Target exists", "Hardlink count"
};

#ifdef _WIN32
using PluginWideChar = wchar_t;
#define WDX_EXPORT extern "C" __declspec(dllexport)
#define WDX_CALL __stdcall
#else
// doublecmd/plugins/sdk/common.h defines WCHAR as uint16_t on Linux.
using PluginWideChar = char16_t;
#define WDX_EXPORT extern "C" __attribute__((visibility("default")))
#define WDX_CALL
#endif
static_assert(sizeof(PluginWideChar) == 2, "WDX WCHAR must have 16 bits");

void CopyBytes(std::string_view value, char* output, int maxBytes) {
    if (!output || maxBytes <= 0) return;
    const std::size_t length = (std::min)(value.size(), static_cast<std::size_t>(maxBytes - 1));
    std::memcpy(output, value.data(), length);
    output[length] = 0;
}

std::u16string ReadWide(const PluginWideChar* input) {
    if (!input) return {};
    std::u16string out;
    for (; *input; ++input) out.push_back(static_cast<char16_t>(*input));
    return out;
}

bool IsHighSurrogate(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// Encoding/decoding routines are strict: never normalize Hebrew niqqud and
// never split surrogate pairs or silently replace invalid path characters.
bool Utf16ToUtf8(std::u16string_view input, std::string& output) {
    output.clear();
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char16_t u = input[i];
        if (!u) return false;
        std::uint32_t cp = u;
        if (IsHighSurrogate(u)) {
            if (i + 1 >= input.size() || !IsLowSurrogate(input[i + 1])) return false;
            cp = 0x10000 + ((u - 0xD800) << 10) + (input[++i] - 0xDC00);
        } else if (IsLowSurrogate(u)) {
            return false;
        }
        if (cp <= 0x7F) output.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return true;
}

bool Utf8ToUtf16(std::string_view input, std::u16string& output) {
    output.clear();
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i++]);
        std::uint32_t cp = 0;
        unsigned extra = 0;
        if (first <= 0x7F) cp = first;
        else if (first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; extra = 1; }
        else if (first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; extra = 2; }
        else if (first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; extra = 3; }
        else return false;
        if (i + extra > input.size()) return false;
        for (unsigned j = 0; j < extra; ++j) {
            const auto next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp == 0) return false;
        if (cp <= 0xFFFF) output.push_back(static_cast<char16_t>(cp));
        else {
            cp -= 0x10000;
            output.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            output.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    return true;
}

enum class Type { None, FileSymlink, DirectorySymlink, Junction, Other };
struct Details {
    Type type = Type::None;
    std::u16string target;
    bool targetExists = false;
    std::uint32_t hardlinkCount = 0;
    bool error = false;
};

#ifdef _WIN32
Details Inspect(const std::u16string& path, bool hardlinks) {
    Details info;
    const auto raw = tcs::InspectLink(std::wstring(path.begin(), path.end()), hardlinks);
    info.error = raw.error != ERROR_SUCCESS && raw.kind == tcs::ReparseKind::None;
    info.target = std::u16string(raw.target.begin(), raw.target.end());
    info.targetExists = raw.targetExists;
    info.hardlinkCount = raw.hardlinkCount;
    switch (raw.kind) {
        case tcs::ReparseKind::SymbolicFile: info.type = Type::FileSymlink; break;
        case tcs::ReparseKind::SymbolicDirectory: info.type = Type::DirectorySymlink; break;
        case tcs::ReparseKind::Junction: info.type = Type::Junction; break;
        case tcs::ReparseKind::Other: info.type = Type::Other; break;
        default: break;
    }
    return info;
}
#else
Details Inspect(const std::u16string& path, bool hardlinks) {
    Details info;
    std::string native;
    if (!Utf16ToUtf8(path, native)) { info.error = true; return info; }
    struct stat file{};
    if (lstat(native.c_str(), &file) != 0) { info.error = true; return info; }
    if (S_ISLNK(file.st_mode)) {
        std::vector<char> target(256);
        ssize_t bytes = 0;
        for (;;) {
            bytes = readlink(native.c_str(), target.data(), target.size());
            if (bytes < 0) { info.error = true; return info; }
            if (static_cast<std::size_t>(bytes) < target.size()) break;
            if (target.size() >= 1 << 20) { info.error = true; return info; }
            target.resize(target.size() * 2);
        }
        if (!Utf8ToUtf16(std::string_view(target.data(), static_cast<std::size_t>(bytes)), info.target)) {
            info.error = true;
            return info;
        }
        struct stat resolved{};
        info.targetExists = stat(native.c_str(), &resolved) == 0;
        // POSIX does not store the symlink's intended type. For broken
        // symlinks the type cannot be determined, so report FileSymlink.
        info.type = (info.targetExists && S_ISDIR(resolved.st_mode))
            ? Type::DirectorySymlink : Type::FileSymlink;
    } else if (hardlinks && S_ISREG(file.st_mode) && file.st_nlink > 1) {
        info.hardlinkCount = static_cast<std::uint32_t>((std::min)(
            static_cast<std::uintmax_t>(file.st_nlink),
            static_cast<std::uintmax_t>((std::numeric_limits<std::int32_t>::max)())));
    }
    return info;
}
#endif

int GetContent(const std::u16string& path, int index, void* output, int maxBytes, bool unicode) {
    if (index < 0 || index >= 4) return ft_nosuchfield;
    if (path.empty() || !output || maxBytes <= 0) return ft_fileerror;
    const auto info = Inspect(path, index == 0 || index == 3);
    if (info.error) return ft_fileerror;
    if (index == 3) {
        if (maxBytes < static_cast<int>(sizeof(std::int32_t))) return ft_fileerror;
        if (info.hardlinkCount <= 1) return ft_fieldempty;
        const std::int32_t count = static_cast<std::int32_t>(info.hardlinkCount);
        std::memcpy(output, &count, sizeof(count));
        return ft_numeric_32;
    }
    std::u16string value;
    switch (index) {
        case 0:
            switch (info.type) {
                case Type::DirectorySymlink: value = u"Directory symlink"; break;
                case Type::FileSymlink: value = u"File symlink"; break;
                case Type::Junction: value = u"Junction / mount point"; break;
                case Type::Other: value = u"Other reparse point"; break;
                case Type::None:
                    if (info.hardlinkCount > 1) value = u"Hardlinked file";
                    break;
            }
            break;
        case 1: value = info.target; break;
        case 2:
            if (info.type != Type::None && !info.target.empty())
                value = info.targetExists ? u"Yes" : u"No";
            break;
        default: return ft_nosuchfield;
    }
    if (value.empty()) return ft_fieldempty;
    if (unicode) {
        const std::size_t capacity = static_cast<std::size_t>(maxBytes) / sizeof(PluginWideChar);
        if (capacity < 2) return ft_fileerror;
        std::size_t n = (std::min)(value.size(), capacity - 1);
        if (n && n < value.size() && IsHighSurrogate(value[n - 1]) && IsLowSurrogate(value[n]))
            --n;
        for (std::size_t i = 0; i < n; ++i)
            static_cast<PluginWideChar*>(output)[i] = static_cast<PluginWideChar>(value[i]);
        static_cast<PluginWideChar*>(output)[n] = 0;
        return ft_stringw;
    }
#ifdef _WIN32
    const std::wstring wide(value.begin(), value.end());
    const int required = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return ft_fileerror;
    std::string narrow(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, narrow.data(), required, nullptr, nullptr) <= 0)
        return ft_fileerror;
    CopyBytes(narrow, static_cast<char*>(output), maxBytes);
#else
    std::string narrow;
    if (!Utf16ToUtf8(value, narrow)) return ft_fileerror;
    CopyBytes(narrow, static_cast<char*>(output), maxBytes);
#endif
    return ft_string;
}
} // namespace

#ifdef _WIN32
// Total Commander invokes this after loading the WDX. Its suggested INI lives
// alongside wincmd.ini, but COMMANDER_INI (when present) is preferred.
struct ContentDefaultParamStruct {
    int size;
    DWORD interfaceLow;
    DWORD interfaceHigh;
    char defaultIniName[MAX_PATH];
};
WDX_EXPORT void WDX_CALL ContentSetDefaultParams(ContentDefaultParamStruct* params) {
    tcs::AutoConfigureOnFirstLoad(params && params->size >= sizeof(ContentDefaultParamStruct)
                                 ? params->defaultIniName : nullptr);
}
#endif

WDX_EXPORT int WDX_CALL ContentGetSupportedField(int index, char* name, char* units, int maxlen) {
    if (index < 0 || index >= 4) return ft_nomorefields;
    CopyBytes(kFields[index], name, maxlen);
    if (units && maxlen > 0) units[0] = 0;
    return index == 3 ? ft_numeric_32 : ft_string;
}
WDX_EXPORT int WDX_CALL ContentGetValueW(PluginWideChar* filename, int index, int /*unit*/,
                                         void* result, int maxlen, int /*flags*/) {
    if (!filename) return ft_fileerror;
    return GetContent(ReadWide(filename), index, result, maxlen, true);
}
WDX_EXPORT int WDX_CALL ContentGetValue(char* filename, int index, int /*unit*/,
                                        void* result, int maxlen, int /*flags*/) {
    if (!filename) return ft_fileerror;
    std::u16string wide;
#ifdef _WIN32
    const int chars = MultiByteToWideChar(CP_ACP, 0, filename, -1, nullptr, 0);
    if (chars <= 0) return ft_fileerror;
    std::wstring temp(static_cast<std::size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, filename, -1, temp.data(), chars) <= 0)
        return ft_fileerror;
    wide.assign(temp.begin(), temp.end() - 1);
#else
    if (!Utf8ToUtf16(filename, wide)) return ft_fileerror;
#endif
    return GetContent(wide, index, result, maxlen, false);
}
#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
#endif
