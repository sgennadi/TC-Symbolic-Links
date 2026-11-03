#include "TcUiPolish.h"
#include <cassert>
int main(){
    using namespace tcs::ui;
    assert(Version(L"0.2.22")==L"v0.2.22");
    assert(Version(L"v0.2.22")==L"v0.2.22");
    assert(UpdateButton(true,false,L"v0.2.22",Lang::English)==L"Update to v0.2.22");
    assert(UpdateButton(true,true,L"v0.2.22",Lang::English)==L"Update to v0.2.22");
    assert(UpdateButton(false,true,L"v0.2.22",Lang::English)==L"Reinstall");
    assert(UpdateButton(false,false,L"",Lang::English)==L"Check for updates");
    assert(UpdateButton(true,false,L"v0.2.22",Lang::Russian)==L"Обновить до v0.2.22");
    assert(UpdateButton(false,true,L"",Lang::Hebrew)==L"התקן מחדש");
    assert(UpdateDescription(true,false,Lang::English).find(L"SHA-256")!=std::wstring::npos);
    assert(EmptyLinksTitle(Lang::English,true)==L"No links found");
    assert(EmptyLinksTitle(Lang::Russian,false)==L"Проверьте свои ссылки");
    assert(EmptyLinksHelp(Lang::English,true).find(L"No files were changed")!=std::wstring::npos);
    const auto normal=UpdateWindowSize(1920,1080);
    assert(normal.width==670 && normal.height==354);
    const auto small=UpdateWindowSize(530,300);
    assert(small.width==506 && small.height==276);
    return 0;
}
