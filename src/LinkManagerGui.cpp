// Link Manager: one native WDX window, no companion executable.
#include "LinkManagerGui.h"
#include "LinkManagerCore.h"
#include "LinkCore.h"
#include "TcUiGraphics.h"
#include "TcUiPolish.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace tcs::manager {
namespace {
constexpr wchar_t kClass[]=L"TCSLinkManager020";
constexpr UINT WM_TCS_MANAGER_INITIAL_SCAN=WM_APP+0x560;
enum {ID_ROOT=301,ID_FIND=302,ID_DEPTH=303,ID_TABLE=304,
      ID_STATUS=305,ID_NEW_TARGET=306,ID_OPEN=307,ID_COPY=308,
      ID_COPY_REL=309,ID_REPAIR=310,ID_CLOSE=311,ID_REFRESH=312,
      ID_INFO=313,ID_EMPTY=314};
struct Ui {
    HWND window=nullptr,owner=nullptr,root=nullptr,depth=nullptr,table=nullptr;
    HWND status=nullptr,target=nullptr,info=nullptr;
    HWND find=nullptr,refresh=nullptr,open=nullptr,copy=nullptr;
    HWND copyRelative=nullptr,repair=nullptr,close=nullptr,empty=nullptr;
    HFONT font=nullptr;
    UINT dpi=96;
    bool running=true,ru=false,he=false,searched=false,compact=false;
    std::vector<Entry> items;
    const wchar_t* L(const wchar_t* en,const wchar_t* ruText,
                     const wchar_t* heText)const{
        return he?heText:(ru?ruText:en);
    }
};
int Dp(const Ui& ui,int n){return MulDiv(n,static_cast<int>(ui.dpi),96);}
std::wstring Text(HWND control){
    if(!control)return {};
    const int len=GetWindowTextLengthW(control);
    if(len<0||len>32766)return {};
    std::wstring value(static_cast<std::size_t>(len+1),L'\0');
    const int n=GetWindowTextW(control,value.data(),len+1);
    value.resize((std::max)(n,0));
    return value;
}
void SetStatus(Ui& u,const std::wstring& msg){
    if(u.status)SetWindowTextW(u.status,msg.c_str());
}
HWND Ctrl(HWND parent,const wchar_t* name,const wchar_t* title,DWORD style,int id){
    return CreateWindowExW(0,name,title,style|WS_CHILD|WS_VISIBLE,
        0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE)),
        nullptr);
}
int Selected(const Ui& u){
    if(!u.table)return -1;
    return ListView_GetNextItem(u.table,-1,LVNI_SELECTED);
}
void Details(Ui& u){
    const int index=Selected(u);
    const bool chosen=index>=0&&static_cast<std::size_t>(index)<u.items.size();
    for(HWND button:{u.open,u.copy,u.copyRelative,u.repair})
        if(button)EnableWindow(button,chosen);
    if(!chosen){
        if(u.info)SetWindowTextW(u.info,u.L(
            L"Select a link to inspect its exact target and health.",
            L"Выберите ссылку для просмотра цели и состояния.",
            L"בחר קישור כדי לצפות ביעד ובמצב."));
        if(u.repair)EnableWindow(u.repair,FALSE);
        return;
    }
    const auto& e=u.items[static_cast<std::size_t>(index)];
    const std::wstring value=std::wstring(KindLabel(e))+L"  |  "+
        StatusLabel(e.health)+L"  |  "+e.path+L"  ->  "+
        (e.target.empty()?L"(no target)":e.target);
    if(u.info)SetWindowTextW(u.info,value.c_str());
    if(u.repair)EnableWindow(u.repair,
        e.health==Health::Broken && !IsRemoteSmbPath(e.path) &&
        (e.kind==ReparseKind::SymbolicFile ||
         e.kind==ReparseKind::SymbolicDirectory));
}
void Populate(Ui& u,const Scan& scan){
    u.items=scan.items;
    ListView_DeleteAllItems(u.table);
    for(std::size_t i=0;i<u.items.size();++i){
        const Entry& e=u.items[i];
        LVITEMW item{};
        item.mask=LVIF_TEXT;
        item.iItem=static_cast<int>(i);
        item.pszText=const_cast<LPWSTR>(e.path.c_str());
        const int row=ListView_InsertItem(u.table,&item);
        if(row<0)continue;
        auto set=[&](int column,const std::wstring& value){
            ListView_SetItemText(u.table,row,column,const_cast<LPWSTR>(value.c_str()));
        };
        set(1,KindLabel(e));
        set(2,StatusLabel(e.health));
        set(3,e.target);
        set(4,e.hardlinks>1?std::to_wstring(e.hardlinks):L"");
    }
    std::wstring msg=std::to_wstring(u.items.size())+u.L(
        L" links; examined ",L" ссылок; проверено ",L" קישורים; נבדקו ");
    msg+=std::to_wstring(scan.visited);
    if(scan.truncated)msg+=u.L(
        L" (limit reached)",L" (достигнут лимит)",L" (הגעת למגבלה)");
    if(scan.firstError!=ERROR_SUCCESS && scan.firstError!=ERROR_FILE_NOT_FOUND)
        msg+=L" | Win32 "+std::to_wstring(scan.firstError);
    SetStatus(u,msg);
    if(u.empty) {
        ShowWindow(u.empty,u.items.empty()?SW_SHOW:SW_HIDE);
        InvalidateRect(u.empty,nullptr,FALSE);
    }
    Details(u);
}
void ScanFolder(Ui& u){
    const auto path=Text(u.root);
    if(!tcs::IsExistingDirectory(path)){
        MessageBoxW(u.window,u.L(
            L"Choose a real filesystem directory (disk or SMB).",
            L"Выберите реальную папку на диске или SMB.",
            L"יש לבחור תיקייה אמיתית בדיסק או ב-SMB."),
            L"TC Symbolic Links",MB_OK|MB_ICONWARNING);
        return;
    }
    const int depthIndex=static_cast<int>(SendMessageW(u.depth,CB_GETCURSEL,0,0));
    const unsigned depth=depthIndex==0?1u:depthIndex==1?3u:6u;
    SetStatus(u,u.L(L"Scanning (bounded, no directory link traversal)...",
        L"Поиск (с ограничением, без перехода по ссылкам на папки)...",
        L"סריקה מוגבלת, ללא מעבר בקישורים לתיקיות..."));
    SetCursor(LoadCursorW(nullptr,IDC_WAIT));
    // A bounded read-only scan. On SMB reduce budget to avoid long network
    // requests; no network settings, permissions or symlink policy are changed.
    const bool remote=IsRemoteSmbPath(path);
    const Scan report=FindLinks(path,depth,remote?700:5000,remote?400:2000);
    SetCursor(LoadCursorW(nullptr,IDC_ARROW));
    u.searched=true;
    Populate(u,report);
}
bool Clipboard(HWND owner,const std::wstring& value){
    if(value.empty()||value.size()>32760||!OpenClipboard(owner))return false;
    if(!EmptyClipboard()){CloseClipboard();return false;}
    const std::size_t size=(value.size()+1)*sizeof(wchar_t);
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!block){CloseClipboard();return false;}
    void* memory=GlobalLock(block);
    if(!memory){GlobalFree(block);CloseClipboard();return false;}
    memcpy(memory,value.c_str(),size);
    GlobalUnlock(block);
    if(!SetClipboardData(CF_UNICODETEXT,block)){
        GlobalFree(block);CloseClipboard();return false;
    }
    CloseClipboard();
    return true;
}
void Act(Ui& u,int id){
    const int index=Selected(u);
    if(index<0||static_cast<std::size_t>(index)>=u.items.size())return;
    const Entry e=u.items[static_cast<std::size_t>(index)];
    if(id==ID_REFRESH){
        Entry next=InspectOne(e.path);
        u.items[static_cast<std::size_t>(index)]=next;
        std::wstring s=StatusLabel(next.health);
        ListView_SetItemText(u.table,index,2,s.data());
        Details(u);
        return;
    }
    const std::wstring resolved=ResolvedTarget(e);
    if(id==ID_OPEN){
        if(resolved.empty()){SetStatus(u,L"This link has no target path.");return;}
        const auto n=reinterpret_cast<INT_PTR>(ShellExecuteW(u.window,L"open",
            resolved.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
        if(n<=32)SetStatus(u,L"Cannot open target (ShellExecute "+std::to_wstring(n)+L").");
        return;
    }
    if(id==ID_COPY||id==ID_COPY_REL){
        const std::wstring value=id==ID_COPY_REL?
            tcs::RelativeTarget(e.path,resolved):resolved;
        if(!Clipboard(u.window,value))SetStatus(u,L"Cannot copy target path.");
        else SetStatus(u,u.L(L"Target path copied.",L"Путь цели скопирован.",
                             L"נתיב היעד הועתק."));
        return;
    }
    if(id==ID_REPAIR){
        if(e.health!=Health::Broken||IsRemoteSmbPath(e.path)||
            (e.kind!=ReparseKind::SymbolicDirectory &&
             e.kind!=ReparseKind::SymbolicFile))return;
        const auto target=Text(u.target);
        if(target.empty()){SetStatus(u,L"Enter a new existing target first.");return;}
        const auto confirmation=std::wstring(u.L(
            L"Repair this BROKEN local symbolic link?\n",
            L"Исправить эту ПОВРЕЖДЁННУЮ локальную ссылку?\n",
            L"לתקן קישור מקומי שבור זה?\n"))+e.path+L"\n"+target;
        if(MessageBoxW(u.window,confirmation.c_str(),L"Repair broken link",
            MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
        const auto result=RepairBroken(e.path,target);
        std::wstring message=result.explanation;
        if(result.code!=ERROR_SUCCESS)
            message+=L" ("+std::to_wstring(result.code)+L")";
        if(!result.retainedBackup.empty())message+=L"\n"+result.retainedBackup;
        MessageBoxW(u.window,message.c_str(),L"TC Symbolic Links - Repair",
            MB_OK|(result.code==ERROR_SUCCESS?MB_ICONINFORMATION:MB_ICONWARNING));
        ScanFolder(u);
    }
}
void Layout(Ui& u){
    RECT rc{};GetClientRect(u.window,&rc);
    const int w=rc.right-rc.left,h=rc.bottom-rc.top;
    const int m=Dp(u,u.compact?10:14),g=Dp(u,8);
    const int line=Dp(u,u.compact?29:33),button=Dp(u,38);
    auto pos=[](HWND child,int x,int y,int width,int height){
        if(child)MoveWindow(child,x,y,(std::max)(1,width),
                            (std::max)(1,height),TRUE);
    };
    const int right=Dp(u,124);
    const int depthWidth=Dp(u,92);
    const int searchY=Dp(u,u.compact?74:97);
    pos(u.root,m,searchY,w-2*m-right-depthWidth-2*g,line);
    pos(u.depth,w-m-right-depthWidth-g,searchY,depthWidth,line);
    pos(u.find,w-m-right,searchY,right,line);
    const int tableY=searchY+line+g;
    const int tableBottom=h-m-button-Dp(u,u.compact?96:119);
    const int tableHeight=(std::max)(Dp(u,92),tableBottom-tableY);
    pos(u.table,m,tableY,w-2*m,tableHeight);
    const int contentsTop=tableY+Dp(u,27);
    pos(u.empty,m+Dp(u,5),contentsTop,w-2*m-Dp(u,10),
        (std::max)(Dp(u,45),tableHeight-Dp(u,34)));
    int colWidths[5]={(std::max)(Dp(u,175),w*33/100),
        Dp(u,139),Dp(u,106),(std::max)(Dp(u,165),w*26/100),Dp(u,73)};
    for(int i=0;i<5;++i)ListView_SetColumnWidth(u.table,i,colWidths[i]);
    pos(u.info,m+Dp(u,11),tableBottom+Dp(u,5),
        w-2*m-Dp(u,22),Dp(u,28));
    pos(u.target,m+Dp(u,11),tableBottom+Dp(u,38),
        w-2*m-Dp(u,22),Dp(u,30));
    pos(u.status,m+Dp(u,12),tableBottom+Dp(u,74),
        w-2*m-Dp(u,24),Dp(u,23));
    const int by=h-m-button;
    const int space=w-2*m-4*g;
    const int step=(std::max)(Dp(u,60),space/5);
    HWND controls[5]={u.open,u.copy,u.copyRelative,u.repair,u.close};
    for(int i=0;i<5;++i){
        const int x=m+i*(step+g);
        pos(controls[i],x,by,
            i==4?(std::max)(Dp(u,50),w-m-x):step,button);
    }
    InvalidateRect(u.window,nullptr,FALSE);
}
void PaintManager(Ui& u) {
    PAINTSTRUCT ps{};
    HDC screen=BeginPaint(u.window,&ps);
    if(!screen)return;
    RECT rc{};GetClientRect(u.window,&rc);
    const int w=rc.right,h=rc.bottom;
    HDC memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=memory?CreateCompatibleBitmap(screen,w,h):nullptr;
    HGDIOBJ previous=memory&&bitmap?SelectObject(memory,bitmap):nullptr;
    HDC dc=memory&&bitmap?memory:screen;
    HBRUSH background=CreateSolidBrush(tcs::visual::Canvas);
    FillRect(dc,&rc,background);DeleteObject(background);
    const int m=Dp(u,u.compact?10:14);
    const RECT banner{m,Dp(u,10),w-m,
                      Dp(u,u.compact?64:84)};
    const HICON icon=reinterpret_cast<HICON>(
        SendMessageW(u.window,WM_GETICON,ICON_SMALL,0));
    tcs::ui::Header(dc,banner,u.dpi,icon,
        L"TC Symbolic Links",
        u.L(L"LINK MANAGER  /  Find, inspect and repair",
            L"МЕНЕДЖЕР ССЫЛОК  /  Поиск и проверка",
            L"מנהל קישורים  /  חיפוש ובדיקה"),
        L"v0.2.22");
    RECT pos{};GetWindowRect(u.info,&pos);
    MapWindowPoints(nullptr,u.window,reinterpret_cast<POINT*>(&pos),2);
    const float top=static_cast<float>(pos.top-Dp(u,5));
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const int tableBottom=h-m-Dp(u,38)-Dp(u,u.compact?96:119);
    const float cardHeight=(std::max)(static_cast<float>(Dp(u,98)),
        static_cast<float>(h-m-Dp(u,46))-top);
    tcs::visual::Card(g,Gdiplus::RectF(
        static_cast<float>(m)+.5f,top,
        static_cast<float>(w-2*m)-1.f,cardHeight),
        static_cast<float>(Dp(u,11)),
        tcs::visual::White,tcs::visual::Stroke);
    if(dc==memory)BitBlt(screen,0,0,w,h,dc,0,0,SRCCOPY);
    if(previous)SelectObject(memory,previous);
    if(bitmap)DeleteObject(bitmap);
    if(memory)DeleteDC(memory);
    EndPaint(u.window,&ps);
}
void ApplyFont(Ui& u){
    HFONT next=CreateFontW(-MulDiv(9,static_cast<int>(u.dpi),72),0,0,0,FW_NORMAL,
        FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    if(!next)return;
    for(HWND control:{u.root,u.depth,u.find,u.table,u.info,u.target,u.status,
        u.open,u.copy,u.copyRelative,u.repair,u.close})
        if(control)SendMessageW(control,WM_SETFONT,
            reinterpret_cast<WPARAM>(next),TRUE);
    if(u.font)DeleteObject(u.font);
    u.font=next;
}
void Build(Ui& u){
    u.root=Ctrl(u.window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_ROOT);
    u.depth=Ctrl(u.window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_DEPTH);
    for(const wchar_t* item:{L"1 level",L"3 levels",L"6 levels"})
        SendMessageW(u.depth,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
    SendMessageW(u.depth,CB_SETCURSEL,1,0);
    u.find=Ctrl(u.window,L"BUTTON",u.L(L"Find links",L"Найти ссылки",
        L"חפש קישורים"),WS_TABSTOP|BS_OWNERDRAW,ID_FIND);
    u.table=Ctrl(u.window,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|
        LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,ID_TABLE);
    ListView_SetExtendedListViewStyle(u.table,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(u.table,tcs::visual::White);
    ListView_SetTextBkColor(u.table,tcs::visual::White);
    ListView_SetTextColor(u.table,tcs::visual::Ink);
    u.empty=Ctrl(u.window,L"STATIC",L"",SS_OWNERDRAW,ID_EMPTY);
    const wchar_t* names[]={L"Link path",L"Type",L"Health",L"Target",L"Hardlinks"};
    const wchar_t* russian[]={L"Путь ссылки",L"Тип",L"Состояние",L"Цель",L"Жёсткие"};
    const wchar_t* hebrew[]={L"נתיב קישור",L"סוג",L"מצב",L"יעד",L"קישורים"};
    for(int i=0;i<5;++i){
        LVCOLUMNW col{};
        col.mask=LVCF_TEXT|LVCF_WIDTH;
        col.pszText=const_cast<LPWSTR>(u.he?hebrew[i]:u.ru?russian[i]:names[i]);
        col.cx=Dp(u,130);
        ListView_InsertColumn(u.table,i,&col);
    }
    u.info=Ctrl(u.window,L"STATIC",L"",SS_ENDELLIPSIS,ID_INFO);
    u.target=Ctrl(u.window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,
        ID_NEW_TARGET);
    SendMessageW(u.target,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(
        u.L(L"New target for Repair Broken Links (existing local file/folder)",
            L"Новая цель Repair (существующий локальный файл/каталог)",
            L"יעד חדש לתיקון קישור מקומי שבור")));
    u.status=Ctrl(u.window,L"STATIC",L"",SS_ENDELLIPSIS,ID_STATUS);
    u.open=Ctrl(u.window,L"BUTTON",u.L(L"Open Target",L"Открыть цель",
        L"פתח יעד"),WS_TABSTOP|BS_OWNERDRAW,ID_OPEN);
    u.copy=Ctrl(u.window,L"BUTTON",u.L(L"Copy Target",L"Копировать путь",
        L"העתק יעד"),WS_TABSTOP|BS_OWNERDRAW,ID_COPY);
    u.copyRelative=Ctrl(u.window,L"BUTTON",u.L(L"Copy Relative",L"Относит. путь",
        L"העתק יחסי"),WS_TABSTOP|BS_OWNERDRAW,ID_COPY_REL);
    u.repair=Ctrl(u.window,L"BUTTON",u.L(L"Repair",L"Исправить",
        L"תקן"),WS_TABSTOP|BS_OWNERDRAW,ID_REPAIR);
    u.close=Ctrl(u.window,L"BUTTON",u.L(L"Close",L"Закрыть",
        L"סגור"),WS_TABSTOP|BS_OWNERDRAW,ID_CLOSE);
    ApplyFont(u);
    SetStatus(u,u.L(
        L"Choose a folder and Find links. Existing folders are scanned automatically.",
        L"Выберите папку и нажмите «Найти ссылки». Существующие папки сканируются автоматически.",
        L"בחר תיקייה וחפש קישורים. תיקיות קיימות נסרקות אוטומטית."));
    Details(u);
    if(u.empty)ShowWindow(u.empty,SW_SHOW);
}
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* u=reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){
        auto* args=reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(args->lpCreateParams));
        // Preserve default non-client initialization including the caption.
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    switch(msg){
        case WM_CREATE:if(u){
            // CreateWindowExW dispatches WM_CREATE BEFORE returning. At this
            // moment ShowLinkManager has NOT assigned u.window yet. All child
            // controls previously received a NULL parent and were invisible.
            u->window=hwnd;
            u->dpi=GetDpiForWindow(hwnd);
            Build(*u);
            Layout(*u);
        }return 0;
        case WM_SIZE:if(u&&u->table)Layout(*u);return 0;
        case WM_TCS_MANAGER_INITIAL_SCAN:
            if(u&&u->root&&u->table &&
               tcs::IsExistingDirectory(Text(u->root)))
                ScanFolder(*u);
            return 0;
        case WM_DPICHANGED:if(u){
            u->dpi=HIWORD(wp);
            const RECT* r=reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd,nullptr,r->left,r->top,
                r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);
            ApplyFont(*u);
            Layout(*u);
        }return 0;
        case WM_GETMINMAXINFO:if(u){
            auto* m=reinterpret_cast<MINMAXINFO*>(lp);
            RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
            m->ptMinTrackSize.x=(std::min)(Dp(*u,630),int(work.right-work.left)-20);
            m->ptMinTrackSize.y=(std::min)(Dp(*u,455),int(work.bottom-work.top)-20);
        }return 0;
        case WM_NOTIFY:if(u&&reinterpret_cast<NMHDR*>(lp) &&
            reinterpret_cast<NMHDR*>(lp)->idFrom==ID_TABLE &&
            reinterpret_cast<NMHDR*>(lp)->code==LVN_ITEMCHANGED)
            Details(*u);
            break;
        case WM_COMMAND:if(u&&HIWORD(wp)==BN_CLICKED){
            switch(LOWORD(wp)){
                case ID_FIND:ScanFolder(*u);return 0;
                case ID_OPEN:case ID_COPY:case ID_COPY_REL:
                case ID_REPAIR:case ID_REFRESH:Act(*u,LOWORD(wp));return 0;
                case ID_CLOSE:DestroyWindow(hwnd);return 0;
            }
        }break;
        case WM_CLOSE:DestroyWindow(hwnd);return 0;
        case WM_DESTROY:if(u)u->running=false;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

void ShowLinkManager(HWND owner,const std::wstring& initialFolder,
                     const std::vector<std::wstring>& selected){
    Ui u;
    u.owner=owner;
    if(owner&&IsWindow(owner)){
        if(HWND checkbox=GetDlgItem(owner,121))
            u.compact=SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED;
    }
    tcs::ui::GdiSession graphics;
    const LANGID lang=GetUserDefaultUILanguage();
    u.ru=PRIMARYLANGID(lang)==LANG_RUSSIAN;
    u.he=PRIMARYLANGID(lang)==LANG_HEBREW;
    u.dpi=owner?GetDpiForWindow(owner):GetDpiForSystem();
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ShowLinkManager),&self))return;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=Proc;
    wc.hInstance=self;
    wc.lpszClassName=kClass;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=static_cast<HICON>(LoadImageW(self,MAKEINTRESOURCEW(101),
        IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    wc.hIconSm=static_cast<HICON>(LoadImageW(self,MAKEINTRESOURCEW(101),
        IMAGE_ICON,16,16,LR_DEFAULTCOLOR));
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassExW(&wc) &&
       GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int maxWidth=(std::max)(320,int(work.right-work.left)-25);
    const int maxHeight=(std::max)(260,int(work.bottom-work.top)-25);
    const int width=(std::min)(960,maxWidth);
    const int height=(std::min)(600,maxHeight);
    const int left=work.left+(int(work.right-work.left)-width)/2;
    const int top=work.top+(int(work.bottom-work.top)-height)/2;
    u.window=CreateWindowExW(WS_EX_DLGMODALFRAME,kClass,
        L"TC Symbolic Links v0.2.21 - Link Manager",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        left,top,width,height,owner,nullptr,self,&u);
    if(!u.window)return;
    // The Win32 class may have been re-used by the host process. Set the
    // caption explicitly AFTER CreateWindowExW: the screenshot and GUI CI
    // showed an apparently empty title even when native children existed.
    const wchar_t* caption=u.he?
        L"TC Symbolic Links v0.2.21 - מנהל קישורים":
        (u.ru?L"TC Symbolic Links v0.2.21 - Менеджер ссылок":
              L"TC Symbolic Links v0.2.21 - Link Manager");
    SetWindowTextW(u.window,caption);
    const COLORREF captionColor=RGB(11,46,90);
    const COLORREF captionText=RGB(255,255,255);
    DwmSetWindowAttribute(u.window,35,&captionColor,sizeof(captionColor));
    DwmSetWindowAttribute(u.window,36,&captionText,sizeof(captionText));
    if(wc.hIcon)SendMessageW(u.window,WM_SETICON,ICON_BIG,
                            reinterpret_cast<LPARAM>(wc.hIcon));
    if(wc.hIconSm)SendMessageW(u.window,WM_SETICON,ICON_SMALL,
                              reinterpret_cast<LPARAM>(wc.hIconSm));
    u.dpi=GetDpiForWindow(u.window);
    Layout(u);
    SetWindowTextW(u.root,initialFolder.c_str());
    for(const auto& path:selected){
        const Entry info=InspectOne(path);
        if(info.kind!=ReparseKind::None||info.hardlinks>1)u.items.push_back(info);
    }
    if(!u.items.empty()) {
        const Scan seed{u.items,0,false,ERROR_SUCCESS};
        Populate(u,seed);
    }
    ShowWindow(u.window,SW_SHOW);
    UpdateWindow(u.window);
    // Display the native controls before starting a bounded disk/SMB scan.
    // A slow enumeration must not produce an all-white modal frame.
    if(!initialFolder.empty())
        PostMessageW(u.window,WM_TCS_MANAGER_INITIAL_SCAN,0,0);
    if(owner)EnableWindow(owner,FALSE);
    MSG msg{};
    bool quitting=false;
    while(u.running){
        const int rc=GetMessageW(&msg,nullptr,0,0);
        if(rc<=0){if(rc==0)quitting=true;break;}
        if(!IsDialogMessageW(u.window,&msg)){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if(owner&&IsWindow(owner)){EnableWindow(owner,TRUE);SetForegroundWindow(owner);}
    if(u.font)DeleteObject(u.font);
    if(quitting)PostQuitMessage(static_cast<int>(msg.wParam));
}
} // namespace tcs::manager
