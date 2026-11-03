#include "LinkManagerCore.h"
#include "LinkCore.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

namespace fs=std::filesystem;
namespace {
void Check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
struct Temp {
    fs::path root;
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
Temp Directory() {
    wchar_t folder[MAX_PATH+1]{},tempName[MAX_PATH+1]{};
    const DWORD n=GetTempPathW(MAX_PATH+1,folder);
    Check(n>0&&n<=MAX_PATH,"temporary path unavailable");
    Check(GetTempFileNameW(folder,L"TCL",0,tempName)!=FALSE,"cannot allocate temporary path");
    Check(DeleteFileW(tempName)!=FALSE,"cannot delete initial placeholder");
    fs::create_directories(tempName);
    return Temp{tempName};
}
void Write(const fs::path& path) {
    std::ofstream file(path,std::ios::binary);
    Check(static_cast<bool>(file),"cannot write test file");
    file<<"TC link manager 0.2.20";
}
}
int main(){
    try {
        auto temp=Directory();
        const auto source=temp.root/L"source.txt";
        const auto hardlink=temp.root/L"second.txt";
        Write(source);
        const auto created=tcs::CreateLink(source.wstring(),hardlink.wstring(),
            tcs::LinkKind::Hardlink,false);
        Check(created==ERROR_SUCCESS,"hardlink creation failed");
        tcs::manager::Identity id;
        Check(tcs::manager::Capture(hardlink.wstring(),id),
              "failed to capture hardlink identity for undo");
        const auto links=tcs::manager::FindLinks(temp.root.wstring(),2,300,50);
        Check(links.visited>=2,"scan did not visit expected files");
        bool foundHardlink=false;
        for(const auto& item:links.items)
            if(_wcsicmp(item.path.c_str(),hardlink.c_str())==0 &&
               item.hardlinks>1 && item.health==tcs::manager::Health::Valid)
                foundHardlink=true;
        Check(foundHardlink,"finder failed to report hardlink");
        const auto undo=tcs::manager::Undo({id});
        Check(undo.deleted==1&&undo.skipped==0,"hardlink undo failed");
        Check(fs::exists(source)&&!fs::exists(hardlink),
              "undo changed original file or left created hardlink");
        Check(tcs::manager::Undo({id}).deleted==0,
              "undo must never delete another entry after first removal");
        Write(temp.root/L"another.txt");
        const auto limited=tcs::manager::FindLinks(temp.root.wstring(),1,1,50);
        Check(limited.truncated,"finder must stop at explicit item limit");
        const auto alias=temp.root/L"file-symlink.txt";
        const auto sym=tcs::CreateLink(source.wstring(),alias.wstring(),
            tcs::LinkKind::FileSymbolic,false);
        if(sym==ERROR_SUCCESS){
            tcs::manager::Identity symlinkId;
            Check(tcs::manager::Capture(alias.wstring(),symlinkId),
                  "cannot capture symlink identity");
            auto inspected=tcs::manager::InspectOne(alias.wstring());
            Check(inspected.kind==tcs::ReparseKind::SymbolicFile,
                  "symlink type not reported");
            Check(inspected.health==tcs::manager::Health::Valid,
                  "working symlink must have Valid health");
            Check(tcs::manager::Undo({symlinkId}).deleted==1,
                  "link undo must remove only the link name");
            Check(fs::exists(source)&&!fs::exists(alias),
                  "undo followed symlink into source");
            const DWORD brokenMade=tcs::CreateLink(source.wstring(),alias.wstring(),
                tcs::LinkKind::FileSymbolic,false);
            Check(brokenMade==ERROR_SUCCESS,"cannot recreate symlink for repair");
            Check(DeleteFileW(source.c_str())!=FALSE,"cannot break test symlink");
            inspected=tcs::manager::InspectOne(alias.wstring());
            Check(inspected.kind==tcs::ReparseKind::SymbolicFile,
                  "broken symlink lost reparse kind");
            Check(inspected.health==tcs::manager::Health::Broken,
                  "broken link must not be called valid");
            const auto replacement=temp.root/L"replacement.txt";
            Write(replacement);
            const auto repaired=tcs::manager::RepairBroken(alias.wstring(),
                                                           replacement.wstring());
            Check(repaired.code==ERROR_SUCCESS,"broken symlink repair failed");
            inspected=tcs::manager::InspectOne(alias.wstring());
            Check(inspected.kind==tcs::ReparseKind::SymbolicFile &&
                  inspected.health==tcs::manager::Health::Valid,
                  "repaired link did not become valid");
            Check(fs::exists(replacement),"repair must never modify source");
        } else if(sym!=ERROR_PRIVILEGE_NOT_HELD &&
                 sym!=ERROR_ACCESS_DENIED&&sym!=ERROR_NOT_SUPPORTED){
            Check(false,"unexpected native symlink creation failure");
        } else std::cout<<"SKIP: native symlink privilege unavailable\n";
        std::cout<<"PASS: bounded finder, health, identity-protected undo, repair\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
