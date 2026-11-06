#include "LinkManagerCore.h"
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <utility>
#include <vector>

namespace tcs::manager {
namespace {
std::wstring Api(std::wstring p) {
    if(p.starts_with(L"\\\\?\\") || p.size()<240)return p;
    if(p.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+p.substr(2);
    if(p.size()>=3&&p[1]==L':'&&p[2]==L'\\')return L"\\\\?\\"+p;
    return p;
}
bool ReadIdentity(const std::wstring& path,Identity& id){
    HANDLE h=CreateFileW(Api(path).c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok=GetFileInformationByHandle(h,&info);
    CloseHandle(h);
    if(!ok)return false;
    id.volume=info.dwVolumeSerialNumber;
    id.indexHigh=info.nFileIndexHigh;
    id.indexLow=info.nFileIndexLow;
    id.directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    id.path=path;
    return true;
}
bool SameIdentity(const Identity& a,const Identity& b){
    return a.volume==b.volume&&a.indexHigh==b.indexHigh&&
        a.indexLow==b.indexLow&&a.directory==b.directory&&
        a.kind==b.kind&&a.hardlink==b.hardlink&&a.target==b.target&&
        a.relative==b.relative;
}
bool DeleteOnlyLink(const Identity& value){
    return (value.directory?RemoveDirectoryW(Api(value.path).c_str()):
        DeleteFileW(Api(value.path).c_str()))!=FALSE;
}
bool ExistsNoFollow(const std::wstring& p){
    if(GetFileAttributesW(Api(p).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    HANDLE h=CreateFileW(Api(p).c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_ACCESS_DENIED;
    CloseHandle(h);
    return true;
}
}
std::wstring ResolvedTarget(const Entry& entry){
    if(entry.target.empty())return {};
    return FullPath(entry.relative?Join(ParentPath(entry.path),entry.target):
                    entry.target);
}
const wchar_t* StatusLabel(Health value){
    switch(value){
        case Health::Valid:return L"Valid";
        case Health::Broken:return L"Broken";
        case Health::AccessDenied:return L"Access denied";
        default:return L"Unknown";
    }
}
const wchar_t* KindLabel(const Entry& value){
    switch(value.kind){
        case ReparseKind::SymbolicFile:return L"File symlink";
        case ReparseKind::SymbolicDirectory:return L"Directory symlink";
        case ReparseKind::Junction:return L"Junction";
        case ReparseKind::Other:return L"Other reparse point";
        default:return value.hardlinks>1?L"Hardlink":L"Other";
    }
}
Entry InspectOne(const std::wstring& path){
    Entry e;
    e.path=FullPath(path);
    if(e.path.empty()){e.error=ERROR_INVALID_NAME;return e;}
    const LinkDetails link=InspectLink(e.path,true);
    e.kind=link.kind;
    e.target=link.target;
    e.relative=link.relative;
    e.hardlinks=link.hardlinkCount;
    e.error=link.error;
    if(link.error!=ERROR_SUCCESS){
        if(link.error==ERROR_FILE_NOT_FOUND||link.error==ERROR_PATH_NOT_FOUND)
            e.health=Health::Broken;
        else if(link.error==ERROR_ACCESS_DENIED)e.health=Health::AccessDenied;
        return e;
    }
    if(link.kind==ReparseKind::None){
        if(link.hardlinkCount>1)e.health=Health::Valid;
        return e;
    }
    if(link.kind==ReparseKind::Other)return e;
    const DWORD traversal=CheckLinkTraversal(e.path);
    e.error=traversal;
    if(traversal==ERROR_SUCCESS)e.health=Health::Valid;
    else if(traversal==ERROR_FILE_NOT_FOUND||traversal==ERROR_PATH_NOT_FOUND)
        e.health=Health::Broken;
    else if(traversal==ERROR_ACCESS_DENIED)e.health=Health::AccessDenied;
    return e;
}
Scan FindLinks(const std::wstring& root,unsigned maxDepth,
               std::size_t maxVisited,std::size_t maxLinks){
    Scan scan;
    const auto start=FullPath(root);
    const DWORD attrs=GetFileAttributesW(Api(start).c_str());
    if(start.empty()||attrs==INVALID_FILE_ATTRIBUTES||
        !(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){
        scan.firstError=attrs==INVALID_FILE_ATTRIBUTES?GetLastError():ERROR_DIRECTORY;
        return scan;
    }
    maxDepth=(std::min)(maxDepth,8u);
    maxVisited=(std::min)(maxVisited,std::size_t{10000});
    maxLinks=(std::min)(maxLinks,std::size_t{4000});
    struct Folder{std::wstring name;unsigned depth;};
    std::vector<Folder> todo{{start,0}};
    while(!todo.empty()&&!scan.truncated){
        Folder dir=std::move(todo.back());todo.pop_back();
        WIN32_FIND_DATAW data{};
        HANDLE search=FindFirstFileW(Api(Join(dir.name,L"*")).c_str(),&data);
        if(search==INVALID_HANDLE_VALUE){
            if(scan.firstError==ERROR_SUCCESS)scan.firstError=GetLastError();
            continue;
        }
        do{
            if(wcscmp(data.cFileName,L".")==0||wcscmp(data.cFileName,L"..")==0)
                continue;
            if(++scan.visited>maxVisited){scan.truncated=true;break;}
            const auto full=Join(dir.name,data.cFileName);
            const bool directory=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            const bool reparse=(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
            if(reparse||!directory){
                const Entry entry=InspectOne(full);
                if(entry.kind!=ReparseKind::None||entry.hardlinks>1){
                    scan.items.push_back(entry);
                    if(scan.items.size()>=maxLinks){scan.truncated=true;break;}
                }
            }
            if(directory&&!reparse&&dir.depth<maxDepth)
                todo.push_back({full,dir.depth+1});
        }while(FindNextFileW(search,&data));
        const DWORD endError=GetLastError();
        if(!scan.truncated&&endError!=ERROR_NO_MORE_FILES&&
           scan.firstError==ERROR_SUCCESS)scan.firstError=endError;
        FindClose(search);
    }
    return scan;
}
bool Capture(const std::wstring& path,Identity& out){
    Identity id;
    if(!ReadIdentity(path,id))return false;
    const auto details=InspectLink(path,true);
    if(details.error!=ERROR_SUCCESS||details.kind==ReparseKind::Other||
       (details.kind==ReparseKind::None&&details.hardlinkCount<=1))return false;
    id.kind=details.kind;
    id.target=details.target;
    id.relative=details.relative;
    id.hardlink=details.kind==ReparseKind::None&&details.hardlinkCount>1;
    out=std::move(id);
    return true;
}
UndoSummary Undo(const std::vector<Identity>& batch){
    UndoSummary report;
    for(auto it=batch.rbegin();it!=batch.rend();++it){
        Identity current;
        if(!Capture(it->path,current)||!SameIdentity(current,*it)){
            ++report.skipped;
            if(report.firstError==ERROR_SUCCESS)report.firstError=ERROR_INVALID_DATA;
            continue;
        }
        if(!DeleteOnlyLink(*it)){
            ++report.skipped;
            if(report.firstError==ERROR_SUCCESS)report.firstError=GetLastError();
        }else ++report.deleted;
    }
    return report;
}
RepairResult RepairBroken(const std::wstring& path,const std::wstring& newTarget){
    RepairResult result;
    const auto oldPath=FullPath(path),target=FullPath(newTarget);
    if(oldPath.empty()||target.empty()||_wcsicmp(oldPath.c_str(),target.c_str())==0){
        result.code=ERROR_INVALID_NAME;
        result.explanation=L"Invalid repair path.";
        return result;
    }
    if(IsRemoteSmbPath(oldPath)){
        result.code=ERROR_NOT_SUPPORTED;
        result.explanation=L"Repair on SMB is inspection-only; no server changes.";
        return result;
    }
    const Entry existing=InspectOne(oldPath);
    if((existing.kind!=ReparseKind::SymbolicFile &&
        existing.kind!=ReparseKind::SymbolicDirectory)||
       existing.health!=Health::Broken){
        result.code=ERROR_NOT_SUPPORTED;
        result.explanation=L"Only confirmed broken local symlinks can be repaired.";
        return result;
    }
    Identity original;
    if(!Capture(oldPath,original)){
        result.code=ERROR_ACCESS_DENIED;
        result.explanation=L"Cannot verify identity of original link.";
        return result;
    }
    const bool directory=existing.kind==ReparseKind::SymbolicDirectory;
    const DWORD attrs=GetFileAttributesW(Api(target).c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES || bool(attrs&FILE_ATTRIBUTE_DIRECTORY)!=directory){
        result.code=ERROR_DIRECTORY;
        result.explanation=L"New target must exist and match the original link type.";
        return result;
    }
    const std::wstring backup=oldPath+L".tcs-repair-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64());
    if(ExistsNoFollow(backup)){
        result.code=ERROR_ALREADY_EXISTS;
        result.explanation=L"Repair backup name is already in use.";
        return result;
    }
    if(!MoveFileExW(Api(oldPath).c_str(),Api(backup).c_str(),MOVEFILE_WRITE_THROUGH)){
        result.code=GetLastError();
        result.explanation=L"Cannot move old broken link to temporary backup.";
        return result;
    }
    // Re-verify after rename BEFORE installing a replacement. An external
    // process might have changed the old path between preflight and MoveFileEx.
    // Never replace an unverified object; preserve or restore it without
    // overwriting another object at the original location.
    Identity staged;
    if(!Capture(backup,staged)||!SameIdentity(staged,original)){
        result.code=ERROR_INVALID_DATA;
        if(!ExistsNoFollow(oldPath) &&
           MoveFileExW(Api(backup).c_str(),Api(oldPath).c_str(),
                       MOVEFILE_WRITE_THROUGH))
            result.explanation=L"Repair cancelled: link changed during preparation. Original path restored.";
        else {
            result.retainedBackup=backup;
            result.explanation=L"Repair cancelled: link identity changed. Preserve backup and inspect manually.";
        }
        return result;
    }
    const auto kind=directory?LinkKind::DirectorySymbolic:LinkKind::FileSymbolic;
    const DWORD created=CreateLink(target,oldPath,kind,false);
    if(created!=ERROR_SUCCESS){
        result.code=created;
        if(!ExistsNoFollow(oldPath) && MoveFileExW(Api(backup).c_str(),
            Api(oldPath).c_str(),MOVEFILE_WRITE_THROUGH))
            result.explanation=L"Repair failed: original broken link restored.";
        else {
            result.retainedBackup=backup;
            result.explanation=L"Repair failed; original retained in backup path.";
        }
        return result;
    }
    // The target may remain intact if later cleanup fails. Never recurse.
    Identity oldBackup;
    if(!Capture(backup,oldBackup)||!SameIdentity(oldBackup,original)||
       !DeleteOnlyLink(oldBackup)){
        result.retainedBackup=backup;
        result.explanation=L"New link created; old link kept at backup path.";
        return result;
    }
    result.explanation=L"Repaired symlink verified; original target was not modified.";
    return result;
}

chain::Result InspectChain(const std::wstring& path, unsigned maxHops) {
    // Do not probe remote server capabilities or change SMB policy: read-only.
    // InspectLink opens the REPARSE_POINT itself, not its final target.
    auto observe = [](const std::wstring& item) -> chain::Observation {
        chain::Observation node;
        const auto details = InspectLink(item);
        node.error = details.error;
        node.target = details.target;
        node.relative = details.relative;
        switch (details.kind) {
            case ReparseKind::SymbolicFile:
                node.kind = chain::Kind::FileSymlink;
                break;
            case ReparseKind::SymbolicDirectory:
                node.kind = chain::Kind::DirectorySymlink;
                break;
            case ReparseKind::Junction:
                node.kind = chain::Kind::Junction;
                break;
            case ReparseKind::Other:
                node.kind = chain::Kind::Unsupported;
                break;
            default:
                node.kind = chain::Kind::Regular;
                break;
        }
        return node;
    };
    auto resolve = [](const std::wstring& linkPath, const chain::Observation& node) {
        std::wstring target = node.target;
        // Junctions usually contain native NT paths such as \\??\\C:\\Folder.
        // Convert the print/substitute forms before normal Win32 normalization.
        if (target.starts_with(L"\\??\\UNC\\"))
            target = L"\\\\" + target.substr(8);
        else if (target.starts_with(L"\\\\?\\UNC\\"))
            target = L"\\\\" + target.substr(8);
        else if (target.starts_with(L"\\??\\"))
            target.erase(0, 4);
        else if (target.starts_with(L"\\\\?\\"))
            target.erase(0, 4);
        return FullPath(node.relative ? Join(ParentPath(linkPath), target) : target);
    };
    return chain::Walk(FullPath(path), observe, resolve, maxHops);
}

} // namespace tcs::manager
