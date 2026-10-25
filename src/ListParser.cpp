#include "ListParser.h"
#include <stdexcept>

namespace tcs {
std::vector<std::u16string> ParseTcWideList(std::span<const std::byte> bytes) {
    if (bytes.size() < 2 || bytes.size() % 2 != 0 ||
        std::to_integer<unsigned>(bytes[0]) != 0xFF ||
        std::to_integer<unsigned>(bytes[1]) != 0xFE) {
        throw std::invalid_argument("The TC %WL list must be UTF-16LE with a BOM");
    }

    std::vector<std::u16string> paths;
    std::u16string line;
    bool expectLowSurrogate = false;
    const auto finishLine = [&] {
        if (expectLowSurrogate) throw std::invalid_argument("Incomplete UTF-16 surrogate pair");
        if (!line.empty() && line.back() == u'\r') line.pop_back();
        if (!line.empty()) {
            if (line.size() >= 2 && line.front() == u'"' && line.back() == u'"')
                paths.emplace_back(line.substr(1, line.size() - 2));
            else
                paths.emplace_back(std::move(line));
            if (paths.size() > 10000) throw std::length_error("Too many selected entries");
        }
        line.clear();
    };

    for (std::size_t i = 2; i < bytes.size(); i += 2) {
        const auto c = static_cast<char16_t>(
            std::to_integer<unsigned>(bytes[i]) |
            (std::to_integer<unsigned>(bytes[i + 1]) << 8));
        if (c == 0) throw std::invalid_argument("Unexpected null character in list");
        const bool low = c >= 0xDC00 && c <= 0xDFFF;
        const bool high = c >= 0xD800 && c <= 0xDBFF;
        if (expectLowSurrogate != low)
            throw std::invalid_argument("Invalid UTF-16 surrogate sequence");
        expectLowSurrogate = high;
        if (c == u'\n') finishLine();
        else line.push_back(c);
    }
    finishLine();
    return paths;
}
}
