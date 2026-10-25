// Load the compiled .wdx64 with the Windows loader and exercise the actual
// Total Commander WDX Unicode exports without requiring a TC installation.
#include "LinkCore.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace {
constexpr int ft_string = 8;
constexpr int ft_stringw = 11;
constexpr int ft_numeric_32 = 1;

using GetSupportedField = int (__stdcall *)(int, char*, char*, int);
using GetValueW = int (__stdcall *)(wchar_t*, int, int, void*, int, int);

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDir {
    fs::path root;
    ~TempDir() {
        std::error_code ignored;
        if (!root.empty()) fs::remove_all(root, ignored);
    }
};

struct Module {
    HMODULE handle = nullptr;
    explicit Module(const wchar_t* path) : handle(LoadLibraryW(path)) {
        Require(handle != nullptr, "LoadLibraryW(.wdx64) failed");
    }
    ~Module() { if (handle) FreeLibrary(handle); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    template <typename T> T Export(const char* name) const {
        auto address = GetProcAddress(handle, name);
        Require(address != nullptr, "WDX export is missing");
        return reinterpret_cast<T>(address);
    }
};

TempDir NewTempDirectory() {
    wchar_t temp[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
    Require(length != 0 && length <= MAX_PATH, "GetTempPathW failed");
    wchar_t placeholder[MAX_PATH + 1]{};
    Require(GetTempFileNameW(temp, L"WDX", 0, placeholder) != 0, "GetTempFileNameW failed");
    Require(DeleteFileW(placeholder), "DeleteFileW placeholder failed");
    const fs::path root(placeholder);
    fs::create_directories(root);
    return TempDir{root};
}

void WriteFile(const fs::path& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(h != INVALID_HANDLE_VALUE, "CreateFileW Hebrew file failed");
    CloseHandle(h);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Pass full path to SymlinkInspector.wdx64");
        Module module(argv[1]);
        const auto getField = module.Export<GetSupportedField>("ContentGetSupportedField");
        const auto getValue = module.Export<GetValueW>("ContentGetValueW");
        (void)module.Export<FARPROC>("ContentGetValue");

        char name[128]{};
        char units[128]{};
        Require(getField(0, name, units, sizeof(name)) == ft_string,
                "Link type WDX field ABI mismatch");
        Require(std::string(name) == "Link type", "Link type field name mismatch");
        Require(getField(3, name, units, sizeof(name)) == ft_numeric_32,
                "Hardlink count WDX field ABI mismatch");

        TempDir dir = NewTempDirectory();
        const fs::path sourceDir = dir.root / L"מסמכים בעברית";
        const fs::path destDir = dir.root / L"יעד 2026";
        fs::create_directories(sourceDir);
        fs::create_directories(destDir);
        const fs::path original = sourceDir / L"ש\u05B8לו\u05B9ם.txt";
        const fs::path hardlink = destDir / L"קובץ קשיח.txt";
        const fs::path junction = destDir / L"קישור תיקייה";
        WriteFile(original);
        Require(tcs::CreateLink(original.wstring(), hardlink.wstring(),
                tcs::LinkKind::Hardlink, false) == ERROR_SUCCESS,
                "Hebrew hardlink creation failed");
        Require(tcs::CreateLink(sourceDir.wstring(), junction.wstring(),
                tcs::LinkKind::Junction, false) == ERROR_SUCCESS,
                "Hebrew junction creation failed");

        wchar_t output[4096]{};
        std::wstring path = junction.wstring();
        Require(getValue(path.data(), 0, 0, output, sizeof(output), 0) == ft_stringw,
                "WDX did not return UTF-16 type value");
        Require(std::wstring(output) == L"Junction / mount point",
                "WDX did not recognize the Hebrew-named junction");

        Require(getValue(path.data(), 1, 0, output, sizeof(output), 0) == ft_stringw,
                "WDX did not return UTF-16 target value");
        Require(std::wstring(output) == sourceDir.wstring(),
                "WDX returned a damaged or incorrect Hebrew target path");
        Require(getValue(path.data(), 2, 0, output, sizeof(output), 0) == ft_stringw,
                "WDX target-exists field missing");
        Require(std::wstring(output) == L"Yes", "WDX target-exists should be Yes");

        path = hardlink.wstring();
        int hardlinkCount = 0;
        Require(getValue(path.data(), 3, 0, &hardlinkCount, sizeof(hardlinkCount), 0) == ft_numeric_32,
                "WDX hardlink-count type mismatch");
        Require(hardlinkCount >= 2, "WDX hardlink count is incorrect");
        std::cout << "PASS: WDX native exports, Hebrew junction target, hardlink count\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
