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
        ok=root&&table&&status&&close &&
           IsWindowVisible(root)&&IsWindowVisible(table)&&
           area.right-area.left>200 && area.bottom-area.top>90 &&
           std::wcsstr(title,L"v0.2.20")!=nullptr &&
           GetWindowTextLengthW(status)>0;
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
