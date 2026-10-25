// Tests load the same WDX source compiled for Linux Double Commander using
// the Double Commander ABI (16-bit WCHAR even though Linux wchar_t is 32-bit).
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <codecvt>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace {
constexpr int ft_string = 8, ft_stringw = 11, ft_numeric_32 = 1;
constexpr int ft_fileerror = -2, ft_fieldempty = -3;
using GetField = int (*)(int, char*, char*, int);
using GetValueW = int (*)(char16_t*, int, int, void*, int, int);
using GetValueA = int (*)(char*, int, int, void*, int, int);

void Require(bool test, const char* message) {
    if (!test) throw std::runtime_error(message);
}
std::string ToUtf8(std::u16string_view input) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> cv;
    return cv.to_bytes(input.data(), input.data() + input.size());
}
std::u16string ToWide(const std::string& ascii) {
    return std::u16string(ascii.begin(), ascii.end());
}
struct Module {
    void* instance;
    explicit Module(const char* path) : instance(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!instance) throw std::runtime_error(dlerror());
    }
    ~Module() { if (instance) dlclose(instance); }
    template <typename T> T Load(const char* name) const {
        auto* proc = dlsym(instance, name);
        Require(proc != nullptr, "WDX export missing");
        return reinterpret_cast<T>(proc);
    }
};
struct TempDir {
    fs::path path;
    TempDir() {
        char pattern[] = "/tmp/tc-wdx-unicode-XXXXXX";
        const char* root = mkdtemp(pattern);
        Require(root != nullptr, "mkdtemp failed");
        path = root;
    }
    ~TempDir() {
        std::error_code ignore;
        fs::remove_all(path, ignore);
    }
};

