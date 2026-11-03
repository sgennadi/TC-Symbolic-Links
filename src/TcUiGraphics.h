#pragma once
// Shared HiDPI visual vocabulary for native WDX dialogs. No WebView, managed UI,
// external assets or companion EXE. All icon shapes are drawn as vector paths.
#include "TcVisualTheme.h"
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>

namespace tcs::ui {
struct GdiSession {
    ULONG_PTR token=0;
    GdiSession() {
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)
            token=0;
    }
    ~GdiSession(){if(token)Gdiplus::GdiplusShutdown(token);}
    bool Ready() const {return token!=0;}
    GdiSession(const GdiSession&)=delete;
    GdiSession& operator=(const GdiSession&)=delete;
};
enum class Glyph { Download, Refresh, External, Close, Folder, Search,
                   Open, Copy, Repair, Check, Link };
inline void DrawGlyph(Gdiplus::Graphics& g, Glyph glyph,
                      const Gdiplus::RectF& r, COLORREF color, UINT dpi) {
    const float sx=r.Width/20.f,sy=r.Height/20.f,x=r.X,y=r.Y;
    Gdiplus::Pen pen(tcs::visual::Tone(color),
                     (std::max)(1.f,static_cast<float>(tcs::visual::Px(1,dpi))*1.7f));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    auto line=[&](float a,float b,float c,float d){
        g.DrawLine(&pen,x+a*sx,y+b*sy,x+c*sx,y+d*sy);
    };
    auto rect=[&](float a,float b,float w,float h){
        g.DrawRectangle(&pen,x+a*sx,y+b*sy,w*sx,h*sy);
    };
    switch(glyph){
        case Glyph::Download:
            line(10,2,10,13);line(5,9,10,14);line(10,14,15,9);
            line(4,17,16,17);break;
        case Glyph::Refresh:
            g.DrawArc(&pen,x+3*sx,y+3*sy,14*sx,14*sy,35.f,280.f);
            line(15,3,18,8);line(18,8,12,8);break;
        case Glyph::External:
            rect(3,7,10,10);line(9,3,17,3);line(17,3,17,11);
            line(17,3,9,11);break;
        case Glyph::Close:
            line(5,5,15,15);line(15,5,5,15);break;
        case Glyph::Folder:
            line(2,6,8,6);line(8,6,10,8);line(10,8,18,8);
            line(18,8,18,17);line(18,17,2,17);line(2,17,2,6);break;
        case Glyph::Search:
            g.DrawEllipse(&pen,x+3*sx,y+2*sy,11*sx,11*sy);
            line(12,12,18,18);break;
        case Glyph::Open:
            rect(3,8,12,9);line(6,12,16,3);line(11,3,16,3);line(16,3,16,8);break;
        case Glyph::Copy:
            rect(6,5,11,12);line(3,13,3,2);line(3,2,13,2);
            break;
        case Glyph::Repair:
            line(4,16,15,5);line(13,3,17,7);line(2,18,5,18);
            break;
        case Glyph::Check:
            line(3,10,8,15);line(8,15,17,5);break;
        case Glyph::Link:
            g.DrawEllipse(&pen,x+1*sx,y+8*sy,11*sx,8*sy);
            g.DrawEllipse(&pen,x+8*sx,y+4*sy,11*sx,8*sy);
            break;
    }
}
inline void PaintButton(const DRAWITEMSTRUCT& item,UINT dpi,
                        bool primary,Glyph glyph,bool danger=false) {
    const bool disabled=(item.itemState & ODS_DISABLED)!=0;
    const bool pressed=(item.itemState & ODS_SELECTED)!=0;
    const RECT r=item.rcItem;
    const float w=static_cast<float>(r.right-r.left);
    const float h=static_cast<float>(r.bottom-r.top);
    if(w<=1.f||h<=1.f)return;
    Gdiplus::Graphics g(item.hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const COLORREF fill=disabled?RGB(229,234,243):
        primary?(pressed?RGB(10,81,158):RGB(16,111,212)):
        danger?(pressed?RGB(255,225,226):RGB(255,241,241)):
        pressed?RGB(222,237,252):RGB(255,255,255);
    const COLORREF edge=disabled?RGB(219,225,237):primary?fill:
        danger?RGB(241,190,190):RGB(207,221,239);
    const COLORREF ink=disabled?RGB(139,151,168):
        primary?RGB(255,255,255):
        danger?RGB(172,57,68):RGB(22,64,119);
    const Gdiplus::RectF box(static_cast<float>(r.left)+0.6f,
        static_cast<float>(r.top)+0.6f,w-1.2f,h-1.2f);
    tcs::visual::Card(g,box,static_cast<float>(tcs::visual::Px(9,dpi)),fill,edge);
    const int iconSize=tcs::visual::Px(18,dpi);
    const int space=tcs::visual::Px(12,dpi);
    const float iconY=static_cast<float>(r.top)+(h-iconSize)/2.f;
    DrawGlyph(g,glyph,Gdiplus::RectF(static_cast<float>(r.left+space),iconY,
        static_cast<float>(iconSize),static_cast<float>(iconSize)),ink,dpi);
    wchar_t text[192]{};
    GetWindowTextW(item.hwndItem,text,192);
    const float textX=static_cast<float>(r.left+space+iconSize+tcs::visual::Px(9,dpi));
    tcs::visual::Text(g,text,Gdiplus::RectF(textX,static_cast<float>(r.top+1),
        (std::max)(1.f,static_cast<float>(r.right)-textX-
        static_cast<float>(tcs::visual::Px(7,dpi))),h-2.f),
        static_cast<float>(tcs::visual::Px(13,dpi)),true,
        tcs::visual::Tone(ink),false);
    if(item.itemState & ODS_FOCUS)tcs::visual::FocusRing(
        item.hDC,r,dpi,primary);
}
inline void Header(HDC dc,const RECT& r,UINT dpi,HICON icon,
                   const std::wstring& title,const std::wstring& subtitle,
                   const std::wstring& version) {
    const float w=static_cast<float>(r.right-r.left);
    const float h=static_cast<float>(r.bottom-r.top);
    if(w<80.f||h<24.f)return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const Gdiplus::RectF box(static_cast<float>(r.left)+0.5f,
        static_cast<float>(r.top)+0.5f,w-1.f,h-1.f);
    Gdiplus::GraphicsPath shape;
    tcs::visual::AddRoundRect(shape,box,
        static_cast<float>(tcs::visual::Px(12,dpi)));
    Gdiplus::LinearGradientBrush brush(box,
        tcs::visual::Tone(tcs::visual::Navy),
        tcs::visual::Tone(RGB(15,87,176)),
        Gdiplus::LinearGradientModeHorizontal);
    g.FillPath(&brush,&shape);
    const int logo=tcs::visual::Px(36,dpi);
    const int lx=r.left+tcs::visual::Px(16,dpi);
    const float ly=static_cast<float>(r.top)+(h-logo)/2.f;
    Gdiplus::SolidBrush circle(Gdiplus::Color(45,255,255,255));
    Gdiplus::Pen circleEdge(Gdiplus::Color(95,255,255,255),1.f);
    g.FillEllipse(&circle,static_cast<float>(lx),ly,
                  static_cast<float>(logo),static_cast<float>(logo));
    g.DrawEllipse(&circleEdge,static_cast<float>(lx),ly,
                  static_cast<float>(logo),static_cast<float>(logo));
    if(icon){
        Gdiplus::Bitmap* bitmap=Gdiplus::Bitmap::FromHICON(icon);
        if(bitmap){
            g.DrawImage(bitmap,static_cast<float>(lx)+logo*.19f,
                ly+logo*.19f,logo*.62f,logo*.62f);
            delete bitmap;
        }
    }
    const float tx=static_cast<float>(lx+logo+tcs::visual::Px(15,dpi));
    const float badgeW=static_cast<float>(tcs::visual::Px(82,dpi));
    const float badgeX=static_cast<float>(r.right)-badgeW-
        static_cast<float>(tcs::visual::Px(16,dpi));
    const float titleWidth=(std::max)(1.f,badgeX-tx-
        static_cast<float>(tcs::visual::Px(12,dpi)));
    tcs::visual::Text(g,title,Gdiplus::RectF(tx,static_cast<float>(r.top)+
        tcs::visual::Px(9,dpi),titleWidth,
        static_cast<float>(tcs::visual::Px(27,dpi))),
        static_cast<float>(tcs::visual::Px(18,dpi)),true,
        tcs::visual::Tone(tcs::visual::White),false);
    tcs::visual::Text(g,subtitle,Gdiplus::RectF(tx,static_cast<float>(r.top)+
        tcs::visual::Px(37,dpi),titleWidth,
        static_cast<float>(tcs::visual::Px(18,dpi))),
        static_cast<float>(tcs::visual::Px(10,dpi)),false,
        Gdiplus::Color(224,218,238,255),false);
    if(!version.empty() && box.Width>static_cast<float>(tcs::visual::Px(420,dpi))){
        const Gdiplus::RectF chip(badgeX,static_cast<float>(r.top)+
            (h-static_cast<float>(tcs::visual::Px(28,dpi)))/2.f,
            badgeW,static_cast<float>(tcs::visual::Px(28,dpi)));
        Gdiplus::GraphicsPath round;
        tcs::visual::AddRoundRect(round,chip,
            static_cast<float>(tcs::visual::Px(8,dpi)));
        Gdiplus::SolidBrush chipBrush(Gdiplus::Color(48,255,255,255));
        g.FillPath(&chipBrush,&round);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::Font font(L"Segoe UI",
            static_cast<float>(tcs::visual::Px(11,dpi)),
            Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush white(tcs::visual::Tone(tcs::visual::White));
        g.DrawString(version.c_str(),-1,&font,chip,&format,&white);
    }
}
inline void EmptyState(HDC dc,const RECT& rect,UINT dpi,
                       const std::wstring& title,
                       const std::wstring& explanation) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::SolidBrush white(tcs::visual::Tone(tcs::visual::White));
    g.FillRectangle(&white,static_cast<float>(rect.left),
                    static_cast<float>(rect.top),
                    static_cast<float>(rect.right-rect.left),
                    static_cast<float>(rect.bottom-rect.top));
    const int cx=(rect.left+rect.right)/2;
    const int cy=(rect.top+rect.bottom)/2;
    const int size=tcs::visual::Px(40,dpi);
    const float yy=static_cast<float>(cy-tcs::visual::Px(56,dpi));
    Gdiplus::SolidBrush bg(tcs::visual::Tone(tcs::visual::SoftBlue));
    g.FillEllipse(&bg,static_cast<float>(cx-size/2),yy,
                  static_cast<float>(size),static_cast<float>(size));
    DrawGlyph(g,Glyph::Search,Gdiplus::RectF(
        static_cast<float>(cx-tcs::visual::Px(10,dpi)),
        yy+static_cast<float>(tcs::visual::Px(10,dpi)),
        static_cast<float>(tcs::visual::Px(20,dpi)),
        static_cast<float>(tcs::visual::Px(20,dpi))),
        tcs::visual::Blue,dpi);
    const float lineW=static_cast<float>(rect.right-rect.left-
        tcs::visual::Px(40,dpi));
    const float left=static_cast<float>(rect.left)+
        static_cast<float>(tcs::visual::Px(20,dpi));
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    center.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::Font boldFont(L"Segoe UI",
        static_cast<float>(tcs::visual::Px(16,dpi)),
        Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(L"Segoe UI",
        static_cast<float>(tcs::visual::Px(11,dpi)),
        Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush ink(tcs::visual::Tone(tcs::visual::Navy));
    Gdiplus::SolidBrush muted(tcs::visual::Tone(tcs::visual::Muted));
    g.DrawString(title.c_str(),-1,&boldFont,Gdiplus::RectF(left,yy+
        tcs::visual::Px(49,dpi),lineW,static_cast<float>(tcs::visual::Px(30,dpi))),
        &center,&ink);
    g.DrawString(explanation.c_str(),-1,&bodyFont,Gdiplus::RectF(left,yy+
        tcs::visual::Px(80,dpi),lineW,static_cast<float>(tcs::visual::Px(56,dpi))),
        &center,&muted);
}
} // namespace tcs::ui
