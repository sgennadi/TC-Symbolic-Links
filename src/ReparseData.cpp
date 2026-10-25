#include "ReparseData.h"

namespace tcs::reparse {
namespace {

std::uint16_t Read16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned>(bytes[offset]) |
        (std::to_integer<unsigned>(bytes[offset + 1]) << 8));
}

std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset) {
    return std::uint32_t(Read16(bytes, offset)) |
        (std::uint32_t(Read16(bytes, offset + 2)) << 16);
}

bool ValidUtf16(std::u16string_view text) {
    bool high = false;
    for (const char16_t ch : text) {
        const bool low = ch >= 0xDC00 && ch <= 0xDFFF;
        if (low != high || ch == 0) return false;
        high = ch >= 0xD800 && ch <= 0xDBFF;
    }
    return !high;
}

bool Extract(std::span<const std::byte> raw, std::size_t stringStart,
             std::size_t stringLength, std::uint16_t offset,
             std::uint16_t byteCount, std::u16string& output) {
    if ((offset & 1U) || (byteCount & 1U) || offset > stringLength ||
        byteCount > stringLength - offset) return false;
    output.clear();
    output.reserve(byteCount / 2);
    for (std::size_t n = 0; n < byteCount; n += 2)
        output.push_back(static_cast<char16_t>(Read16(raw, stringStart + offset + n)));
    return ValidUtf16(output);
}

void NormalizeNtPrefix(std::u16string& path) {
    // The PrintName can be empty for some valid Windows reparse points.
    // The SubstituteName then uses an NT-native prefix rather than a DOS path.
    if (path.starts_with(u"\\??\\UNC\\") || path.starts_with(u"\\\\?\\UNC\\"))
        path = u"\\\\" + path.substr(8);
    else if (path.starts_with(u"\\??\\") || path.starts_with(u"\\\\?\\"))
        path.erase(0, 4);
}

} // namespace

bool Parse(std::span<const std::byte> raw, Details& details) {
    details = {};
    if (raw.size() < 8) return false;
    const std::uint32_t tag = Read32(raw, 0);
    const std::size_t dataLength = Read16(raw, 4);
    if (dataLength > raw.size() - 8) return false;
    if (tag != kSymlinkTag && tag != kMountPointTag) {
        details.kind = Kind::Other;
        return true;
    }

    const bool isSymlink = tag == kSymlinkTag;
    const std::size_t pathStart = isSymlink ? 20 : 16;
    if (dataLength < pathStart - 8) return false;
    const std::size_t pathBytes = dataLength - (pathStart - 8);
    const std::uint16_t subOffset = Read16(raw, 8);
    const std::uint16_t subBytes = Read16(raw, 10);
    const std::uint16_t printOffset = Read16(raw, 12);
    const std::uint16_t printBytes = Read16(raw, 14);

    // Validate both names, even when the PrintName is preferred, so corrupt
    // offsets cannot be hidden in the SubstituteName.
    std::u16string substitute, print;
    if (!Extract(raw, pathStart, pathBytes, subOffset, subBytes, substitute) ||
        !Extract(raw, pathStart, pathBytes, printOffset, printBytes, print)) return false;
    if (isSymlink) details.relative = (Read32(raw, 16) & 1U) != 0;
    details.kind = isSymlink ? Kind::SymbolicLink : Kind::MountPoint;
    details.target = !print.empty() ? std::move(print) : std::move(substitute);
    if (print.empty()) NormalizeNtPrefix(details.target);
    return true;
}

} // namespace tcs::reparse
