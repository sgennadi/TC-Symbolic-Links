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
        const HWND empty=GetDlgItem(wnd,314);
        const HWND find=GetDlgItem(wnd,302);
        const HWND open=GetDlgItem(wnd,307);
        const HWND depth=GetDlgItem(wnd,303);
        bool depthChoicesVisible=false,depthCanChange=false,depthStored=false;
        int dropdownHeight=0,itemHeight=0,depthOptions=0;
        if(depth){
            depthOptions=static_cast<int>(SendMessageW(depth,CB_GETCOUNT,0,0));
            // The regression was a one-row COMBOBOX: it showed "3 levels"
            // but its drop list had no usable vertical space.
            SendMessageW(depth,CB_SHOWDROPDOWN,TRUE,0);
            COMBOBOXINFO info{sizeof(info)};
            RECT listRect{};
            const bool popup=SendMessageW(depth,CB_GETDROPPEDSTATE,0,0)!=0;
            const bool hasList=GetComboBoxInfo(depth,&info)!=FALSE &&
                info.hwndList && GetWindowRect(info.hwndList,&listRect);
            dropdownHeight=hasList?listRect.bottom-listRect.top:0;
            itemHeight=static_cast<int>(SendMessageW(depth,CB_GETITEMHEIGHT,0,0));
            depthChoicesVisible=depthOptions==3 && popup && hasList &&
                itemHeight>0 && dropdownHeight>=itemHeight*3;
            SendMessageW(depth,CB_SHOWDROPDOWN,FALSE,0);
            constexpr wchar_t registryKey[]=L"Software\\TCSymbolicLinks\\LinkManager";
            DWORD previous=3,previousBytes=sizeof(previous);
            const bool existed=RegGetValueW(HKEY_CURRENT_USER,registryKey,
                L"ScanDepth",RRF_RT_REG_DWORD,nullptr,&previous,
                &previousBytes)==ERROR_SUCCESS;
            // Simulate selecting the last then first entry via the native
            // WM_COMMAND notification that real mouse/keyboard input sends.
            const bool last=SendMessageW(depth,CB_SETCURSEL,2,0)==2;
            SendMessageW(wnd,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(depth));
            DWORD saved=0,savedSize=sizeof(saved);
            const bool storesSix=RegGetValueW(HKEY_CURRENT_USER,registryKey,
                L"ScanDepth",RRF_RT_REG_DWORD,nullptr,&saved,
                &savedSize)==ERROR_SUCCESS&&saved==6;
            const bool first=SendMessageW(depth,CB_SETCURSEL,0,0)==0;
            SendMessageW(wnd,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(depth));
            savedSize=sizeof(saved);
            depthStored=storesSix&&RegGetValueW(HKEY_CURRENT_USER,
                registryKey,L"ScanDepth",RRF_RT_REG_DWORD,nullptr,&saved,
                &savedSize)==ERROR_SUCCESS&&saved==1;
            wchar_t statusText[256]{};
            if(status)GetWindowTextW(status,statusText,256);
            depthCanChange=last&&first&&
                std::wcsstr(statusText,L"1")!=nullptr;
            // Regression tests must not modify an operator's saved choices.
            if(existed){
                HKEY key=nullptr;
                if(RegOpenKeyExW(HKEY_CURRENT_USER,registryKey,0,
                    KEY_SET_VALUE,&key)==ERROR_SUCCESS){
                    RegSetValueExW(key,L"ScanDepth",0,REG_DWORD,
                        reinterpret_cast<const BYTE*>(&previous),sizeof(previous));
                    RegCloseKey(key);
                }
            }else{
                RegDeleteKeyValueW(HKEY_CURRENT_USER,registryKey,L"ScanDepth");
            }
        }
        const bool buttonsStyled=find&&open &&
            (GetWindowLongPtrW(find,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW &&
            (GetWindowLongPtrW(open,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW;
        const bool emptyVisible=empty&&IsWindowVisible(empty);
        wchar_t title[256]{};
        GetWindowTextW(wnd,title,256);
        RECT area{};
        if(table)GetWindowRect(table,&area);
        const bool rootVisible=root&&IsWindowVisible(root);
        const bool tableVisible=table&&IsWindowVisible(table);
        const bool titleValid=std::wcsstr(title,L"v0.2.24")!=nullptr;
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
                 <<" ownerdraw_buttons="<<buttonsStyled
                 <<" empty_state="<<emptyVisible
                 <<" depth_options="<<depthOptions
                 <<" dropdown_height="<<dropdownHeight
                 <<" item_height="<<itemHeight
                 <<" popup_visible="<<depthChoicesVisible
                 <<" depth_changed="<<depthCanChange
                 <<" depth_saved="<<depthStored
                 <<" status_text_length="<<textLength<<"\\n";
        ok=root&&table&&status&&close && rootVisible&&tableVisible &&
           emptyVisible&&buttonsStyled&&depthChoicesVisible&&
           depthCanChange&&depthStored &&
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
