// Offline rendering validation of the Nipkg-Creator-inspired Win32 UI.
// Works without an interactive Total Commander desktop.
#include "TcVisualTheme.h"
#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    Surface(int width, int height) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        dc = CreateCompatibleDC(nullptr);
        Check(dc != nullptr, "Cannot create offscreen drawing context");
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(bitmap != nullptr && pixels != nullptr, "Cannot create DIB surface");
        old = SelectObject(dc, bitmap);
        RECT bounds{0, 0, width, height};
        HBRUSH brush = CreateSolidBrush(tcs::visual::Canvas);
        FillRect(dc, &bounds, brush);
        DeleteObject(brush);
    }
    ~Surface() {
        if (dc && old) SelectObject(dc, old);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
} // namespace

int main() {
    try {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token{};
        Check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok,
              "Windows GDI+ failed to initialize");
        {
            Surface screen(690, 410);
            tcs::visual::Banner(screen.dc, RECT{15, 15, 675, 105}, 96,
                nullptr, L"TC Symbolic Links", L"One selected - safe preview", false);
            const COLORREF navy = GetPixel(screen.dc, 35, 31);
            const COLORREF blue = GetPixel(screen.dc, 645, 31);
            Check(navy != CLR_INVALID && blue != CLR_INVALID,
                  "Gradient pixels could not be read");
            Check(GetBValue(blue) > GetBValue(navy) + 45 &&
                  GetGValue(blue) > GetGValue(navy) + 20,
                  "Navy-to-electric-blue header gradient is missing");

            RECT primary{28, 120, 205, 163};
            tcs::visual::RoundedButton(screen.dc, primary,
                tcs::visual::Green, tcs::visual::Green, 10);
            const COLORREF center = GetPixel(screen.dc, 110, 143);
            const COLORREF corner = GetPixel(screen.dc, 29, 121);
            Check(GetGValue(center) > GetRValue(center) + 90,
                  "Primary Create button is not green");
            Check(corner == tcs::visual::Canvas,
                  "Rounded corner is square or its backdrop is not preserved");
            const RECT client{0, 0, 690, 410};
            tcs::visual::FormPanels(screen.dc, client, 96, 15, 14,
                175, 244, 113, 50, true, false, false, false,
                L"All links created successfully", false);
            const COLORREF success = GetPixel(screen.dc, 425, 369);
            Check(GetGValue(success) > GetRValue(success),
                  "Success status card did not show soft green");
            tcs::visual::FormPanels(screen.dc, client, 96, 15, 14,
                175, 244, 113, 50, true, true, false, false,
                L"SMB access denied", false);
            const COLORREF failure = GetPixel(screen.dc, 425, 369);
            Check(GetRValue(failure) > GetGValue(failure),
                  "Failure status card did not show soft red");
        }
        {
            Surface dpi2(1280, 270);
            tcs::visual::Banner(dpi2.dc, RECT{22, 20, 1250, 200}, 192,
                nullptr, L"TC Symbolic Links", L"DPI 200% - Unicode", true);
            Check(GetPixel(dpi2.dc, 1150, 35) != CLR_INVALID,
                  "200 percent DPI header rendering failed");
        }
        Gdiplus::GdiplusShutdown(token);
        std::cout << "PASS: antialiased corners, NI gradient, green actions, "
                     "semantic status cards, RTL and 200% DPI offscreen rendering\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
