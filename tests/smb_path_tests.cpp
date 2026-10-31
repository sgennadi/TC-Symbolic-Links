#include "SmbPath.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
}
int main() {
    try {
        using tcs::smb::ShareRoot;
        using tcs::smb::SameShare;
        Require(ShareRoot(L"\\\\SERVER\\Share\\File.txt") == L"\\\\SERVER\\Share",
                "Ordinary UNC parsing");
        Require(ShareRoot(L"\\\\?\\UNC\\SERVER\\Share\\sub") == L"\\\\SERVER\\Share",
                "Extended UNC parsing");
        Require(ShareRoot(L"\\??\\UNC\\SERVER\\Share\\sub") == L"\\\\SERVER\\Share",
                "NT object UNC parsing");
        Require(ShareRoot(L"\\\\SERVER\\Share") == L"\\\\SERVER\\Share",
                "UNC share root without slash");
        Require(ShareRoot(L"\\\\SERVER").empty(), "Bare server is not share");
        Require(ShareRoot(L"\\\\SERVER\\").empty(), "Empty share invalid");
        Require(ShareRoot(L"\\\\?\\C:\\Temp").empty(), "Device path not SMB UNC");
        Require(ShareRoot(L"C:\\Temp").empty(), "Local path not SMB UNC");
        Require(SameShare(L"\\\\HOST\\Data\\a", L"\\\\host\\data\\Links\\a"),
                "Same share case-insensitive comparison");
        Require(SameShare(L"\\\\?\\UNC\\HOST\\Data\\a", L"\\??\\UNC\\host\\DATA\\b"),
                "Extended and NT UNC share equivalence");
        Require(!SameShare(L"\\\\HOST\\Data\\a", L"\\\\HOST\\Data2\\a"),
                "Different SMB shares are different volumes");
        Require(!SameShare(L"\\\\HOST\\Data\\a", L"\\\\OTHER\\Data\\a"),
                "Different SMB servers not same share");
        Require(!SameShare(L"C:\\a", L"\\\\HOST\\Data\\a"),
                "Local and remote paths different");
        std::cout << "PASS: UNC share boundaries\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
