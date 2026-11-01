// Genuine network regression against an existing official immutable release.
// Exercises WinHTTP redirects, GitHub REST asset digest, ZIP download,
// static zlib extraction validation, x86/x64 PE checks and staging.
// It never installs a DLL or changes a Total Commander configuration.
#include "NativeUpdateInstall.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <system_error>
namespace fs=std::filesystem;
int main() {
    tcs::update::NativeStage result{};
    const bool ok=tcs::update::StageNativeUpdate(L"v0.2.16",nullptr,result);
    if(!ok) {
        std::wcerr<<L"FAIL: native GitHub download: "<<result.description<<L"\n";
        return 1;
    }
    const auto zip=fs::path(result.folder)/L"TC-Symbolic-Links-win32-win64.zip";
    const bool safe=fs::exists(zip) && fs::is_regular_file(zip) &&
        fs::file_size(zip)>10000 && result.tag==L"v0.2.16";
    std::error_code ignored;
    if(!result.folder.empty()) fs::remove_all(result.folder,ignored);
    if(!safe) {
        std::cerr<<"FAIL: native verified stage is missing or wrong version\n";
        return 1;
    }
    std::cout<<"PASS: official GitHub ZIP downloaded, SHA-256 and PE verified\n";
    return 0;
}
