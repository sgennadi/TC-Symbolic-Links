#pragma once
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tcs::batch {
enum class Status {
    Ready, ExistingDestination, DuplicateName, SameSourceAndDestination,
    InvalidName, InvalidSource, SourceTypeMismatch
};
struct Source {
    std::wstring path;
    bool accessible = true;
    bool directory = false;
};
struct Row {
    std::wstring source;
    std::wstring destination;
    std::wstring name;
    bool directory = false;
    Status status = Status::Ready;
};
inline std::wstring Base(std::wstring_view path) {
    while (!path.empty() && (path.back()==L'\\' || path.back()==L'/'))
        path.remove_suffix(1);
    const auto last=path.find_last_of(L"\\/");
    return std::wstring(path.substr(last==std::wstring_view::npos?0:last+1));
}
inline std::wstring Fold(std::wstring_view text) {
    std::wstring output(text);
    for(auto& c:output)c=static_cast<wchar_t>(std::towlower(c));
    return output;
}
inline std::wstring MakeName(std::wstring_view filename, std::wstring_view pattern,
                              std::size_t index) {
    const std::wstring original(filename);
    const auto dot=filename.find_last_of(L'.');
    const bool hasExt=dot!=std::wstring_view::npos && dot>0 && dot+1<filename.size();
    const std::wstring stem=hasExt?std::wstring(filename.substr(0,dot)):original;
    const std::wstring ext=hasExt?std::wstring(filename.substr(dot+1)):L"";
    if(pattern.empty())return original;
    std::wstring out;
    for(std::size_t i=0;i<pattern.size();) {
        if(pattern[i]==L'{') {
            const auto end=pattern.find(L'}',i+1);
            if(end!=std::wstring_view::npos) {
                const auto token=pattern.substr(i,end-i+1);
                const std::wstring* replacement=nullptr;
                if(token==L"{Name}") replacement=&original;
                else if(token==L"{Stem}") replacement=&stem;
                else if(token==L"{Ext}") replacement=&ext;
                if(replacement) {out+=*replacement;i=end+1;continue;}
                if(token==L"{Index}") {out+=std::to_wstring(index+1);i=end+1;continue;}
            }
        }
        out+=pattern[i++];
    }
    return out;
}
inline bool ValidName(std::wstring_view name) {
    if(name.empty()||name.size()>240||name==L"."||name==L"..")return false;
    if(name.back()==L' '||name.back()==L'.')return false;
    for(wchar_t ch:name)
        if(ch<32||ch==127||std::wstring_view(L"<>:\"/\\|?*").find(ch)!=std::wstring_view::npos)
            return false;
    auto prefix=name.substr(0,name.find(L'.'));
    std::wstring upper(prefix);
    for(auto& ch:upper)ch=static_cast<wchar_t>(std::towupper(ch));
    if(upper==L"CON"||upper==L"PRN"||upper==L"AUX"||upper==L"NUL")
        return false;
    if(upper.size()==4 && upper[3]>=L'1'&&upper[3]<=L'9' &&
       (upper.substr(0,3)==L"COM"||upper.substr(0,3)==L"LPT"))
        return false;
    return true;
}
inline std::wstring Join(std::wstring_view parent,std::wstring_view name) {
    std::wstring value(parent);
    if(!value.empty() && value.back()!=L'\\' && value.back()!=L'/')value+=L'\\';
    value.append(name);
    return value;
}
inline std::vector<Row> Plan(const std::vector<Source>& sources,
                             std::wstring_view destinationFolder,
                             std::wstring_view namePattern,
                             int mode,
                             const std::function<bool(const std::wstring&)>& exists) {
    std::vector<Row> result;
    result.reserve(sources.size());
    std::set<std::wstring> names;
    for(std::size_t i=0;i<sources.size();++i) {
        const auto& src=sources[i];
        Row row;
        row.source=src.path;
        row.directory=src.directory;
        row.name=MakeName(Base(src.path),namePattern,i);
        row.destination=Join(destinationFolder,row.name);
        if(!src.accessible)row.status=Status::InvalidSource;
        else if((mode==1||mode==4)&&src.directory ||
                (mode==2||mode==3)&&!src.directory)
            row.status=Status::SourceTypeMismatch;
        else if(!ValidName(row.name))row.status=Status::InvalidName;
        else if(Fold(src.path)==Fold(row.destination))
            row.status=Status::SameSourceAndDestination;
        else if(!names.insert(Fold(row.name)).second)
            row.status=Status::DuplicateName;
        else if(exists && exists(row.destination))
            row.status=Status::ExistingDestination;
        result.push_back(std::move(row));
    }
    return result;
}
inline bool AllReady(const std::vector<Row>& rows) {
    return !rows.empty() && std::all_of(rows.begin(),rows.end(),
        [](const Row& row){return row.status==Status::Ready;});
}
inline const wchar_t* StatusName(Status status) {
    switch(status){
        case Status::Ready:return L"READY";
        case Status::ExistingDestination:return L"EXISTS";
        case Status::DuplicateName:return L"DUPLICATE";
        case Status::SameSourceAndDestination:return L"SELF";
        case Status::InvalidName:return L"INVALID NAME";
        case Status::InvalidSource:return L"UNAVAILABLE";
        case Status::SourceTypeMismatch:return L"TYPE MISMATCH";
    }
    return L"UNKNOWN";
}
} // namespace tcs::batch
