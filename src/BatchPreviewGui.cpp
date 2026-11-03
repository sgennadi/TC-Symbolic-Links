#include "BatchPreviewGui.h"
#include "TcUiGraphics.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>

namespace tcs::batch {
namespace {
constexpr wchar_t kClass[]=L"TCSBatchPreview020";
constexpr int ID_TABLE=611,ID_GO=612,ID_CLOSE=613,ID_STATUS=614;
struct Screen {
    HWND window=nullptr,list=nullptr,proceed=nullptr,close=nullptr,status=nullptr;
    HFONT font=nullptr;
    bool running=true,accepted=false,creation=false,ru=false,he=false;
    UINT dpi=96;
    const std::vector<Row>* rows=nullptr;
    const wchar_t* L(const wchar_t* en,const wchar_t* ruText,
                     const wchar_t* heText) const {
        return he?heText:(ru?ruText:en);
    }
};
int Dp(const Screen& s,int value) {
    return MulDiv(value,static_cast<int>(s.dpi),96);
}
void Layout(Screen& s) {
    RECT client{};GetClientRect(s.window,&client);
    const int w=client.right-client.left,h=client.bottom-client.top;
    const int m=Dp(s,15),g=Dp(s,11),button=Dp(s,40);
    const int tableTop=Dp(s,94);
    const int buttonsY=h-m-button;
    const int statusY=buttonsY-Dp(s,44);
    const int tableHeight=(std::max)(Dp(s,65),statusY-tableTop-Dp(s,12));
    MoveWindow(s.list,m,tableTop,(std::max)(1,w-2*m),tableHeight,TRUE);
    if(s.list){
        int widths[]={w*32/100,w*35/100,Dp(s,120),Dp(s,120)};
        for(int i=0;i<4;++i)ListView_SetColumnWidth(s.list,i,widths[i]);
    }
    MoveWindow(s.status,m+Dp(s,14),statusY+Dp(s,7),
        w-2*m-Dp(s,28),Dp(s,28),TRUE);
    MoveWindow(s.close,w-m-Dp(s,134),buttonsY,Dp(s,134),button,TRUE);
    MoveWindow(s.proceed,w-m-Dp(s,280),buttonsY,Dp(s,134),button,TRUE);
    InvalidateRect(s.window,nullptr,FALSE);
}
void PaintPreview(Screen& s) {
    PAINTSTRUCT ps{};
    HDC screen=BeginPaint(s.window,&ps);
    if(!screen)return;
    RECT rect{};GetClientRect(s.window,&rect);
    const int w=rect.right,h=rect.bottom;
    HDC mem=CreateCompatibleDC(screen);
    HBITMAP map=mem?CreateCompatibleBitmap(screen,w,h):nullptr;
    HGDIOBJ prior=mem&&map?SelectObject(mem,map):nullptr;
    HDC dc=mem&&map?mem:screen;
    HBRUSH bg=CreateSolidBrush(tcs::visual::Canvas);
    FillRect(dc,&rect,bg);
    DeleteObject(bg);
    const int m=Dp(s,15);
    const HICON icon=reinterpret_cast<HICON>(
        SendMessageW(s.window,WM_GETICON,ICON_SMALL,0));
    const RECT banner{m,Dp(s,12),w-m,Dp(s,81)};
    tcs::ui::Header(dc,banner,s.dpi,icon,
        L"TC Symbolic Links",
        s.L(L"SMART BATCH PREVIEW  /  Check before creating",
            L"ПРЕДПРОСМОТР  /  Проверка перед созданием",
            L"תצוגה מקדימה  /  בדיקה לפני יצירה"),
        L"v0.2.24");
    const int y=h-m-Dp(s,40)-Dp(s,45);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    tcs::visual::Card(g,Gdiplus::RectF(
        static_cast<float>(m)+.5f,static_cast<float>(y)+.5f,
        static_cast<float>(w-2*m)-1.f,static_cast<float>(Dp(s,37))-1.f),
        static_cast<float>(Dp(s,10)),
        tcs::visual::White,tcs::visual::Stroke);
    if(dc==mem)BitBlt(screen,0,0,w,h,dc,0,0,SRCCOPY);
    if(prior)SelectObject(mem,prior);
    if(map)DeleteObject(map);
    if(mem)DeleteDC(mem);
    EndPaint(s.window,&ps);
}
void ApplyFont(Screen& s) {
    HFONT next=CreateFontW(-MulDiv(9,int(s.dpi),72),0,0,0,
        FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    if(!next)return;
    for(HWND control:{s.list,s.status,s.proceed,s.close})
        if(control)SendMessageW(control,WM_SETFONT,
            reinterpret_cast<WPARAM>(next),TRUE);
    if(s.font)DeleteObject(s.font);
    s.font=next;
}
void Populate(Screen& s){
    ListView_DeleteAllItems(s.list);
    for(std::size_t i=0;i<s.rows->size();++i){
        const auto& row=(*s.rows)[i];
        LVITEMW item{};
        item.iItem=static_cast<int>(i);
        item.mask=LVIF_TEXT;
        item.pszText=const_cast<LPWSTR>(row.source.c_str());
        const int index=ListView_InsertItem(s.list,&item);
        if(index<0)continue;
        auto set=[&](int col,const std::wstring& value){
            ListView_SetItemText(s.list,index,col,const_cast<LPWSTR>(value.c_str()));
        };
        set(1,row.destination);
        set(2,row.directory?s.L(L"Directory",L"Каталог",L"תיקייה"):
            s.L(L"File",L"Файл",L"קובץ"));
        set(3,StatusName(row.status));
    }
    std::size_t ready=0;
    for(const auto& row:*s.rows)
        if(row.status==Status::Ready)++ready;
    const auto text=std::to_wstring(ready)+L"/"+
        std::to_wstring(s.rows->size())+
        s.L(L" ready. Existing items are NEVER replaced.",
            L" готово. Существующие объекты НИКОГДА не заменяются.",
            L" מוכנים. פריטים קיימים לעולם אינם מוחלפים.");
    SetWindowTextW(s.status,text.c_str());
    EnableWindow(s.proceed,s.creation && AllReady(*s.rows));
}
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<Screen*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){
        auto* args=reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(args->lpCreateParams));
        // Keep the versioned caption from CreateWindowExW.
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    switch(msg){
        case WM_PAINT:
            if(state){PaintPreview(*state);return 0;}
            break;
        case WM_ERASEBKGND:return TRUE;
        case WM_DRAWITEM:
            if(state&&lp){
                const auto& item=*reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
                if(item.CtlID==ID_GO||item.CtlID==ID_CLOSE){
                    tcs::ui::PaintButton(item,state->dpi,
                        item.CtlID==ID_GO,
                        item.CtlID==ID_GO?tcs::ui::Glyph::Check:
                        tcs::ui::Glyph::Close);
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORSTATIC:
            if(state){
                static HBRUSH white=CreateSolidBrush(tcs::visual::White);
                SetTextColor(reinterpret_cast<HDC>(wp),tcs::visual::Muted);
                SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);
                return reinterpret_cast<LRESULT>(white);
            }
            break;
        case WM_CREATE:if(state){
            state->window=hwnd;
            state->dpi=GetDpiForWindow(hwnd);
            state->list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
                WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP,
                0,0,0,0,hwnd,reinterpret_cast<HMENU>(ID_TABLE),
                GetModuleHandleW(nullptr),nullptr);
            ListView_SetExtendedListViewStyle(state->list,
                LVS_EX_DOUBLEBUFFER|LVS_EX_FULLROWSELECT);
            ListView_SetBkColor(state->list,tcs::visual::White);
            ListView_SetTextBkColor(state->list,tcs::visual::White);
            ListView_SetTextColor(state->list,tcs::visual::Ink);
            const wchar_t* en[]={L"Source",L"New link",L"Type",L"Status"};
            const wchar_t* ru[]={L"Источник",L"Новая ссылка",L"Тип",L"Статус"};
            const wchar_t* he[]={L"מקור",L"קישור חדש",L"סוג",L"מצב"};
            for(int i=0;i<4;++i){
                LVCOLUMNW col{};
                col.mask=LVCF_TEXT|LVCF_WIDTH;
                col.cx=Dp(*state,170);
                col.pszText=const_cast<LPWSTR>(
                    state->he?he[i]:state->ru?ru[i]:en[i]);
                ListView_InsertColumn(state->list,i,&col);
            }
            state->status=CreateWindowW(L"STATIC",L"",
                WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,0,0,0,0,hwnd,
                reinterpret_cast<HMENU>(ID_STATUS),nullptr,nullptr);
            state->proceed=CreateWindowW(L"BUTTON",state->L(
                L"Create all",L"Создать все",L"צור הכל"),
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,
                reinterpret_cast<HMENU>(ID_GO),nullptr,nullptr);
            state->close=CreateWindowW(L"BUTTON",state->L(
                L"Close",L"Закрыть",L"סגור"),
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,
                reinterpret_cast<HMENU>(ID_CLOSE),nullptr,nullptr);
            ApplyFont(*state);
            Populate(*state);
            Layout(*state);
            if(!state->creation)ShowWindow(state->proceed,SW_HIDE);
        }return 0;
        case WM_SIZE:if(state&&state->list)Layout(*state);return 0;
        case WM_GETMINMAXINFO:if(state){
            auto* out=reinterpret_cast<MINMAXINFO*>(lp);
            RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
            out->ptMinTrackSize.x=(std::min)(Dp(*state,580),int(work.right-work.left)-20);
            out->ptMinTrackSize.y=(std::min)(Dp(*state,390),int(work.bottom-work.top)-20);
        }return 0;
        case WM_DPICHANGED:if(state){
            state->dpi=HIWORD(wp);
            const auto* r=reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,
                r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
            ApplyFont(*state);
            Layout(*state);
        }return 0;
        case WM_COMMAND:if(state&&HIWORD(wp)==BN_CLICKED){
            if(LOWORD(wp)==ID_GO){
                if(state->creation&&AllReady(*state->rows))
                    state->accepted=true;
                DestroyWindow(hwnd);
                return 0;
            }
            if(LOWORD(wp)==ID_CLOSE){DestroyWindow(hwnd);return 0;}
        }break;
        case WM_CLOSE:DestroyWindow(hwnd);return 0;
        case WM_DESTROY:if(state)state->running=false;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace
bool ConfirmPreview(HWND owner,const std::vector<Row>& rows,bool forCreation){
    tcs::ui::GdiSession graphics;
    Screen s;
    s.rows=&rows;s.creation=forCreation;
    const auto lang=GetUserDefaultUILanguage();
    s.ru=PRIMARYLANGID(lang)==LANG_RUSSIAN;
    s.he=PRIMARYLANGID(lang)==LANG_HEBREW;
    s.dpi=owner?GetDpiForWindow(owner):GetDpiForSystem();
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ConfirmPreview),&module))return false;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=Proc;
    wc.hInstance=module;
    wc.lpszClassName=kClass;
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=LoadIconW(module,MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int w=(std::min)(980,int(work.right-work.left)-24);
    const int h=(std::min)(560,int(work.bottom-work.top)-24);
    const int x=work.left+(int(work.right-work.left)-w)/2;
    const int y=work.top+(int(work.bottom-work.top)-h)/2;
    s.window=CreateWindowExW(WS_EX_DLGMODALFRAME,kClass,
        L"TC Symbolic Links v0.2.24 - Smart Batch Preview",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        x,y,w,h,owner,nullptr,module,&s);
    if(!s.window)return false;
    const COLORREF navy=RGB(11,46,90),white=RGB(255,255,255);
    DwmSetWindowAttribute(s.window,35,&navy,sizeof(navy));
    DwmSetWindowAttribute(s.window,36,&white,sizeof(white));
    ShowWindow(s.window,SW_SHOW);
    UpdateWindow(s.window);
    if(owner)EnableWindow(owner,FALSE);
    MSG msg{};bool quit=false;
    while(s.running){
        const int rc=GetMessageW(&msg,nullptr,0,0);
        if(rc<=0){quit=rc==0;break;}
        if(!IsDialogMessageW(s.window,&msg)){
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }
    if(owner&&IsWindow(owner)){EnableWindow(owner,TRUE);SetForegroundWindow(owner);}
    if(s.font)DeleteObject(s.font);
    if(quit)PostQuitMessage(int(msg.wParam));
    return s.accepted;
}
} // namespace tcs::batch
