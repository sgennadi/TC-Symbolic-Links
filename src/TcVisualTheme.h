#pragma once
// NI Package Creator-inspired native visual layer.
// Uses only Windows GDI+, not .NET, WebView or another executable.
#include <windows.h>
// GDI+ headers need the COM IStream/PROPID types when WIN32_LEAN_AND_MEAN is set.
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>

namespace tcs::visual {
inline Gdiplus::Color Tone(COLORREF rgb, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}
constexpr COLORREF Canvas = RGB(244, 247, 253);
constexpr COLORREF Ink = RGB(20, 39, 68);
constexpr COLORREF Muted = RGB(94, 112, 142);
constexpr COLORREF Navy = RGB(11, 46, 90);
constexpr COLORREF Blue = RGB(14, 106, 230);
constexpr COLORREF DarkBlue = RGB(12, 70, 180);
constexpr COLORREF Green = RGB(16, 170, 93);
constexpr COLORREF GreenHover = RGB(12, 147, 82);
constexpr COLORREF GreenDark = RGB(10, 134, 76);
constexpr COLORREF Stroke = RGB(211, 225, 243);
constexpr COLORREF SoftBlue = RGB(226, 239, 255);
constexpr COLORREF SoftGreen = RGB(225, 248, 234);
constexpr COLORREF SoftRed = RGB(255, 236, 237);
constexpr COLORREF Red = RGB(201, 43, 57);
constexpr COLORREF White = RGB(255, 255, 255);

constexpr COLORREF Field = RGB(249, 251, 255);

// The same recognizable glyph vocabulary as NipkgCreator.ModernButton.
// Native vector paths avoid empty Segoe MDL2 glyphs on some Windows images.
enum class ButtonGlyph { Folder, Create, Close, Stop, Link };


inline int Px(int base, UINT dpi) {
    return MulDiv(base, static_cast<int>(dpi), 96);
}
inline void AddRoundRect(Gdiplus::GraphicsPath& path,
                         const Gdiplus::RectF& bounds, float radius) {
    const float r = (std::min)(radius, (std::min)(bounds.Width, bounds.Height) / 2.f);
    if (r < 1.f) {
        path.AddRectangle(bounds);
        return;
    }
    const float d = r * 2.f;
    path.AddArc(bounds.X, bounds.Y, d, d, 180.f, 90.f);
    path.AddArc(bounds.GetRight() - d, bounds.Y, d, d, 270.f, 90.f);
    path.AddArc(bounds.GetRight() - d, bounds.GetBottom() - d, d, d, 0.f, 90.f);
    path.AddArc(bounds.X, bounds.GetBottom() - d, d, d, 90.f, 90.f);
    path.CloseFigure();
}
inline void Card(Gdiplus::Graphics& g, const Gdiplus::RectF& r,
                 float radius, COLORREF fill, COLORREF outline) {
    if (r.Width <= 2.f || r.Height <= 2.f) return;
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    Gdiplus::SolidBrush background(Tone(fill));
    Gdiplus::Pen border(Tone(outline), 1.0f);
    g.FillPath(&background, &path);
    g.DrawPath(&border, &path);
}
inline void RoundedButton(HDC dc, const RECT& bounds,
                          COLORREF fill, COLORREF outline, int radius) {
    const float width = static_cast<float>(bounds.right - bounds.left);
    const float height = static_cast<float>(bounds.bottom - bounds.top);
    if (width <= 2.f || height <= 2.f) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Card(g, Gdiplus::RectF(static_cast<float>(bounds.left) + 1.f,
         static_cast<float>(bounds.top) + 1.f, width - 2.f, height - 2.f),
         static_cast<float>(radius), fill, outline);
}
inline void InputField(HDC dc, const RECT& bounds, UINT dpi, bool focused = false) {
    const float width = static_cast<float>(bounds.right - bounds.left);
    const float height = static_cast<float>(bounds.bottom - bounds.top);
    if (width <= 3.f || height <= 3.f) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Card(g, Gdiplus::RectF(static_cast<float>(bounds.left) + 0.5f,
                          static_cast<float>(bounds.top) + 0.5f,
                          width - 1.f, height - 1.f),
         static_cast<float>(Px(8, dpi)), Field, focused ? Blue : Stroke);
}

inline void Glyph(HDC dc, const RECT& bounds, ButtonGlyph kind,
                  COLORREF ink, UINT dpi) {
    const float width = static_cast<float>(bounds.right - bounds.left);
    const float height = static_cast<float>(bounds.bottom - bounds.top);
    if (width <= 1.f || height <= 1.f) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x = static_cast<float>(bounds.left);
    const float y = static_cast<float>(bounds.top);
    const float sx = width / 20.f;
    const float sy = height / 20.f;
    Gdiplus::Pen pen(Tone(ink), static_cast<float>(Px(1, dpi)) * 1.7f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    auto line = [&](float x1, float y1, float x2, float y2) {
        g.DrawLine(&pen, x + x1 * sx, y + y1 * sy,
                  x + x2 * sx, y + y2 * sy);
    };
    switch (kind) {
        case ButtonGlyph::Folder:
            line(1.f, 6.f, 7.f, 6.f);
            line(7.f, 6.f, 9.f, 8.f);
            line(9.f, 8.f, 19.f, 8.f);
            line(19.f, 8.f, 19.f, 18.f);
            line(19.f, 18.f, 1.f, 18.f);
            line(1.f, 18.f, 1.f, 6.f);
            break;
        case ButtonGlyph::Create: {
            Gdiplus::PointF points[3] = {
                {x + 5.f * sx, y + 2.f * sy},
                {x + 18.f * sx, y + 10.f * sy},
                {x + 5.f * sx, y + 18.f * sy}};
            Gdiplus::SolidBrush brush(Tone(ink));
            g.FillPolygon(&brush, points, 3);
            break;
        }
        case ButtonGlyph::Close:
            line(4.f, 4.f, 16.f, 16.f);
            line(16.f, 4.f, 4.f, 16.f);
            break;
        case ButtonGlyph::Stop:
            g.DrawRectangle(&pen, x + 5.f * sx, y + 5.f * sy,
                            10.f * sx, 10.f * sy);
            break;
        case ButtonGlyph::Link:
            g.DrawEllipse(&pen, x + 1.f * sx, y + 8.f * sy, 11.f * sx, 8.f * sy);
            g.DrawEllipse(&pen, x + 8.f * sx, y + 4.f * sy, 11.f * sx, 8.f * sy);
            break;
    }
}


inline void Text(Gdiplus::Graphics& g, const std::wstring& text,
                 const Gdiplus::RectF& region, float px, bool bold,
                 Gdiplus::Color color, bool rtl) {
    if (text.empty() || region.Width < 1.f || region.Height < 1.f) return;
    Gdiplus::Font font(L"Segoe UI", px, bold ? Gdiplus::FontStyleBold :
                      Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(rtl ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
        (rtl ? Gdiplus::StringFormatFlagsDirectionRightToLeft : 0));
    Gdiplus::SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, region, &format, &brush);
}
inline void Banner(HDC dc, RECT area, UINT dpi, HICON icon,
                   const std::wstring& title, const std::wstring& subtitle,
                   bool rtl) {
    const float x = static_cast<float>(area.left);
    const float y = static_cast<float>(area.top);
    const float w = static_cast<float>(area.right - area.left);
    const float h = static_cast<float>(area.bottom - area.top);
    if (w < 16.f || h < 16.f) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::RectF rect(x + 0.5f, y + 0.5f, w - 1.f, h - 1.f);
    Gdiplus::GraphicsPath clip;
    AddRoundRect(clip, rect, static_cast<float>(Px(17, dpi)));
    Gdiplus::LinearGradientBrush gradient(rect,
        Tone(Navy), Tone(Blue), Gdiplus::LinearGradientModeHorizontal);
    g.FillPath(&gradient, &clip);
    const Gdiplus::GraphicsState previous = g.Save();
    g.SetClip(&clip);
    // Subtle curved green and blue waves match the NI Package Creator header.
    Gdiplus::GraphicsPath blueWave;
    blueWave.AddBezier(x + w - Px(360, dpi), y + h,
        x + w - Px(240, dpi), y + h - Px(80, dpi),
        x + w - Px(95, dpi), y + h - Px(36, dpi),
        x + w, y + h - Px(25, dpi));
    blueWave.AddLine(x + w, y + h - Px(25, dpi), x + w, y + h);
    blueWave.CloseFigure();
    Gdiplus::SolidBrush blueGlow(Gdiplus::Color(87, 43, 190, 255));
    g.FillPath(&blueGlow, &blueWave);
    Gdiplus::GraphicsPath greenWave;
    greenWave.AddBezier(x + w - Px(235, dpi), y + h,
        x + w - Px(140, dpi), y + h - Px(65, dpi),
        x + w - Px(62, dpi), y + h - Px(52, dpi),
        x + w, y + h - Px(70, dpi));
    greenWave.AddLine(x + w, y + h - Px(70, dpi), x + w, y + h);
    greenWave.CloseFigure();
    Gdiplus::SolidBrush greenGlow(Gdiplus::Color(108, 34, 211, 128));
    g.FillPath(&greenGlow, &greenWave);
    g.Restore(previous);

    const float badge = static_cast<float>(Px(58, dpi));
    const float badgeX = rtl ? x + w - static_cast<float>(Px(76, dpi)) :
                             x + static_cast<float>(Px(19, dpi));
    const float badgeY = y + (h - badge) / 2.f;
    Gdiplus::SolidBrush badgeFill(Gdiplus::Color(49, 255, 255, 255));
    Gdiplus::Pen badgeOutline(Gdiplus::Color(128, 255, 255, 255), 1.1f);
    g.FillEllipse(&badgeFill, badgeX, badgeY, badge, badge);
    g.DrawEllipse(&badgeOutline, badgeX, badgeY, badge, badge);
    if (icon) {
        Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromHICON(icon);
        if (bitmap) {
            g.DrawImage(bitmap, badgeX + badge * 0.19f, badgeY + badge * 0.19f,
                        badge * 0.62f, badge * 0.62f);
            delete bitmap;
        }
    }
    const float textX = rtl ? x + static_cast<float>(Px(24, dpi)) :
                              x + static_cast<float>(Px(95, dpi));
    const float textRightPad = rtl ? static_cast<float>(Px(100, dpi)) :
                                    static_cast<float>(Px(16, dpi));
    const float textW = (std::max)(1.f, w - (textX - x) - textRightPad);
    Text(g, title, Gdiplus::RectF(textX, y + static_cast<float>(Px(10, dpi)),
         textW, static_cast<float>(Px(38, dpi))),
         static_cast<float>(Px(22, dpi)), true, Tone(White), rtl);
    Text(g, subtitle, Gdiplus::RectF(textX, y + static_cast<float>(Px(49, dpi)),
         textW, static_cast<float>(Px(28, dpi))),
         static_cast<float>(Px(11, dpi)), false,
         Gdiplus::Color(235, 225, 240, 255), rtl);
}
inline void FormPanels(HDC dc, const RECT& client, UINT dpi,
                       int margin, int gap, int cardTop, int cardBottom,
                       int toolsTop, int toolsHeight,
                       bool finished, bool failed, bool unsafe, bool creating,
                       const std::wstring& status, bool rtl) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const float width = static_cast<float>(client.right - client.left);
    const float usable = width - 2.f * margin;
    const float cardW = (usable - gap) * 0.5f;
    const float corner = static_cast<float>(Px(13, dpi));
    Card(g, Gdiplus::RectF(static_cast<float>(margin) + 0.5f,
         static_cast<float>(toolsTop) + 0.5f, usable - 1.f,
         static_cast<float>(toolsHeight) - 1.f), corner, White, Stroke);
    const float panelHeight = static_cast<float>(cardBottom - cardTop);
    Card(g, Gdiplus::RectF(static_cast<float>(margin) + 0.5f,
         static_cast<float>(cardTop) + 0.5f, cardW - 1.f,
         panelHeight - 1.f), corner, White, Stroke);
    Card(g, Gdiplus::RectF(static_cast<float>(margin) + cardW + gap + 0.5f,
         static_cast<float>(cardTop) + 0.5f, cardW - 1.f,
         panelHeight - 1.f), corner, White, Stroke);
    const int statusTop = client.bottom - Px(67, dpi);
    const COLORREF statusFill = unsafe || failed ? SoftRed :
        finished && !creating ? SoftGreen : SoftBlue;
    const COLORREF statusBorder = unsafe || failed ? RGB(241, 179, 179) :
        finished && !creating ? RGB(159, 222, 182) : Stroke;
    Card(g, Gdiplus::RectF(static_cast<float>(margin) + 0.5f,
         static_cast<float>(statusTop) + 0.5f, usable - 1.f,
         static_cast<float>(Px(55, dpi)) - 1.f),
         corner, statusFill, statusBorder);
    const COLORREF statusInk = unsafe || failed ? RGB(136, 39, 48) :
        finished && !creating ? GreenDark : Navy;
    Text(g, status, Gdiplus::RectF(
         rtl ? static_cast<float>(margin + Px(255, dpi)) :
               static_cast<float>(margin + Px(18, dpi)),
         static_cast<float>(statusTop + Px(5, dpi)),
         (std::max)(1.f, usable - static_cast<float>(Px(265, dpi))),
         static_cast<float>(Px(44, dpi))),
         static_cast<float>(Px(11, dpi)), true, Tone(statusInk), rtl);
}
} // namespace tcs::visual
