#include "PosixSmbBackend.h"
#include "SmbPath.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <vector>

namespace tcs::posix {
namespace {
thread_local bool lastSuccess=false;
thread_local std::wstring lastDiagnostic;

struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    Handle()=default;
    explicit Handle(HANDLE value):h(value){}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    void reset(HANDLE value=INVALID_HANDLE_VALUE) {
        if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        h=value;
    }
    HANDLE release(){HANDLE tmp=h;h=INVALID_HANDLE_VALUE;return tmp;}
    bool valid()const{return h && h!=INVALID_HANDLE_VALUE;}
};
struct Profile {
    std::wstring host,user;
    unsigned port=22;
};
std::wstring IniPath(){
    std::wstring appData(32768,L'\0');
    const DWORD n=GetEnvironmentVariableW(L"APPDATA",appData.data(),
        static_cast<DWORD>(appData.size()));
    if(!n||n>=appData.size())return {};
    appData.resize(n);
    return appData+L"\\TCSymbolicLinks\\posix-shares.ini";
}
std::wstring IniValue(const std::wstring& ini,const std::wstring& section,
                      const wchar_t* name){
    wchar_t value[512]{};
    GetPrivateProfileStringW(section.c_str(),name,L"",value,512,ini.c_str());
    return value;
}
bool SafeHost(const std::wstring& value){
    if(value.empty()||value.size()>250||value.front()==L'-')return false;
    for(wchar_t c:value)
        if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||
            (c>=L'0'&&c<=L'9')||c==L'-'||c==L'.'||c==L'_'))
            return false;
    return true;
}
bool SafeUser(const std::wstring& value){
    return SafeHost(value) && value!=L"." && value!=L"..";
}
bool LoadProfile(const std::wstring& shareRoot, Profile& profile){
    const auto ini=IniPath();
    if(ini.empty())return false;
    if(GetPrivateProfileIntW(shareRoot.c_str(),L"Enabled",0,ini.c_str())!=1)
        return false;
    // Explicit SSH user and host, do not infer credentials from SMB session.
    profile.host=IniValue(ini,shareRoot,L"SSHHost");
    profile.user=IniValue(ini,shareRoot,L"SSHUser");
    const int port=GetPrivateProfileIntW(shareRoot.c_str(),L"SSHPort",22,ini.c_str());
    if(!SafeHost(profile.host)||!SafeUser(profile.user)||port<1||port>65535)
        return false;
    profile.port=static_cast<unsigned>(port);
    return true;
}
bool ToUtf8(const std::wstring& input,std::string& output) {
    if(input.empty()||input.size()>4096)return false;
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        input.c_str(),static_cast<int>(input.size()),nullptr,0,nullptr,nullptr);
    if(bytes<=0)return false;
    output.resize(static_cast<size_t>(bytes));
    return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        input.c_str(),static_cast<int>(input.size()),output.data(),bytes,nullptr,nullptr)==bytes;
}
std::string B64(const std::string& input) {
    static constexpr char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for(size_t i=0;i<input.size();i+=3){
        const auto a=static_cast<unsigned char>(input[i]);
        const auto b=i+1<input.size()?static_cast<unsigned char>(input[i+1]):0;
        const auto c=i+2<input.size()?static_cast<unsigned char>(input[i+2]):0;
        result+=t[a>>2];
        result+=t[((a&3)<<4)|(b>>4)];
        result+=i+1<input.size()?t[((b&15)<<2)|(c>>6)]:'=';
        result+=i+2<input.size()?t[c&63]:'=';
    }
    return result;
}
std::wstring RelativeToShare(const std::wstring& path,const std::wstring& root) {
    if(path.size()<=root.size() || _wcsnicmp(path.c_str(),root.c_str(),root.size())!=0 ||
       path[root.size()]!=L'\\')return {};
    std::wstring rel=path.substr(root.size()+1);
    if(rel.empty() || rel.find(L'/')!=std::wstring::npos || rel.find(L'\0')!=std::wstring::npos)
        return {};
    std::replace(rel.begin(),rel.end(),L'\\',L'/');
    if(rel.starts_with(L"/") || rel.ends_with(L"/"))return {};
    return rel;
}
std::wstring SshPath() {
    wchar_t sys[MAX_PATH+1]{};
    const UINT n=GetSystemDirectoryW(sys,MAX_PATH);
    if(!n||n>=MAX_PATH)return {};
    return std::wstring(sys)+L"\\OpenSSH\\ssh.exe";
}
std::wstring Quote(const std::wstring& value) {
    // Host and user use a strict ASCII allow-list; the executable has an
    // absolute path. Quoted args cannot be interpreted as SSH switches.
    return L"\""+value+L"\"";
}
DWORD RunSsh(const Profile& config,const std::string& request,std::string& answer){
    const auto exe=SshPath();
    if(exe.empty()||GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES){
        lastDiagnostic=L"Windows OpenSSH client is unavailable.";return ERROR_FILE_NOT_FOUND;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE stdoutReadRaw=nullptr,stdoutWriteRaw=nullptr;
    if(!CreatePipe(&stdoutReadRaw,&stdoutWriteRaw,&sa,0))return GetLastError();
    Handle stdoutRead(stdoutReadRaw), stdoutWrite(stdoutWriteRaw);
    if(!SetHandleInformation(stdoutRead.h,HANDLE_FLAG_INHERIT,0))return GetLastError();
    HANDLE stdinReadRaw=nullptr,stdinWriteRaw=nullptr;
    if(!CreatePipe(&stdinReadRaw,&stdinWriteRaw,&sa,0))return GetLastError();
    Handle stdinRead(stdinReadRaw),stdinWrite(stdinWriteRaw);
    if(!SetHandleInformation(stdinWrite.h,HANDLE_FLAG_INHERIT,0))return GetLastError();

    std::wstring command=Quote(exe)+
        L" -T -o BatchMode=yes -o StrictHostKeyChecking=yes"
        L" -o ConnectTimeout=8 -o ConnectionAttempts=1"
        L" -o PasswordAuthentication=no -o KbdInteractiveAuthentication=no"
        L" -o ForwardAgent=no -o ClearAllForwardings=yes"
        L" -o PermitLocalCommand=no -o RequestTTY=no"
        L" -o LogLevel=ERROR -p "+std::to_wstring(config.port)+
        L" -l "+Quote(config.user)+L" "+Quote(config.host)+
        L" tcs-symlink-v1";
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;
    si.hStdInput=stdinRead.h;
    si.hStdOutput=stdoutWrite.h;
    si.hStdError=stdoutWrite.h;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),mutableCommand.data(),
        nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        lastDiagnostic=L"Unable to launch Windows OpenSSH client.";
        return GetLastError();
    }
    Handle child(pi.hProcess),thread(pi.hThread);
    stdinRead.reset();
    stdoutWrite.reset();
    DWORD written=0;
    const BOOL sent=WriteFile(stdinWrite.h,request.data(),
        static_cast<DWORD>(request.size()),&written,nullptr);
    stdinWrite.reset(); // EOF is required by the server protocol.
    if(!sent||written!=request.size()){
        TerminateProcess(child.h,1);
        WaitForSingleObject(child.h,5000);
        lastDiagnostic=L"Could not send protocol request to SSH helper.";
        return ERROR_WRITE_FAULT;
    }
    const DWORD wait=WaitForSingleObject(child.h,20000);
    if(wait!=WAIT_OBJECT_0){
        TerminateProcess(child.h,1);
        WaitForSingleObject(child.h,5000);
        lastDiagnostic=L"Authorized SSH helper timed out.";
        return ERROR_TIMEOUT;
    }
    DWORD exitCode=1;
    GetExitCodeProcess(child.h,&exitCode);
    // Read only currently buffered bytes: never block on a server-held pipe.
    DWORD available=0;
    if(PeekNamedPipe(stdoutRead.h,nullptr,0,nullptr,&available,nullptr) && available){
        const DWORD capacity=(std::min)(available,DWORD{2048});
        answer.resize(capacity);
        DWORD count=0;
        if(ReadFile(stdoutRead.h,answer.data(),capacity,&count,nullptr))
            answer.resize(count);
        else answer.clear();
    }
    if(exitCode==0 && (answer=="TCS-POSIX-OK\n"||answer=="TCS-POSIX-OK\r\n"))
        return ERROR_SUCCESS;
    if(answer.find("TCS-POSIX-ERR:")!=std::string::npos){
        const auto begin=answer.find("TCS-POSIX-ERR:")+14;
        const auto end=answer.find_first_of("\r\n",begin);
        const auto reason=answer.substr(begin,end==std::string::npos?
            std::string::npos:end-begin);
        lastDiagnostic=L"POSIX server refused directory symlink: "+
            std::wstring(reason.begin(),reason.end());
        if(reason=="already-exists")return ERROR_ALREADY_EXISTS;
        if(reason=="access-denied")return ERROR_ACCESS_DENIED;
        if(reason=="source-or-parent-missing")return ERROR_PATH_NOT_FOUND;
        if(reason=="invalid-path"||reason=="invalid-request")return ERROR_INVALID_NAME;
        return ERROR_NOT_SUPPORTED;
    }
    lastDiagnostic=L"SSH server did not return an authenticated TCS-POSIX-1 result. "
                   L"Check forced command, host key, key authentication and share mapping.";
    return ERROR_NOT_SUPPORTED;
}
} // namespace

