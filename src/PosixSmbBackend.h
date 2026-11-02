#pragma once
#include <windows.h>
#include <string>

namespace tcs::posix {
// Explicit per-SMB-share opt-in. Never changes Samba configuration or ACLs.
bool EnabledFor(const std::wstring& destinationUnc);
// Calls the administrator-authorized forced-command SSH helper (not cmd.exe).
// Source and destination MUST be on the same share. POSIX helper performs
// independent dirfd / O_NOFOLLOW checks and post-creation readlink verification.
DWORD CreateDirectoryLink(const std::wstring& sourceUnc, const std::wstring& linkUnc);
void ResetResult();
bool LastSuccess();
std::wstring LastDiagnostic();
}
