#include "LinkBatchPlan.h"
#include <cassert>
#include <string>
#include <vector>
int main(){
    using namespace tcs::batch;
    assert(MakeName(L"file.txt",L"{Index}-{Stem}-copy.{Ext}",0)==L"1-file-copy.txt");
    assert(MakeName(L"Folder",L"{Stem}_link",1)==L"Folder_link");
    assert(MakeName(L"file.txt",L"",0)==L"file.txt");
    assert(!ValidName(L".."));
    assert(!ValidName(L"CON.txt"));
    assert(!ValidName(L"lpt1"));
    assert(!ValidName(L"bad/filename"));
    assert(!ValidName(L"test."));
    assert(ValidName(L"פיילל-Папка.txt"));
    auto rows=Plan({{L"C:\\one\\a.txt",true,false},{L"C:\\two\\a.txt",true,false}},
                   L"C:\\links",L"{Name}",0,{});
    assert(rows.size()==2);
    assert(rows[0].status==Status::Ready);
    assert(rows[1].status==Status::DuplicateName);
    rows=Plan({{L"C:\\root\\file.txt",true,false}},L"C:\\root",L"",0,{});
    assert(rows[0].status==Status::SameSourceAndDestination);
    rows=Plan({{L"C:\\root\\file.txt",true,false}},L"C:\\links",L"",0,
        [](const std::wstring&){return true;});
    assert(rows[0].status==Status::ExistingDestination);
    rows=Plan({{L"C:\\root\\file.txt",true,false}},L"C:\\links",L"",2,{});
    assert(rows[0].status==Status::SourceTypeMismatch);
    rows=Plan({{L"C:\\root\\dir",true,true}},L"C:\\links",L"{Name}",2,{});
    assert(AllReady(rows));
    rows=Plan({{L"C:\\root\\file.txt",false,false}},L"C:\\links",L"{Name}",0,{});
    assert(rows[0].status==Status::InvalidSource);
    rows=Plan({{L"C:\\root\\file.txt",true,false}},L"C:\\links",L"bad*.txt",0,{});
    assert(rows[0].status==Status::InvalidName);
    assert(!AllReady(rows));
}