bool EnabledFor(const std::wstring& destinationUnc) {
    const auto share=tcs::smb::ShareRoot(destinationUnc);
    Profile profile;
    return !share.empty() && LoadProfile(share,profile);
}
void ResetResult(){lastSuccess=false;lastDiagnostic.clear();}
bool LastSuccess(){return lastSuccess;}
std::wstring LastDiagnostic(){return lastDiagnostic;}

DWORD CreateDirectoryLink(const std::wstring& sourceUnc,const std::wstring& linkUnc){
    ResetResult();
    const auto share=tcs::smb::ShareRoot(linkUnc);
    if(share.empty()||!tcs::smb::SameShare(sourceUnc,linkUnc)){
        lastDiagnostic=L"POSIX backend permits source and link inside the same SMB share only.";
        return ERROR_NOT_SUPPORTED;
    }
    Profile config;
    if(!LoadProfile(share,config)){
        lastDiagnostic=L"Authorized POSIX SSH profile is disabled or invalid for this share.";
        return ERROR_NOT_SUPPORTED;
    }
    const auto source=RelativeToShare(sourceUnc,share);
    const auto link=RelativeToShare(linkUnc,share);
    if(source.empty()||link.empty()){
        lastDiagnostic=L"POSIX source and link must be below the share root.";
        return ERROR_INVALID_NAME;
    }
    const auto shareName=share.substr(share.find_last_of(L'\\')+1);
    std::string payload="TCS-POSIX-1\n";
    for(const auto& field:{shareName,source,link}){
        std::string encoded;
        if(!ToUtf8(field,encoded))return ERROR_NO_UNICODE_TRANSLATION;
        payload+=B64(encoded)+"\n";
    }
    if(payload.size()>16384)return ERROR_INVALID_DATA;
    std::string answer;
    const DWORD result=RunSsh(config,payload,answer);
    if(result==ERROR_SUCCESS){
        lastSuccess=true;
        lastDiagnostic=L"POSIX directory symlink created and verified via authorized SSH helper.";
    }
    return result;
}
} // namespace tcs::posix
