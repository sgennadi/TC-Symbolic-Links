#pragma once
#include "LinkCore.h"
#include "LinkChainWalk.h"
#include <windows.h>
#include <cstddef>
#include <string>
#include <vector>

namespace tcs::manager {
enum class Health { Valid, Broken, AccessDenied, Unknown };
struct Entry {
    std::wstring path;
    std::wstring target;
    ReparseKind kind = ReparseKind::None;
    Health health = Health::Unknown;
    DWORD error = ERROR_SUCCESS;
    DWORD hardlinks = 0;
    bool relative = false;
};
struct Scan {
    std::vector<Entry> items;
    std::size_t visited = 0;
    bool truncated = false;
    DWORD firstError = ERROR_SUCCESS;
};
struct Identity {
    DWORD volume=0, indexHigh=0, indexLow=0;
    ReparseKind kind=ReparseKind::None;
    bool directory=false, hardlink=false;
    std::wstring target, path;
    bool relative=false;
};
struct UndoSummary {
    std::size_t deleted=0, skipped=0;
    DWORD firstError=ERROR_SUCCESS;
};
struct RepairResult {
    DWORD code=ERROR_SUCCESS;
    std::wstring explanation, retainedBackup;
};
Entry InspectOne(const std::wstring& path);
// Read-only symbolic-link chain inspection (bounded to 16 hops maximum).
chain::Result InspectChain(const std::wstring& path, unsigned maxHops=12);
Scan FindLinks(const std::wstring& root, unsigned maxDepth=6,
               std::size_t maxVisited=5000, std::size_t maxLinks=2000);
bool Capture(const std::wstring& path, Identity& out);
UndoSummary Undo(const std::vector<Identity>& batch);
RepairResult RepairBroken(const std::wstring& path, const std::wstring& newTarget);
std::wstring ResolvedTarget(const Entry& entry);
const wchar_t* StatusLabel(Health value);
const wchar_t* KindLabel(const Entry& value);
} // namespace tcs::manager
