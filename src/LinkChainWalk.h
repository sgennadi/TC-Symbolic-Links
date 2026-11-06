#pragma once
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Read-only, bounded symbolic-link chain traversal. The callbacks keep Win32
// and network I/O outside the portable policy engine (also tested on Linux).
namespace tcs::chain {
enum class Kind { Regular, FileSymlink, DirectorySymlink, Junction, Unsupported };
enum class Status { Resolved, NotALink, Broken, AccessDenied, Loop, HopLimit, Unsupported, Error };
struct Observation {
    Kind kind = Kind::Regular;
    std::wstring target;
    bool relative = false;
    unsigned long error = 0;
};
struct Hop {
    std::wstring path;
    std::wstring target;
    std::wstring resolvedTarget;
    Kind kind = Kind::Regular;
    bool relative = false;
};
struct Result {
    std::vector<Hop> hops;
    std::wstring terminalPath;
    Status status = Status::Error;
    unsigned long error = 0;
};
using Observer = std::function<Observation(const std::wstring&)>;
using Resolver = std::function<std::wstring(const std::wstring&, const Observation&)>;
inline std::wstring Key(std::wstring_view path) {
    std::wstring key(path);
    for (auto& ch : key) {
        if (ch == L'/') ch = L'\\';
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    while (key.size() > 3 && key.back() == L'\\') key.pop_back();
    return key;
}
inline Result Walk(std::wstring start, const Observer& observe,
                   const Resolver& resolve, unsigned maxHops = 12) {
    Result result;
    if (start.empty() || !observe || !resolve) {
        result.terminalPath = std::move(start);
        return result;
    }
    maxHops = (std::max)(1u, (std::min)(maxHops, 16u));
    std::set<std::wstring> visited;
    std::wstring current = std::move(start);
    for (;;) {
        if (!visited.insert(Key(current)).second) {
            result.status = Status::Loop;
            result.terminalPath = std::move(current);
            return result;
        }
        const auto node = observe(current);
        if (node.error != 0) {
            result.error = node.error;
            result.status = node.error == 2 || node.error == 3 ?
                Status::Broken : node.error == 5 ? Status::AccessDenied : Status::Error;
            result.terminalPath = std::move(current);
            return result;
        }
        if (node.kind == Kind::Regular) {
            result.status = result.hops.empty() ? Status::NotALink : Status::Resolved;
            result.terminalPath = std::move(current);
            return result;
        }
        if (node.kind == Kind::Unsupported) {
            result.status = Status::Unsupported;
            result.terminalPath = std::move(current);
            return result;
        }
        if (result.hops.size() >= maxHops) {
            result.status = Status::HopLimit;
            result.terminalPath = std::move(current);
            return result;
        }
        const std::wstring next = node.target.empty() ? L"" : resolve(current, node);
        if (next.empty()) {
            result.status = Status::Error;
            result.terminalPath = std::move(current);
            return result;
        }
        result.hops.push_back({current, node.target, next, node.kind, node.relative});
        current = next;
    }
}
inline const wchar_t* StatusName(Status status) {
    switch (status) {
        case Status::Resolved: return L"Resolved";
        case Status::NotALink: return L"Not a symbolic link";
        case Status::Broken: return L"Target missing";
        case Status::AccessDenied: return L"Access denied";
        case Status::Loop: return L"Loop detected";
        case Status::HopLimit: return L"Hop limit reached";
        case Status::Unsupported: return L"Unsupported reparse point";
        case Status::Error: return L"Cannot inspect target";
    }
    return L"Unknown";
}
} // namespace tcs::chain
