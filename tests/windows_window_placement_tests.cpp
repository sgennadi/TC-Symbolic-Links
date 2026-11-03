// The registry test uses its own per-process key and removes it before exit.
#include "TcMakerWindowState.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdlib>

namespace {
void Require(bool result,const char* message) {
    if(!result)throw std::runtime_error(message);
}
int Width(const RECT& r){return r.right-r.left;}
int Height(const RECT& r){return r.bottom-r.top;}
}
int main(){
    const auto key=std::wstring(L"Software\\TCSymbolicLinks\\Tests\\WindowPlacement-")+
        std::to_wstring(GetCurrentProcessId());
    HWND window=nullptr;
    try{
        using namespace tcs::maker::window;
        const RECT work{0,0,1920,1080};
        const RECT original{230,170,1370,890};
        const RECT fitted=FitRect(original,work,96,96);
        Require(fitted.left==230 && fitted.top==170 &&
                Width(fitted)==1140 && Height(fitted)==720,
                "Normal saved placement must restore exactly");
        const RECT outside{4000,3000,5140,3720};
        const RECT centered=FitRect(outside,work,96,96);
        Require(centered.left>=work.left && centered.right<=work.right &&
                centered.top>=work.top && centered.bottom<=work.bottom,
                "Missing monitor must reposition to the visible work area");
        const RECT smallScreen{0,0,820,510};
        const RECT compact=FitRect(original,smallScreen,96,96);
        Require(compact.left>=0 && compact.top>=0 &&
                compact.right<=smallScreen.right &&
                compact.bottom<=smallScreen.bottom,
                "Small display must keep entire Maker window on screen");
        const RECT negativeMonitor{-1920,0,0,1080};
        const RECT onLeft{-1760,120,-660,800};
        const RECT restoredLeft=FitRect(onLeft,negativeMonitor,96,96);
        Require(restoredLeft.left<0 && restoredLeft.right<=0,
                "Negative monitor coordinates must be preserved");
        const RECT dpiWork{0,0,2560,1440};
        const RECT scaled=FitRect(original,dpiWork,96,144);
        Require(Width(scaled)==1710 && Height(scaled)==1080,
                "Geometry must scale to changed monitor DPI");
        SavedPlacement corrupted{};
        corrupted.magic=0;
        Require(!Valid(corrupted),"Incorrect saved record must be rejected");
        const HINSTANCE app=GetModuleHandleW(nullptr);
        window=CreateWindowExW(0,L"STATIC",L"Maker placement regression",
            WS_OVERLAPPEDWINDOW,230,170,1140,720,nullptr,nullptr,app,nullptr);
        Require(window!=nullptr,"Cannot create regression test window");
        ShowWindow(window,SW_SHOWNORMAL);
        SetWindowPos(window,nullptr,245,188,1100,660,
            SWP_NOZORDER|SWP_NOACTIVATE);
        SavedPlacement saved{};
        Require(Capture(window,96,saved),"Cannot capture normal window");
        Require(Save(saved,key.c_str()),"Cannot persist window geometry");
        SavedPlacement loaded{};
        Require(Load(loaded,key.c_str()),"Cannot load saved geometry");
        Require(loaded.magic==kMagic &&
                loaded.placement.rcNormalPosition.left==
                    saved.placement.rcNormalPosition.left &&
                loaded.placement.rcNormalPosition.right==
                    saved.placement.rcNormalPosition.right,
                "Registry round-trip changed window position");
        SetWindowPos(window,nullptr,10,10,500,350,
            SWP_NOZORDER|SWP_NOACTIVATE);
        bool maximized=true;
        Require(Restore(window,loaded,&maximized),
                "Cannot restore saved geometry");
        Require(!maximized,"Normal restored window must not maximize");
        RECT actual{};GetWindowRect(window,&actual);
        Require(Width(actual)>=900&&Height(actual)>=580,
                "Saved dimensions were not restored");
        Require(SaveWindow(window,96,key.c_str()),
                "WM_EXITSIZEMOVE save path must remain valid");
        Require(Load(loaded,key.c_str()),"Second load failed");
        DestroyWindow(window);
        window=nullptr;
        RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
        std::cout<<"PASS: Maker window restores last size and position, DPI, "
                    "disconnected monitors and HKCU registry state\n";
        return 0;
    }catch(const std::exception& e){
        if(window)DestroyWindow(window);
        RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
