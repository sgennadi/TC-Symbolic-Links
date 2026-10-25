#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace tcs {
// %WL in Total Commander is a UTF-16LE (BOM) list of absolute paths.
// Reject other encodings so a path can never be silently corrupted.
std::vector<std::u16string> ParseTcWideList(std::span<const std::byte> bytes);
}
