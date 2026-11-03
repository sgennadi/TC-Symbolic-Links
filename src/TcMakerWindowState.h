#pragma once
// Native Win32 Maker window placement. Saved per-user, never written to TC's INI.
// No background service, startup process, credentials, network or Samba access.
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace tcs::maker::window {
constexpr wchar_t kSettingsKey[]=L"Software\\TCSymbolicLinks\\Maker";
constexpr wchar_t kValue[]=L"WindowPlacement";
constexpr DWORD kMagic=0x53574354; // "TCWS"
constexpr DWORD kFormatVersion=1;
struct SavedPlacement {
    DWORD magic=kMagic;
    DWORD version=kFormatVersion;
    DWORD dpi=96;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
};
static_assert(sizeof(RECT)==16,"Registry geometry must be architecture-independent.");

inline bool ValidRect(const RECT& rect) noexcept {
    const long long width=static_cast<long long>(rect.right)-rect.left;
    const long long height=static_cast<long long>(rect.bottom)-rect.top;
    return width>=450 && width<=20000 && height>=300 && height<=14000;
}
inline bool Valid(const SavedPlacement& value) noexcept {
    return value.magic==kMagic && value.version==kFormatVersion &&
        value.dpi>=72 && value.dpi<=960 &&
        value.placement.length==sizeof(WINDOWPLACEMENT) &&
        ValidRect(value.placement.rcNormalPosition);
}
inline bool Load(SavedPlacement& value,
                 const wchar_t* subKey=kSettingsKey,
                 HKEY hive=HKEY_CURRENT_USER) noexcept {
    SavedPlacement found{};
    DWORD bytes=sizeof(found);
    if(RegGetValueW(hive,subKey,kValue,RRF_RT_REG_BINARY,
                    nullptr,&found,&bytes)!=ERROR_SUCCESS ||
       bytes!=sizeof(found) || !Valid(found))
        return false;
    value=found;
    return true;
}
inline bool Save(const SavedPlacement& value,
                 const wchar_t* subKey=kSettingsKey,
                 HKEY hive=HKEY_CURRENT_USER) noexcept {
    if(!Valid(value))return false;
    HKEY key=nullptr;
    if(RegCreateKeyExW(hive,subKey,0,nullptr,REG_OPTION_NON_VOLATILE,
                       KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        return false;
    const LONG status=RegSetValueExW(key,kValue,0,REG_BINARY,
        reinterpret_cast<const BYTE*>(&value),sizeof(value));
    RegCloseKey(key);
    return status==ERROR_SUCCESS;
}
inline bool Capture(HWND hwnd,UINT dpi,SavedPlacement& value) noexcept {
    if(!IsWindow(hwnd))return false;
    WINDOWPLACEMENT placement{};
    placement.length=sizeof(placement);
    if(!GetWindowPlacement(hwnd,&placement))return false;
    SavedPlacement candidate{};
    candidate.dpi=(std::max)(72u,(std::min)(dpi,960u));
    candidate.placement=placement;
    // A minimized dialog must reopen normally. A maximized dialog retains
    // its previous normal-size geometry for future restoration.
    candidate.placement.showCmd=IsZoomed(hwnd)?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;
    if(!Valid(candidate))return false;
    value=candidate;
    return true;
}
inline bool SaveWindow(HWND hwnd,UINT dpi,
                       const wchar_t* subKey=kSettingsKey) noexcept {
    SavedPlacement value;
    return Capture(hwnd,dpi,value) && Save(value,subKey);
}
// Pure geometry used by production restore and regression tests.
// A removed monitor, taskbar or new DPI must never place the dialog offscreen.
inline RECT FitRect(const RECT& desired,const RECT& work,
                    UINT savedDpi,UINT currentDpi) noexcept {
    const int workW=(std::max)(320L,work.right-work.left);
    const int workH=(std::max)(240L,work.bottom-work.top);
    const int scaleFrom=static_cast<int>(savedDpi>=72&&savedDpi<=960?savedDpi:96);
    const int scaleTo=static_cast<int>(currentDpi>=72&&currentDpi<=960?currentDpi:96);
    const int maxW=(std::max)(320,workW-20);
    const int maxH=(std::max)(240,workH-20);
    const int minW=(std::min)(maxW,MulDiv(940,scaleTo,96));
    const int minH=(std::min)(maxH,MulDiv(610,scaleTo,96));
    const long long oldW=static_cast<long long>(desired.right)-desired.left;
    const long long oldH=static_cast<long long>(desired.bottom)-desired.top;
    const int requestedW=static_cast<int>((std::min)(oldW,20000LL));
    const int requestedH=static_cast<int>((std::min)(oldH,14000LL));
    const int w=(std::clamp)(MulDiv(requestedW,scaleTo,scaleFrom),minW,maxW);
    const int h=(std::clamp)(MulDiv(requestedH,scaleTo,scaleFrom),minH,maxH);
    RECT visible{};
    const bool intersect=IntersectRect(&visible,&desired,&work)!=FALSE &&
        (visible.right-visible.left)>=100 &&
        (visible.bottom-visible.top)>=80;
    const int x=intersect?(std::clamp)(static_cast<int>(desired.left),
        static_cast<int>(work.left),static_cast<int>(work.right)-w):
        static_cast<int>(work.left)+(workW-w)/2;
    const int y=intersect?(std::clamp)(static_cast<int>(desired.top),
        static_cast<int>(work.top),static_cast<int>(work.bottom)-h):
        static_cast<int>(work.top)+(workH-h)/2;
    return {x,y,x+w,y+h};
}
inline bool Restore(HWND hwnd,const SavedPlacement& stored,
                    bool* wasMaximized=nullptr) noexcept {
    if(!IsWindow(hwnd) || !Valid(stored))return false;
    MONITORINFO monitor{sizeof(monitor)};
    HMONITOR nearest=MonitorFromRect(&stored.placement.rcNormalPosition,
                                    MONITOR_DEFAULTTONEAREST);
    if(!nearest||!GetMonitorInfoW(nearest,&monitor))return false;
    WINDOWPLACEMENT placement=stored.placement;
    placement.length=sizeof(placement);
    placement.flags=0;
    const UINT dpi=GetDpiForWindow(hwnd);
    placement.rcNormalPosition=FitRect(stored.placement.rcNormalPosition,
                                      monitor.rcWork,stored.dpi,dpi);
    const bool maximized=stored.placement.showCmd==SW_SHOWMAXIMIZED;
    // SetWindowPlacement only affects the Maker window; original TC settings
    // and application-wide WindowPlacement are never changed.
    placement.showCmd=maximized?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;
    if(!SetWindowPlacement(hwnd,&placement))return false;
    if(wasMaximized)*wasMaximized=maximized;
    return true;
}
} // namespace tcs::maker::window
