#include "ReparseData.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Byte = std::byte;
using Buffer = std::vector<Byte>;
using Kind = tcs::reparse::Kind;

void Require(bool truth, const char* message) {
    if (!truth) throw std::runtime_error(message);
}
void Put16(Buffer& data, std::size_t at, std::uint16_t value) {
    data.at(at) = Byte(value & 255U);
    data.at(at + 1) = Byte((value >> 8) & 255U);
}
void Put32(Buffer& data, std::size_t at, std::uint32_t value) {
    Put16(data, at, static_cast<std::uint16_t>(value));
    Put16(data, at + 2, static_cast<std::uint16_t>(value >> 16));
}
void AppendWide(Buffer& bytes, std::u16string_view text) {
    for (char16_t ch : text) {
        bytes.push_back(Byte(ch & 255U));
        bytes.push_back(Byte((ch >> 8) & 255U));
    }
}
Buffer Point(std::uint32_t tag, std::u16string_view substitute,
             std::u16string_view display, bool relative = false) {
    const bool symlink = tag == tcs::reparse::kSymlinkTag;
    Buffer bytes(symlink ? 20 : 16, Byte{0});
    Put32(bytes, 0, tag);
    Put16(bytes, 8, 0);
    Put16(bytes, 10, static_cast<std::uint16_t>(substitute.size() * 2));
    Put16(bytes, 12, static_cast<std::uint16_t>(substitute.size() * 2));
    Put16(bytes, 14, static_cast<std::uint16_t>(display.size() * 2));
    if (symlink && relative) Put32(bytes, 16, 1);
    AppendWide(bytes, substitute);
    AppendWide(bytes, display);
    Put16(bytes, 4, static_cast<std::uint16_t>(bytes.size() - 8));
    return bytes;
}
void Expect(Buffer bytes, Kind kind, std::u16string_view target, bool relative) {
    tcs::reparse::Details got;
    Require(tcs::reparse::Parse(bytes, got), "Valid reparse point rejected");
    Require(got.kind == kind, "Wrong reparse point kind");
    Require(got.target == target, "Hebrew UTF-16 target has changed");
    Require(got.relative == relative, "Relative flag incorrect");
}
void Reject(Buffer bytes) {
    tcs::reparse::Details got;
    Require(!tcs::reparse::Parse(bytes, got), "Malformed reparse point accepted");
}
}

int main() {
    // A junction that a Windows machine creates; the logical source names
    // and niqqud must survive verbatim, not normalized or displayed backwards.
    const std::u16string folder = u"C:\\ספרייה\\פרויקט 2026\\ש\u05B8לו\u05B9ם";
    auto junction = Point(tcs::reparse::kMountPointTag, u"\\??\\" + folder, folder);
    Expect(junction, Kind::MountPoint, folder, false);

    // Windows absolute symlink with Hebrew + a surrogate pair.
    const std::u16string emoji = u"D:\\Documents\\קובץ \U0001F4C1.txt";
    Expect(Point(tcs::reparse::kSymlinkTag, u"\\??\\" + emoji, emoji),
           Kind::SymbolicLink, emoji, false);

    // A relative symlink with mixed direction text keeps the exact target.
    const std::u16string relative = u"..\\נתונים\\Report 42 \u05D0.txt";
    Expect(Point(tcs::reparse::kSymlinkTag, relative, relative, true),
           Kind::SymbolicLink, relative, true);

    // If display name is missing, normalize the internal NT prefixes.
    Expect(Point(tcs::reparse::kMountPointTag, u"\\??\\" + folder, u""),
           Kind::MountPoint, folder, false);
    Expect(Point(tcs::reparse::kSymlinkTag, u"\\??\\UNC\\server\\משותף", u""),
           Kind::SymbolicLink, u"\\\\server\\משותף", false);
    Expect(Point(tcs::reparse::kSymlinkTag, u"\\\\?\\UNC\\server\\תיקייה", u""),
           Kind::SymbolicLink, u"\\\\server\\תיקייה", false);

    // Malformed / truncated kernel output must not produce a target string.
    auto shortHeader = junction;
    shortHeader.resize(7);
    Reject(shortHeader);
    auto truncated = junction;
    truncated.pop_back();
    Reject(truncated);
    auto invalidLength = junction;
    Put16(invalidLength, 4, 0xFFFF);
    Reject(invalidLength);
    auto shortPayload = junction;
    Put16(shortPayload, 4, 2);
    Reject(shortPayload);
    auto badOffset = junction;
    Put16(badOffset, 12, 0xFFFE);
    Reject(badOffset);
    auto badSubstituteOffset = junction;
    Put16(badSubstituteOffset, 8, 0xFFFE);
    Reject(badSubstituteOffset);
    auto oddLength = junction;
    Put16(oddLength, 14, 1);
    Reject(oddLength);
    auto oddOffset = junction;
    Put16(oddOffset, 8, 1);
    Reject(oddOffset);
    auto orphanSurrogate = Point(tcs::reparse::kSymlinkTag,
        u"C:\\", std::u16string{char16_t(0xD800)});
    Reject(orphanSurrogate);
    auto embeddedNull = Point(tcs::reparse::kSymlinkTag,
        u"C:\\", std::u16string{u'a', char16_t(0), u'b'});
    Reject(embeddedNull);
    auto shortSymlink = Point(tcs::reparse::kSymlinkTag, u"x", u"x");
    Put16(shortSymlink, 4, 8); // need at least 12 bytes for symbolic links
    Reject(shortSymlink);

    // Unknown vendor tag with a valid length must be safely ignored.
    auto unknown = junction;
    Put32(unknown, 0, 0xA000001F);
    Expect(unknown, Kind::Other, u"", false);

    // Deterministic mutations stress NTFS offsets and header bounds under
    // AddressSanitizer/UBSan without relying on an external fuzzing runtime.
    std::uint32_t state = 0x5A1E2026;
    for (int i = 0; i < 20000; ++i) {
        auto bytes = (i & 1) ? junction :
            Point(tcs::reparse::kSymlinkTag, u"\\??\\C:\\test", u"C:\\test", false);
        for (int n = 0; n < 3; ++n) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const std::size_t offset = (state >> 8) % bytes.size();
            bytes[offset] = Byte(state & 255U);
        }
        if ((i % 7) == 0) bytes.resize(static_cast<std::size_t>(i % (bytes.size() + 1)));
        tcs::reparse::Details result;
        (void)tcs::reparse::Parse(bytes, result);
    }
    return 0;
}
