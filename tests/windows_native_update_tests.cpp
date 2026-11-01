#include "NativeUpdateInstall.h"
#include <zlib.h>
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes=std::vector<unsigned char>;
void Require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
void Word(Bytes& b,uint16_t n) {
    b.push_back(n&0xff);b.push_back(n>>8);
}
void Dword(Bytes& b,uint32_t n) {
    Word(b,static_cast<uint16_t>(n));Word(b,static_cast<uint16_t>(n>>16));
}
Bytes FakePe(uint16_t machine) {
    Bytes b(512,0);
    b[0]='M';b[1]='Z';b[0x3c]=0x80;
    b[0x80]='P';b[0x81]='E';
    b[0x84]=static_cast<unsigned char>(machine);
    b[0x85]=static_cast<unsigned char>(machine>>8);
    return b;
}
Bytes Deflate(const Bytes& source) {
    Bytes compressed(compressBound(static_cast<uLong>(source.size()))+16,0);
    z_stream stream{};
    stream.next_in=const_cast<Bytef*>(source.data());
    stream.avail_in=static_cast<uInt>(source.size());
    stream.next_out=compressed.data();
    stream.avail_out=static_cast<uInt>(compressed.size());
    Require(deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,
        -MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK,"raw deflate init failed");
    const int status=deflate(&stream,Z_FINISH);
    Require(status==Z_STREAM_END,"raw deflate did not finish");
    compressed.resize(stream.total_out);
    deflateEnd(&stream);
    return compressed;
}
struct Member {
    std::string name;
    Bytes data;
};
Bytes MakeZip(const std::array<Member,3>& files) {
    Bytes zip,central;
    for(const auto& f:files) {
        const auto compressed=Deflate(f.data);
        const auto crc=crc32(crc32(0L,Z_NULL,0),f.data.data(),
            static_cast<uInt>(f.data.size()));
        const uint32_t local=static_cast<uint32_t>(zip.size());
        Dword(zip,0x04034b50);Word(zip,20);Word(zip,0);Word(zip,8);
        Word(zip,0);Word(zip,0);Dword(zip,static_cast<uint32_t>(crc));
        Dword(zip,static_cast<uint32_t>(compressed.size()));
        Dword(zip,static_cast<uint32_t>(f.data.size()));
        Word(zip,static_cast<uint16_t>(f.name.size()));Word(zip,0);
        zip.insert(zip.end(),f.name.begin(),f.name.end());
        zip.insert(zip.end(),compressed.begin(),compressed.end());
        Dword(central,0x02014b50);Word(central,20);Word(central,20);
        Word(central,0);Word(central,8);Word(central,0);Word(central,0);
        Dword(central,static_cast<uint32_t>(crc));
        Dword(central,static_cast<uint32_t>(compressed.size()));
        Dword(central,static_cast<uint32_t>(f.data.size()));
        Word(central,static_cast<uint16_t>(f.name.size()));
        Word(central,0);Word(central,0);Word(central,0);
        Word(central,0);Dword(central,0);Dword(central,local);
        central.insert(central.end(),f.name.begin(),f.name.end());
    }
    const uint32_t offset=static_cast<uint32_t>(zip.size());
    zip.insert(zip.end(),central.begin(),central.end());
    Dword(zip,0x06054b50);Word(zip,0);Word(zip,0);
    Word(zip,3);Word(zip,3);Dword(zip,static_cast<uint32_t>(central.size()));
    Dword(zip,offset);Word(zip,0);
    return zip;
}
} // namespace

int main() {
    try {
        const std::array<Member,3> clean{{
            {"SymlinkInspector.wdx",FakePe(0x014c)},
            {"SymlinkInspector.wdx64",FakePe(0x8664)},
            {"pluginst.inf",Bytes{ '[','p','l','u','g','i','n','i','n','s','t','a','l','l',']',
                '\n','t','y','p','e','=','w','d','x','\n',
                'f','i','l','e','=','S','y','m','l','i','n','k','I','n','s','p','e','c','t','o','r',
                '.','w','d','x','\n'}}
        }};
        const auto valid=MakeZip(clean);
        Require(tcs::update::VerifyNativeZipBytes(valid),
                "Valid x86/x64/INF archive unexpectedly rejected");
        auto corrupt=valid;
        corrupt[30+clean[0].name.size()+4]^=0x2f;
        Require(!tcs::update::VerifyNativeZipBytes(corrupt),
                "Corrupted compressed member or CRC was accepted");
        auto duplicate=clean;
        duplicate[1].name="SymlinkInspector.wdx";
        Require(!tcs::update::VerifyNativeZipBytes(MakeZip(duplicate)),
                "Duplicate names were accepted");
        auto traversal=clean;
        traversal[0].name="../../SymlinkInspector.wdx";
        Require(!tcs::update::VerifyNativeZipBytes(MakeZip(traversal)),
                "Traversal ZIP member was accepted");
        auto badMachine=clean;
        badMachine[1].data=FakePe(0x014c);
        Require(!tcs::update::VerifyNativeZipBytes(MakeZip(badMachine)),
                "An x86 DLL was accepted as x64");
        auto forbidden=clean;
        forbidden[2].name="start.ps1";
        Require(!tcs::update::VerifyNativeZipBytes(MakeZip(forbidden)),
                "Unexpected executable/script payload was accepted");
        std::cout<<"PASS: native ZIP deflate, CRC, PE, path and duplicate checks\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr<<"FAIL: "<<ex.what()<<"\n";
        return 1;
    }
}
