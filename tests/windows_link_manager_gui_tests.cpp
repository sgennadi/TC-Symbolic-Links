// Native GUI acceptance for the all-white Link Manager regression.
// The real LinkManagerGui.cpp is linked, not a mocked dialog.
#include "LinkManagerGui.h"
#include <windows.h>
#include <commctrl.h>
#include <cwchar>
#include <iostream>
#include <thread>
int main() {
    INITCOMMONCONTROLSEX init{sizeof(init),
        ICC_WIN95_CLASSES|ICC_LISTVIEW_CLASSES};
    if(!InitCommonControlsEx(&init))return 2;
    std::thread worker([] {
        tcs::manager::ShowLinkManager(nullptr,L"",{});
    });
    HWND wnd=nullptr;
    for(int attempt=0;attempt<80;++attempt) {
        wnd=FindWindowW(L"TCSLinkManager020",nullptr);
        if(wnd&&IsWindowVisible(wnd))break;
        Sleep(50);
    }
    bool ok=false;
    if(wnd) {
        const HWND root=GetDlgItem(wnd,301);
        const HWND table=GetDlgItem(wnd,304);
        const HWND status=GetDlgItem(wnd,305);
        const HWND close=GetDlgItem(wnd,311);
        wchar_t title[256]{};
        GetWindowTextW(wnd,title,256);
        RECT area{};
        if(table)GetWindowRect(table,&area);
        const bool rootVisible=root&&IsWindowVisible(root);
        const bool tableVisible=table&&IsWindowVisible(table);
        const bool titleValid=std::wcsstr(title,L"v0.2.21")!=nullptr;
        std::wcerr<<L"GUI_TITLE_TEXT='"<<title<<L"'\n";
        const int textLength=status?GetWindowTextLengthW(status):-1;
        std::cerr<<"GUI_DIAGNOSTIC window="<<(wnd?1:0)
                 <<" root="<<(root?1:0)
                 <<" table="<<(table?1:0)
                 <<" status="<<(status?1:0)
                 <<" close="<<(close?1:0)
                 <<" root_visible="<<rootVisible
                 <<" table_visible="<<tableVisible
                 <<" table_size="<<(area.right-area.left)<<"x"
                 <<(area.bottom-area.top)
                 <<" title_version="<<titleValid
                 <<" status_text_length="<<textLength<<"\\n";
        ok=root&&table&&status&&close && rootVisible&&tableVisible &&
           area.right-area.left>200 && area.bottom-area.top>90 &&
           titleValid && textLength>0;
        PostMessageW(wnd,WM_CLOSE,0,0);
    }
    worker.join();
    if(!ok) {
        std::cerr<<"FAIL: Link Manager is missing visible native children, "
                    "title version or initial status\n";
        return 1;
    }
    std::cout<<"PASS: Link Manager native children, title version and status\n";
    return 0;
}