std::u16string WideValue(GetValueW get, std::u16string path, int index, int expect = ft_stringw) {
    std::array<char16_t, 8192> buffer{};
    const int type = get(path.data(), index, 0, buffer.data(), static_cast<int>(sizeof(buffer)), 0);
    Require(type == expect, "WDX Unicode field type mismatch");
    if (expect != ft_stringw) return {};
    return buffer.data();
}
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2, "Pass native Linux .wdx path");
        Module module(argv[1]);
        const auto getField = module.Load<GetField>("ContentGetSupportedField");
        const auto getValue = module.Load<GetValueW>("ContentGetValueW");
        const auto getAnsi = module.Load<GetValueA>("ContentGetValue");
        char name[128]{}, units[128]{};
        Require(getField(0, name, units, sizeof(name)) == ft_string, "WDX type field declaration");
        Require(std::string(name) == "Link type", "WDX type field name");
        Require(getField(1, name, units, sizeof(name)) == ft_string, "WDX target field declaration");
        Require(getField(3, name, units, sizeof(name)) == ft_numeric_32, "WDX nlink field declaration");
        Require(getField(4, name, units, sizeof(name)) == 0, "WDX out-of-range field declaration");

        TempDir temp;
        const std::u16string base = ToWide(temp.path.string());
        std::u16string dir = base + u"/\u05DE\u05E1\u05DE\u05DB\u05D9\u05DD \u05D1\u05E2\u05D1\u05E8\u05D9\u05EA";
        const std::u16string targetDir = base + u"/\u05D9\u05E2\u05D3 2026";
        const std::u16string file = dir + u"/\u05E9\u05B8\u05DC\u05D5\u05B9\u05DD \U0001F4C1.txt";
        std::u16string hardlink = targetDir + u"/\u05E7\u05D5\u05D1\u05E5 \u05E7\u05E9\u05D9\u05D7.txt";
        const std::u16string directoryLink = targetDir + u"/\u05E7\u05D9\u05E9\u05D5\u05E8 \u05EA\u05D9\u05E7\u05D9\u05D9\u05D4";
        std::u16string fileLink = targetDir + u"/\u05E7\u05D9\u05E9\u05D5\u05E8 \u05E7\u05D5\u05D1\u05E5.txt";
        const std::u16string brokenLink = targetDir + u"/\u05E7\u05D9\u05E9\u05D5\u05E8 \u05E9\u05D1\u05D5\u05E8";
        std::u16string relativeLink = targetDir + u"/relative \u05E7\u05D9\u05E9\u05D5\u05E8.txt";
        const std::u16string relativeTarget = u"../\u05DE\u05E1\u05DE\u05DB\u05D9\u05DD \u05D1\u05E2\u05D1\u05E8\u05D9\u05EA/\u05E9\u05B8\u05DC\u05D5\u05B9\u05DD \U0001F4C1.txt";
        const auto Native = [](const std::u16string& path) { return ToUtf8(path); };
        fs::create_directories(Native(dir));
        fs::create_directories(Native(targetDir));
        {
            FILE* f = std::fopen(Native(file).c_str(), "wb");
            Require(f != nullptr, "Create Hebrew source file");
            std::fputs("test", f);
            std::fclose(f);
        }
        fs::create_hard_link(Native(file), Native(hardlink));
        fs::create_directory_symlink(Native(dir), Native(directoryLink));
        fs::create_symlink(Native(file), Native(fileLink));
        fs::create_symlink(Native(base + u"/nonexistent"), Native(brokenLink));
        fs::create_symlink(Native(relativeTarget), Native(relativeLink));

        Require(WideValue(getValue, directoryLink, 0) == u"Directory symlink", "Hebrew directory symlink type");
        Require(WideValue(getValue, directoryLink, 1) == dir, "Hebrew directory target exact UTF-16");
        Require(WideValue(getValue, directoryLink, 2) == u"Yes", "Hebrew directory exists");
        Require(WideValue(getValue, fileLink, 0) == u"File symlink", "Hebrew file symlink type");
        Require(WideValue(getValue, fileLink, 1) == file, "Hebrew + niqqud + non-BMP target exact UTF-16");
        Require(WideValue(getValue, fileLink, 2) == u"Yes", "Unicode file target exists");
        Require(WideValue(getValue, brokenLink, 2) == u"No", "Broken link detection");
        Require(WideValue(getValue, relativeLink, 1) == relativeTarget,
                "Relative Hebrew + niqqud + emoji target must be unchanged");
        Require(WideValue(getValue, relativeLink, 2) == u"Yes",
                "Relative Hebrew target must resolve via its parent directory");
        Require(WideValue(getValue, hardlink, 0) == u"Hardlinked file", "Hardlink type");
        int count = 0;
        Require(getValue(hardlink.data(), 3, 0, &count, sizeof(count), 0) == ft_numeric_32 && count >= 2,
                "Hardlink count must be numeric >= 2");
        Require(getValue(dir.data(), 0, 0, &count, sizeof(count), 0) == ft_fieldempty,
                "Normal directory must not be link");

        // ANSI/UTF-8 fallback is present for managers that do not call WDX Unicode entrypoint.
        std::string path8 = Native(fileLink);
        char output8[8192]{};
        Require(getAnsi(path8.data(), 1, 0, output8, sizeof(output8), 0) == ft_string,
                "WDX ANSI/UTF-8 entrypoint");
        Require(std::string(output8) == Native(file), "WDX ANSI/UTF-8 target must preserve Hebrew");

        // WDX maxlen is bytes, not number of WCHARs; don't write past buffers.
        std::array<char16_t, 4> shortBuffer{0x7777, 0x7777, 0x7777, 0x7777};
        Require(getValue(fileLink.data(), 1, 0, shortBuffer.data(), 4, 0) == ft_stringw,
                "Short Unicode buffer field code");
        Require(shortBuffer[1] == 0 && shortBuffer[2] == 0x7777, "Buffer bounds and terminator");
        Require(getValue(fileLink.data(), 1, 0, shortBuffer.data(), 1, 0) == ft_fileerror,
                "Reject single-byte Unicode output buffer");

        // Deliberately truncate immediately between an emoji's UTF-16 surrogate pair.
        const std::size_t emoji = file.find(static_cast<char16_t>(0xD83D));
        Require(emoji != std::u16string::npos, "Test path must contain emoji");
        std::u16string truncated(emoji + 2, u'X');
        Require(getValue(fileLink.data(), 1, 0, truncated.data(),
                         static_cast<int>(truncated.size() * sizeof(char16_t)), 0) == ft_stringw,
                "Surrogate boundary truncation");
        Require(truncated[emoji] == 0, "Never emit half of an emoji surrogate pair");
        std::u16string invalid = fileLink + static_cast<char16_t>(0xD800);
        Require(getValue(invalid.data(), 0, 0, shortBuffer.data(), sizeof(shortBuffer), 0) == ft_fileerror,
                "Reject malformed UTF-16 names");
        std::cout << "PASS: ELF WDX dlopen, exports, Hebrew/niqqud/emoji, real POSIX links, hardlinks, broken links, bounds\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
