// Native, explicit-update installer for TC Symbolic Links.
// No PowerShell, CMD, companion executable or unsigned downloaded code runs.
// The installed (trusted) WDX bootstraps a protected, same-directory helper.
// The helper replaces ONLY the expected WDX files after TC has closed.
#include "NativeUpdateInstall.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace tcs::update {
namespace {
namespace fs = std::filesystem;
constexpr UINT kDownloadProgress = WM_APP + 250;
constexpr size_t kMaximumDownload = 32u * 1024u * 1024u;
constexpr size_t kMaximumMember = 16u * 1024u * 1024u;
constexpr wchar_t kZipName[] = L"TC-Symbolic-Links-win32-win64.zip";
constexpr std::array<const char*,3> kMembers{
    "SymlinkInspector.wdx", "SymlinkInspector.wdx64", "pluginst.inf"
};
struct Http {
    HINTERNET h=nullptr;
    ~Http(){ if(h) WinHttpCloseHandle(h); }
};
std::wstring Join(const std::wstring& base,const std::wstring& leaf) {
    return (fs::path(base)/leaf).wstring();
}
std::wstring OwnDll() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&OwnDll),&module)) return {};
    std::wstring result(32768,L'\0');
    DWORD size=GetModuleFileNameW(module,result.data(),static_cast<DWORD>(result.size()));
    if(!size||size>=result.size()) return {};
    result.resize(size);
    return result;
}
bool TagValid(const std::wstring& tag) {
    static const std::wregex pattern(
        LR"(^v[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}(-preview\.[0-9]{1,8})?$)");
    return std::regex_match(tag,pattern);
}
bool IsWdx(const std::wstring& path) {
    const auto name=fs::path(path).filename().wstring();
    return _wcsicmp(name.c_str(),L"SymlinkInspector.wdx")==0 ||
           _wcsicmp(name.c_str(),L"SymlinkInspector.wdx64")==0;
}
bool Folder(const std::wstring& path) {
    if(CreateDirectoryW(path.c_str(),nullptr)) return true;
    return GetLastError()==ERROR_ALREADY_EXISTS &&
        (GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
std::wstring StageRoot() {
    std::wstring local(32768,L'\0');
    const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),
        static_cast<DWORD>(local.size()));
    if(!size||size>=local.size()) return {};
    local.resize(size);
    return Join(Join(Join(local,L"TCSymbolicLinks"),L"Updates"),L"Native");
}
std::wstring UniqueFolder(const std::wstring& parent) {
    for(unsigned i=0;i<32;i++) {
        auto candidate=Join(parent,L".tcs-native-"+std::to_wstring(GetCurrentProcessId())+
            L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(i));
        if(CreateDirectoryW(candidate.c_str(),nullptr)) return candidate;
        if(GetLastError()!=ERROR_ALREADY_EXISTS) return {};
    }
    return {};
}
bool ReadBytes(const std::wstring& path,std::vector<unsigned char>& content,
               size_t maximum=kMaximumDownload) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER len{};
    bool ok=GetFileSizeEx(h,&len) && len.QuadPart>0 &&
        static_cast<unsigned long long>(len.QuadPart)<=maximum;
    if(ok) {
        content.resize(static_cast<size_t>(len.QuadPart));
        DWORD n=0;
        ok=ReadFile(h,content.data(),static_cast<DWORD>(content.size()),&n,nullptr) &&
            n==content.size();
    }
    CloseHandle(h);
    return ok;
}
bool WriteBytes(const std::wstring& path,const std::vector<unsigned char>& content) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    DWORD n=0;
    const bool ok=WriteFile(h,content.data(),static_cast<DWORD>(content.size()),&n,nullptr) &&
        n==content.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if(!ok) DeleteFileW(path.c_str());
    return ok;
}
std::wstring Hex(const unsigned char* data,size_t size) {
    constexpr wchar_t digits[]=L"0123456789abcdef";
    std::wstring result;
    for(size_t i=0;i<size;i++) {
        result+=digits[data[i]>>4];result+=digits[data[i]&15];
    }
    return result;
}
bool Sha256(const std::vector<unsigned char>& data,std::wstring& digest) {
    BCRYPT_ALG_HANDLE provider=nullptr;
    if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        return false;
    unsigned char hash[32]{};
    const NTSTATUS result=BCryptHash(provider,nullptr,0,
        const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),
        hash,static_cast<ULONG>(sizeof(hash)));
    BCryptCloseAlgorithmProvider(provider,0);
    if(result<0) return false;
    digest=Hex(hash,sizeof(hash));return true;
}
bool HttpsGet(const wchar_t* host,const std::wstring& path,
              std::vector<unsigned char>& body,size_t limit,HWND progress=nullptr) {
    Http session{WinHttpOpen(L"TCSymbolicLinks/0.2.22 native updater",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h) return false;
    WinHttpSetTimeouts(session.h,12000,12000,20000,20000);
    Http connection{WinHttpConnect(session.h,host,INTERNET_DEFAULT_HTTPS_PORT,0)};
    if(!connection.h) return false;
    Http request{WinHttpOpenRequest(connection.h,L"GET",path.c_str(),
        nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!request.h) return false;
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect,sizeof(redirect)))return false;
    const wchar_t* headers=(_wcsicmp(host,L"api.github.com")==0) ?
        L"Accept: application/vnd.github+json\r\n" :
        L"Accept: application/octet-stream\r\n";
    if(!WinHttpSendRequest(request.h,headers,static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       !WinHttpReceiveResponse(request.h,nullptr)) return false;
    DWORD status=0,bytes=sizeof(status);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX) ||
        status!=200) return false;
    DWORD length=0;
    bytes=sizeof(length);
    const bool hasLength=WinHttpQueryHeaders(request.h,
        WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&length,&bytes,WINHTTP_NO_HEADER_INDEX)!=FALSE;
    if(hasLength && length>limit) return false;
    for(;;) {
        DWORD available=0;
        if(!WinHttpQueryDataAvailable(request.h,&available)) return false;
        if(!available) break;
        if(body.size()+available>limit) return false;
        size_t offset=body.size();
        body.resize(offset+available);
        DWORD read=0;
        if(!WinHttpReadData(request.h,body.data()+offset,available,&read)) return false;
        body.resize(offset+read);
        if(!read) break;
        if(progress && hasLength && length) {
            const size_t ratio=body.size()*size_t{100}/static_cast<size_t>(length);
            const unsigned percent=static_cast<unsigned>((std::min)(size_t{99},ratio));
            PostMessageW(progress,kDownloadProgress,percent,0);
        }
    }
    return !body.empty();
}
bool JsonStringField(const std::string& object,const std::string& field,
                     std::string& value) {
    const std::regex pattern("\""+field+"\"\\s*:\\s*\"([^\"\\\\]*)\"");
    std::smatch match;
    if(!std::regex_search(object,match,pattern)) return false;
    value=match[1].str();return true;
}
bool ReleaseDigest(const std::wstring& tag,std::wstring& expected) {
    if(!TagValid(tag)) return false;
    std::vector<unsigned char> data;
    if(!HttpsGet(L"api.github.com",
        L"/repos/sgennadi/TC-Symbolic-Links/releases/tags/"+tag,
        data,2u*1024u*1024u)) return false;
    std::string json(data.begin(),data.end());
    if(json.find("\"draft\":true")!=std::string::npos) return false;
    const auto assets=json.find("\"assets\"");
    if(assets==std::string::npos) return false;
    const auto open=json.find('[',assets);
    if(open==std::string::npos) return false;
    size_t i=open+1;
    int matches=0;
    while(i<json.size()) {
        while(i<json.size() && (json[i]==' '||json[i]=='\n'||json[i]==','))i++;
        if(i>=json.size()||json[i]==']')break;
        if(json[i]!='{') return false;
        const size_t begin=i;
        int depth=0;
        bool quoted=false,escaped=false;
        for(;i<json.size();i++) {
            const char c=json[i];
            if(escaped){escaped=false;continue;}
            if(quoted&&c=='\\'){escaped=true;continue;}
            if(c=='"'){quoted=!quoted;continue;}
            if(quoted)continue;
            if(c=='{') depth++;
            if(c=='}' && --depth==0){i++;break;}
        }
        if(depth!=0)return false;
        const auto object=json.substr(begin,i-begin);
        std::string name;
        if(!JsonStringField(object,"name",name))continue;
        if(name!="TC-Symbolic-Links-win32-win64.zip")continue;
        matches++;
        std::string digest,url,state;
        if(!JsonStringField(object,"digest",digest) ||
           !JsonStringField(object,"browser_download_url",url) ||
           !JsonStringField(object,"state",state) || state!="uploaded")
            return false;
        const std::string expectedUrl="https://github.com/sgennadi/TC-Symbolic-Links/releases/download/"+
            std::string(tag.begin(),tag.end())+"/TC-Symbolic-Links-win32-win64.zip";
        if(url!=expectedUrl || !std::regex_match(digest,
           std::regex(R"(^sha256:[a-f0-9]{64}$)"))) return false;
        expected=std::wstring(digest.begin()+7,digest.end());
    }
    return matches==1;
}
uint16_t U16(const std::vector<unsigned char>& b,size_t i) {
    return uint16_t(b[i])|(uint16_t(b[i+1])<<8);
}
uint32_t U32(const std::vector<unsigned char>& b,size_t i) {
    return uint32_t(U16(b,i))|(uint32_t(U16(b,i+2))<<16);
}
struct Entry {
    std::string name;
    std::vector<unsigned char> bytes;
};
bool ValidPe(const std::vector<unsigned char>& b,uint16_t arch) {
    if(b.size()<512 || U16(b,0)!=0x5a4d) return false;
    const uint32_t offset=U32(b,0x3c);
    return offset>=64 && offset+24<=b.size() &&
        U32(b,offset)==0x4550 && U16(b,offset+4)==arch;
}
bool ExtractTrustedZip(const std::vector<unsigned char>& zip,
                       std::array<Entry,3>& entries) {
    if(zip.size()<22 || zip.size()>kMaximumDownload) return false;
    size_t eocd=zip.size();
    const size_t earliest=zip.size()>65557?zip.size()-65557:0;
    for(size_t i=zip.size()-22;;--i) {
        if(U32(zip,i)==0x06054b50){eocd=i;break;}
        if(i==earliest)break;
    }
    if(eocd==zip.size() || U16(zip,eocd+8)!=3 || U16(zip,eocd+10)!=3 ||
       U16(zip,eocd+4)!=0 || U16(zip,eocd+6)!=0) return false;
    const uint32_t cdOffset=U32(zip,eocd+16), cdSize=U32(zip,eocd+12);
    if(static_cast<size_t>(cdOffset)+cdSize>eocd) return false;
    size_t pos=cdOffset;
    std::array<bool,3> found{};
    for(int entry=0;entry<3;entry++) {
        if(pos+46>zip.size() || U32(zip,pos)!=0x02014b50)return false;
        const uint16_t flags=U16(zip,pos+8),method=U16(zip,pos+10);
        const uint32_t crc=U32(zip,pos+16),compressed=U32(zip,pos+20);
        const uint32_t unpacked=U32(zip,pos+24);
        const uint16_t nameLen=U16(zip,pos+28),extra=U16(zip,pos+30);
        const uint16_t comment=U16(zip,pos+32);
        const uint32_t local=U32(zip,pos+42);
        if((flags&1)!=0 || (flags&0x40)!=0 ||
           (method!=0&&method!=8) || unpacked>kMaximumMember ||
           compressed>kMaximumMember || nameLen>128 ||
           pos+46ull+nameLen+extra+comment>zip.size() ||
           local+30ull>zip.size())return false;
        const std::string name(reinterpret_cast<const char*>(zip.data()+pos+46),nameLen);
        size_t index=3;
        for(size_t j=0;j<3;j++)if(name==kMembers[j])index=j;
        if(index>=3||found[index])return false;
        found[index]=true;
        if(U32(zip,local)!=0x04034b50 ||
           U16(zip,local+8)!=method)return false;
        const uint16_t locName=U16(zip,local+26),locExtra=U16(zip,local+28);
        const size_t begin=static_cast<size_t>(local)+30+locName+locExtra;
        if(begin>zip.size() || compressed>zip.size()-begin ||
           locName!=nameLen ||
           std::string(reinterpret_cast<const char*>(zip.data()+local+30),locName)!=name)
           return false;
        std::vector<unsigned char> out(unpacked+1,0);
        if(method==0) {
            if(compressed!=unpacked)return false;
            std::copy_n(zip.data()+begin,compressed,out.data());
        } else {
            z_stream z{};
            z.next_in=const_cast<Bytef*>(zip.data()+begin);
            z.avail_in=compressed;
            z.next_out=out.data();
            z.avail_out=static_cast<uInt>(out.size());
            if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)return false;
            const int result=inflate(&z,Z_FINISH);
            const bool good=result==Z_STREAM_END && z.total_in==compressed &&
                z.total_out==unpacked;
            inflateEnd(&z);
            if(!good)return false;
        }
        out.resize(unpacked);
        const uLong actualCrc=crc32(crc32(0L,Z_NULL,0),out.data(),
                                    static_cast<uInt>(out.size()));
        if(actualCrc!=crc)return false;
        entries[index]={name,std::move(out)};
        pos+=46ull+nameLen+extra+comment;
    }
    if(pos!=static_cast<size_t>(cdOffset)+cdSize ||
       !found[0]||!found[1]||!found[2] ||
       !ValidPe(entries[0].bytes,0x014c) ||
       !ValidPe(entries[1].bytes,0x8664))return false;
    const std::string inf(entries[2].bytes.begin(),entries[2].bytes.end());
    if(inf.size()>4096 || inf.find("[plugininstall]")==std::string::npos ||
       inf.find("type=wdx")==std::string::npos ||
       inf.find("file=SymlinkInspector.wdx")==std::string::npos)
       return false;
    return true;
}
bool Writable(const std::wstring& dir) {
    const auto test=Join(dir,L".tcs-write-"+std::to_wstring(GetCurrentProcessId())+L".tmp");
    HANDLE h=CreateFileW(test.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
bool Spawn(const std::wstring& dll,const std::wstring& arguments,
           bool elevate,std::wstring& error) {
    wchar_t system[MAX_PATH+1]{};
    if(!GetSystemDirectoryW(system,MAX_PATH)) {
        error=L"Cannot locate system rundll32.exe.";return false;
    }
    const auto executable=Join(system,L"rundll32.exe");
    const auto parameters=L"\""+dll+L"\",TCLinkApplyUpdate "+arguments;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
    info.lpVerb=elevate?L"runas":L"open";
    info.lpFile=executable.c_str();
    info.lpParameters=parameters.c_str();
    info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info)) {
        error=L"Could not start native installer (UAC may have been canceled): "+
              std::to_wstring(GetLastError());
        return false;
    }
    if(info.hProcess)CloseHandle(info.hProcess);
    return true;
}
bool OneInstalled(const std::wstring& module) {
    return IsWdx(module) &&
        GetFileAttributesW(module.c_str())!=INVALID_FILE_ATTRIBUTES;
}
bool WaitProcess(DWORD id,DWORD ms) {
    if(!id)return true;
    HANDLE handle=OpenProcess(SYNCHRONIZE,FALSE,id);
    if(!handle)return GetLastError()==ERROR_INVALID_PARAMETER;
    const DWORD result=WaitForSingleObject(handle,ms);
    CloseHandle(handle);
    return result==WAIT_OBJECT_0;
}
bool TotalCommanderRunning() {
    DWORD ownSession=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession))return true;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W item{sizeof(item)};
    bool found=false;
    if(Process32FirstW(snap,&item)) do {
        if(_wcsicmp(item.szExeFile,L"TOTALCMD.EXE")==0 ||
           _wcsicmp(item.szExeFile,L"TOTALCMD64.EXE")==0) {
            DWORD processSession=0;
            if(ProcessIdToSessionId(item.th32ProcessID,&processSession) &&
               processSession==ownSession) {
                found=true;break;
            }
        }
    } while(Process32NextW(snap,&item));
    CloseHandle(snap);
    return found;
}
std::wstring ReadableError(DWORD code) {
    return L"Win32 error "+std::to_wstring(code);
}
void ShowResult(const std::wstring& msg,bool good) {
    MessageBoxW(nullptr,msg.c_str(),L"TC Symbolic Links - Native Update",
        MB_OK | (good?MB_ICONINFORMATION:MB_ICONERROR)|MB_TOPMOST);
}
struct Arguments {
    std::wstring phase,stage,prepared,install,tag;
    DWORD caller=0,bootstrap=0;
};
bool ParseArgs(std::wstring_view input,Arguments& a) {
    const std::wstring command=L"rundll32.exe "+std::wstring(input);
    int count=0;
    LPWSTR* argv=CommandLineToArgvW(command.c_str(),&count);
    if(!argv)return false;
    bool valid=true;
    for(int i=1;i<count;i++) {
        if(i+1>=count){valid=false;break;}
        const std::wstring_view flag(argv[i++]);
        const std::wstring value(argv[i]);
        if(flag==L"--phase")a.phase=value;
        else if(flag==L"--stage")a.stage=value;
        else if(flag==L"--prepared")a.prepared=value;
        else if(flag==L"--install")a.install=value;
        else if(flag==L"--tag")a.tag=value;
        else if(flag==L"--caller")a.caller=wcstoul(value.c_str(),nullptr,10);
        else if(flag==L"--bootstrap")a.bootstrap=wcstoul(value.c_str(),nullptr,10);
        else valid=false;
    }
    LocalFree(argv);
    return valid && OneInstalled(a.install);
}
bool PrepareInstall(const Arguments& a,std::wstring& error) {
    // Only the already-installed module may authorize elevated work.
    const auto own=OwnDll();
    if(_wcsicmp(own.c_str(),a.install.c_str())!=0 || !TagValid(a.tag)) {
        error=L"Installer module or release tag does not match.";return false;
    }
    // UAC with alternate administrator credentials can change LOCALAPPDATA.
    // Validate the strict stage directory shape; the archive is always
    // reauthenticated against GitHub's release SHA-256 before any use.
    const fs::path staged(a.stage);
    const auto stageName=staged.filename().wstring();
    const auto nativeDir=staged.parent_path().filename().wstring();
    const auto updatesDir=staged.parent_path().parent_path().filename().wstring();
    const auto appDir=staged.parent_path().parent_path().parent_path().filename().wstring();
    if(!staged.is_absolute() || staged.lexically_normal()!=staged ||
       stageName.rfind(L".tcs-native-",0)!=0 ||
       _wcsicmp(nativeDir.c_str(),L"Native")!=0 ||
       _wcsicmp(updatesDir.c_str(),L"Updates")!=0 ||
       _wcsicmp(appDir.c_str(),L"TCSymbolicLinks")!=0) {
        error=L"Staged ZIP must be inside a TC Symbolic Links native update directory.";
        return false;
    }
    std::vector<unsigned char> zip;
    if(!ReadBytes(Join(a.stage,kZipName),zip)) {
        error=L"Staged ZIP unavailable.";return false;
    }
    std::wstring expected,actual;
    if(!ReleaseDigest(a.tag,expected) || !Sha256(zip,actual) || actual!=expected) {
        error=L"Release digest mismatch or trusted GitHub API unavailable.";return false;
    }
    std::array<Entry,3> entries;
    if(!ExtractTrustedZip(zip,entries)) {
        error=L"ZIP content or PE architecture verification failed.";return false;
    }
    const auto installDir=fs::path(a.install).parent_path().wstring();
    const auto prepared=UniqueFolder(installDir);
    if(prepared.empty()) {
        error=L"Cannot create protected updater staging folder.";return false;
    }
    for(size_t i=0;i<entries.size();i++) {
        if(!WriteBytes(Join(prepared,std::wstring(kMembers[i],
            kMembers[i]+std::char_traits<char>::length(kMembers[i]))),entries[i].bytes)) {
            error=L"Cannot write verified package into protected staging folder.";
            return false;
        }
    }
    const bool x64=fs::path(a.install).extension()==L".wdx64";
    const auto runner=Join(prepared,x64?L"TCSNativeRunner.wdx64":L"TCSNativeRunner.wdx");
    if(!CopyFileW(a.install.c_str(),runner.c_str(),TRUE)) {
        error=L"Cannot copy trusted installed WDX into protected helper location.";
        return false;
    }
    const auto arguments=L"--phase apply --prepared \""+prepared+
        L"\" --install \""+a.install+L"\" --caller "+std::to_wstring(a.caller)+
        L" --bootstrap "+std::to_wstring(GetCurrentProcessId());
    return Spawn(runner,arguments,false,error);
}
bool ApplyFiles(const std::wstring& prepared,const std::wstring& installedFolder,
                std::wstring& error) {
    const std::array<std::wstring,3> names{
        L"SymlinkInspector.wdx",L"SymlinkInspector.wdx64",L"pluginst.inf"
    };
    struct Replacement {
        std::wstring target, payload, backup, pending;
        bool installed=false;
    };
    const auto id=L".tcsbackup-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64());
    std::vector<Replacement> files;
    auto cleanupPending=[&]() {
        for(const auto& file:files)
            if(!file.pending.empty()) DeleteFileW(file.pending.c_str());
    };
    // Preflight ALL installed architecture-specific members before changing ANY.
    // A WDX may remain loaded by another TC process, even after the initiating
    // TC process closed. Never overwrite a loaded module in-place.
    for(const auto& name:names) {
        const auto target=Join(installedFolder,name);
        if(GetFileAttributesW(target.c_str())==INVALID_FILE_ATTRIBUTES)
            continue; // Do not silently install an unused architecture.
        const auto payload=Join(prepared,name);
        if(GetFileAttributesW(payload.c_str())==INVALID_FILE_ATTRIBUTES) {
            error=L"Verified staged payload missing: "+name; return false;
        }
        HANDLE probe=CreateFileW(target.c_str(),DELETE|FILE_READ_DATA,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(probe==INVALID_HANDLE_VALUE) {
            const DWORD code=GetLastError();
            error=L"Cannot safely replace "+name+L": "+ReadableError(code)+
                L". Close every Total Commander instance and any process that "
                L"has loaded the plugin. No installed files were modified.";
            return false;
        }
        CloseHandle(probe);
        files.push_back({target,payload,target+id+L".bak",target+id+L".new"});
    }
    if(files.empty()) {
        error=L"No installed WDX files were found. No changes made.";
        return false;
    }
    // Prepare complete verified replacement set and backups before the commit.
    for(const auto& file:files) {
        if(!CopyFileW(file.target.c_str(),file.backup.c_str(),TRUE)) {
            error=L"Cannot back up "+file.target+L": "+ReadableError(GetLastError());
            cleanupPending(); return false;
        }
        if(!CopyFileW(file.payload.c_str(),file.pending.c_str(),TRUE)) {
            error=L"Cannot prepare "+file.target+L": "+ReadableError(GetLastError());
            cleanupPending(); return false;
        }
    }
    // Commit. Keep backups intact so failures are recoverable without loss.
    size_t committed=0;
    for(;committed<files.size();++committed) {
        const auto& file=files[committed];
        if(!MoveFileExW(file.pending.c_str(),file.target.c_str(),
            MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            const DWORD code=GetLastError();
            error=L"Cannot replace "+fs::path(file.target).filename().wstring()+
                L": "+ReadableError(code)+
                L". Another process may still be using the WDX.";
            break;
        }
    }
    cleanupPending();
    if(committed!=files.size()) {
        bool restored=true;
        for(size_t i=committed;i>0;--i) {
            const auto& file=files[i-1];
            const auto restore=file.target+id+L".restore";
            if(!CopyFileW(file.backup.c_str(),restore.c_str(),TRUE) ||
               !MoveFileExW(restore.c_str(),file.target.c_str(),
                   MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                restored=false;
                DeleteFileW(restore.c_str());
            }
        }
        error+=restored?L"\nPrevious installed files restored; backups retained.":
            L"\nRollback incomplete; .tcsbackup files retained for manual recovery.";
        return false;
    }
    // Verify committed bytes rather than reporting success after a mere rename.
    for(const auto& file:files) {
        std::vector<unsigned char> expected,actual;
        if(!ReadBytes(file.payload,expected,kMaximumMember) ||
           !ReadBytes(file.target,actual,kMaximumMember) || expected!=actual) {
            error=L"Installed file verification failed: "+file.target+
                L". Backups have been preserved for recovery.";
            return false;
        }
    }
    return true;
}
bool InstallAfterExit(const Arguments& a,std::wstring& error) {
    const auto installedFolder=fs::path(a.install).parent_path().wstring();
    const auto stageParent=fs::path(a.prepared).parent_path().wstring();
    if(a.prepared.empty() ||
       _wcsicmp(installedFolder.c_str(),stageParent.c_str())!=0 ||
       fs::path(a.prepared).filename().wstring().rfind(L".tcs-native-",0)!=0 ||
       fs::path(OwnDll()).parent_path().wstring()!=a.prepared) {
        error=L"Protected installer directory validation failed.";return false;
    }
    // Wait for the original TC window and both rundll32 processes to exit.
    // The updater itself runs from a separate protected DLL copy.
    bool closed=false;
    for(unsigned i=0;i<600;i++) {
        if(!TotalCommanderRunning() && WaitProcess(a.caller,0) &&
           WaitProcess(a.bootstrap,0)) {closed=true;break;}
        Sleep(500);
    }
    if(!closed) {
        error=L"Timed out. Close every Total Commander and Link Maker window.";
        return false;
    }
    return ApplyFiles(a.prepared,installedFolder,error);
}
} // namespace

bool ApplyNativeFilesForTest(const std::wstring& prepared,
                             const std::wstring& installedFolder,
                             std::wstring& error) {
    // Only the local Windows integration test links this C++ helper; the
    // rundll32 export always uses guarded Apply after trusted verification.
    return ApplyFiles(prepared,installedFolder,error);
}
bool VerifyNativeZipBytes(const std::vector<unsigned char>& zip) {
    std::array<Entry,3> entries;
    return ExtractTrustedZip(zip,entries);
}
bool StageNativeUpdate(const std::wstring& tag,HWND progressWindow,
                       NativeStage& stage) {
    if(!TagValid(tag)) {
        stage.description=L"Invalid release tag.";return false;
    }
    std::wstring expected;
    if(!ReleaseDigest(tag,expected)) {
        stage.description=L"Official release ZIP or SHA-256 digest not available.";
        return false;
    }
    const auto path=L"/sgennadi/TC-Symbolic-Links/releases/download/"+tag+
        L"/"+kZipName;
    std::vector<unsigned char> zip;
    if(!HttpsGet(L"github.com",path,zip,kMaximumDownload,progressWindow)) {
        stage.description=L"Cannot download verified ZIP from GitHub.";
        return false;
    }
    std::wstring actual;
    if(!Sha256(zip,actual) || actual!=expected) {
        stage.description=L"Downloaded ZIP SHA-256 does not match GitHub release.";
        return false;
    }
    std::array<Entry,3> checked;
    if(!ExtractTrustedZip(zip,checked)) {
        stage.description=L"ZIP or x86/x64 DLL verification failed.";
        return false;
    }
    const auto root=StageRoot();
    if(root.empty()) {
        stage.description=L"LOCALAPPDATA unavailable.";return false;
    }
    const auto a=fs::path(root).parent_path().parent_path().wstring();
    const auto b=fs::path(root).parent_path().wstring();
    if(!Folder(a)||!Folder(b)||!Folder(root)) {
        stage.description=L"Cannot create local Updates folder.";return false;
    }
    stage.folder=UniqueFolder(root);
    if(stage.folder.empty() ||
       !WriteBytes(Join(stage.folder,kZipName),zip)) {
        stage.description=L"Cannot stage verified update ZIP.";return false;
    }
    stage.tag=tag;
    stage.description=L"Downloaded and verified from official GitHub Release (SHA-256 OK).";
    if(progressWindow)PostMessageW(progressWindow,kDownloadProgress,100,0);
    return true;
}
bool BeginNativeInstall(const NativeStage& stage,
                        const std::wstring& installedWdx,
                        DWORD originalHostPid,std::wstring& error) {
    if(!OneInstalled(installedWdx)||stage.folder.empty()||!TagValid(stage.tag)) {
        error=L"Installed WDX or staged release is missing.";return false;
    }
    // The *installed*, trusted module is the only elevated code. Never
    // execute the downloaded WDX or a DLL in LOCALAPPDATA with runas.
    const bool elevatedNeeded=!Writable(fs::path(installedWdx).parent_path().wstring());
    const auto args=L"--phase prepare --stage \""+stage.folder+
        L"\" --install \""+installedWdx+L"\" --tag "+stage.tag+
        L" --caller "+std::to_wstring(originalHostPid);
    return Spawn(installedWdx,args,elevatedNeeded,error);
}
void NativeInstallerWorker(std::wstring_view arguments) {
    Arguments args;
    if(!ParseArgs(arguments,args)) {
        ShowResult(L"Native updater arguments are invalid. No files changed.",false);
        return;
    }
    std::wstring error;
    bool success=false;
    if(args.phase==L"prepare") success=PrepareInstall(args,error);
    else if(args.phase==L"apply") success=InstallAfterExit(args,error);
    else error=L"Unknown native updater phase.";
    if(!success)
        ShowResult(error.empty()?L"Native update could not be completed.":error,false);
    else if(args.phase==L"apply")
        ShowResult(L"TC Symbolic Links update installed successfully.\nRestart Total Commander.",true);
}
} // namespace tcs::update
