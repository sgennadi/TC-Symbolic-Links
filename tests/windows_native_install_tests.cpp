#include "NativeUpdateInstall.h"
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
namespace fs=std::filesystem;
namespace {
void Check(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
void Write(const fs::path& path,const std::string& bytes) {
    std::ofstream f(path,std::ios::binary);
    Check(static_cast<bool>(f),"Cannot write native update test fixture");
    f<<bytes;
    Check(static_cast<bool>(f),"Failed writing native update test data");
}
std::string Read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);
    Check(static_cast<bool>(f),"Cannot read native update test fixture");
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}
struct Scope {
    fs::path root;
    ~Scope(){if(!root.empty()){std::error_code ec;fs::remove_all(root,ec);}}
};
Scope TempDir() {
    wchar_t temp[MAX_PATH+1]{},tmpFile[MAX_PATH+1]{};
    DWORD len=GetTempPathW(MAX_PATH+1,temp);
    Check(len>0&&len<=MAX_PATH,"GetTempPathW failed");
    Check(GetTempFileNameW(temp,L"NUP",0,tmpFile)!=0,"GetTempFileNameW failed");
    Check(DeleteFileW(tmpFile)!=FALSE,"Cannot delete temp placeholder");
    fs::create_directories(tmpFile);
    return Scope{tmpFile};
}
constexpr std::array<const wchar_t*,3> names{
    L"SymlinkInspector.wdx",L"SymlinkInspector.wdx64",L"pluginst.inf"
};
void Setup(const fs::path& install,const fs::path& staged,
           bool includeInstalledX64=true,bool includeStagedX64=true) {
    fs::create_directories(staged);
    for(int i=0;i<3;i++) {
        if(i!=1||includeInstalledX64)
            Write(install/names[i],"OLD"+std::to_string(i));
        if(i!=1||includeStagedX64)
            Write(staged/names[i],"NEW"+std::to_string(i));
    }
    Write(install/L"usercmd.ini","KEEP USER COMMANDS EXACT");
    Write(install/L"PERSONAL.BAR","KEEP ALL USER BUTTONS EXACT");
}
void AssertUserFiles(const fs::path& root) {
    Check(Read(root/L"usercmd.ini")=="KEEP USER COMMANDS EXACT",
          "Native install modified personal usercmd.ini");
    Check(Read(root/L"PERSONAL.BAR")=="KEEP ALL USER BUTTONS EXACT",
          "Native install modified existing custom buttons");
}
void AssertBackups(const fs::path& root) {
    int backups=0;
    for(const auto& file:fs::directory_iterator(root)) {
        if(file.path().extension()==L".bak") {
            const auto name=file.path().filename().wstring();
            const auto index=(name.find(L"SymlinkInspector.wdx64")==0)?1:
                ((name.find(L"SymlinkInspector.wdx")==0)?0:2);
            Check(Read(file.path())=="OLD"+std::to_string(index),
                  "Backup changed installed bytes");
            backups++;
        }
    }
    Check(backups==3,"Missing architecture or INI backup");
}
}
int main() {
    try {
        auto scope=TempDir();
        // Successful two-architecture update with persistent original backups.
        const auto install=scope.root/L"Successful";
        const auto stage=install/L".tcs-native-test";
        fs::create_directories(install);
        Setup(install,stage);
        std::wstring reason;
        Check(tcs::update::ApplyNativeFilesForTest(stage.wstring(),install.wstring(),reason),
              "Native updater did not replace both DLL architectures");
        for(int i=0;i<3;i++) {
            Check(Read(install/names[i])=="NEW"+std::to_string(i),
                  "Installed DLL or INF does not match new payload");
        }
        AssertBackups(install);
        AssertUserFiles(install);
        // Replacing the second architecture can fail due to a sharing lock.
        // The already updated first architecture must then roll back.
        const auto rollback=scope.root/L"SharingRollback";
        const auto rollbackStage=rollback/L".tcs-native-test";
        fs::create_directories(rollback);
        Setup(rollback,rollbackStage);
        HANDLE lock=CreateFileW((rollback/names[1]).c_str(),GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(lock!=INVALID_HANDLE_VALUE,"Cannot lock x64 destination");
        reason.clear();
        const bool applied=tcs::update::ApplyNativeFilesForTest(
            rollbackStage.wstring(),rollback.wstring(),reason);
        CloseHandle(lock);
        Check(!applied,"Native installer succeeded despite locked target DLL");
        for(int i=0;i<3;i++)
            Check(Read(rollback/names[i])=="OLD"+std::to_string(i),
                  "Rollback did not restore every original file");
        AssertUserFiles(rollback);
        // Missing staged x64 DLL must not result in a partial installed update.
        const auto missing=scope.root/L"MissingPayload";
        const auto missingStage=missing/L".tcs-native-test";
        fs::create_directories(missing);
        Setup(missing,missingStage,true,false);
        reason.clear();
        Check(!tcs::update::ApplyNativeFilesForTest(
            missingStage.wstring(),missing.wstring(),reason),
            "Missing staged architecture was silently accepted");
        for(int i=0;i<3;i++)
            Check(Read(missing/names[i])=="OLD"+std::to_string(i),
                  "Missing package architecture produced a partial installation");
        AssertUserFiles(missing);
        // x86-only installations must not gain an unrequested x64 registration.
        const auto selective=scope.root/L"Selective";
        const auto selectiveStage=selective/L".tcs-native-test";
        fs::create_directories(selective);
        Setup(selective,selectiveStage,false,true);
        reason.clear();
        Check(tcs::update::ApplyNativeFilesForTest(
            selectiveStage.wstring(),selective.wstring(),reason),
            "x86-only compatible update failed");
        Check(!fs::exists(selective/names[1]),"Added unwanted x64 DLL");
        AssertUserFiles(selective);
        std::cout<<"PASS: native WDX update, backups, sharing rollback and user settings\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr<<"FAIL: "<<ex.what()<<"\n";
        return 1;
    }
}
