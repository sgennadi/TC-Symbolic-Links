#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace tcs::reparse {

constexpr std::uint32_t kSymlinkTag = 0xA000000C;
constexpr std::uint32_t kMountPointTag = 0xA0000003;

enum class Kind { SymbolicLink, MountPoint, Other };

struct Details {
    Kind kind = Kind::Other;
    std::u16string target;
    bool relative = false;
};

// Parses the raw FSCTL_GET_REPARSE_POINT data without Windows headers. This
// same code is tested on Linux and used by the Windows WDX inspector.
// Returns false on malformed or truncated reparse data. An unknown but valid
// tag is reported as Other (its private payload is intentionally not parsed).
bool Parse(std::span<const std::byte> raw, Details& details);

} // namespace tcs::reparse
