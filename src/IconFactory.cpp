#include "IconFactory.h"
#include <gdiplus.h>
#include <filesystem>

using namespace Gdiplus;

namespace nestlone {
namespace {

void FillRound(HDC dc, COLORREF color, int left, int top, int right, int bottom, int radius) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old = SelectObject(dc, brush);
    RoundRect(dc, left, top, right, bottom, radius, radius);
    SelectObject(dc, old);
    DeleteObject(brush);
}

}

HICON CreateNestloneIcon(int size) {
    static ULONG_PTR gdiplusToken = [] {
        GdiplusStartupInput input;
        ULONG_PTR token = 0;
        GdiplusStartup(&token, &input, nullptr);
        return token;
    }();
    (void)gdiplusToken;

    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path png = std::filesystem::path(modulePath).parent_path() / L"assets" / L"nestlone-icon.png";
    Bitmap source(png.c_str(), FALSE);
    if (source.GetLastStatus() == Ok) {
        Bitmap resized(size, size, PixelFormat32bppARGB);
        Graphics graphics(&resized);
        graphics.SetCompositingMode(CompositingModeSourceCopy);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.DrawImage(&source, Rect(0, 0, size, size));
        HICON icon = nullptr;
        if (resized.GetHICON(&icon) == Ok && icon) return icon;
    }

    // Deterministic fallback keeps the app usable when the external PNG is missing.
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* pixels = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !pixels) return nullptr;

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, color);
    RECT bounds{0, 0, size, size};
    HBRUSH clear = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(dc, &bounds, clear);
    DeleteObject(clear);
    SetBkMode(dc, TRANSPARENT);
    const int s = max(1, size / 16);
    FillRound(dc, RGB(74, 211, 226), 2 * s, 5 * s, 14 * s, 13 * s, 4 * s);
    FillRound(dc, RGB(112, 226, 212), 3 * s, 3 * s, 15 * s, 11 * s, 4 * s);
    FillRound(dc, RGB(235, 255, 252), 4 * s, 2 * s, 13 * s, 9 * s, 3 * s);
    FillRound(dc, RGB(72, 176, 216), 5 * s, 4 * s, 12 * s, 6 * s, 1 * s);
    FillRound(dc, RGB(255, 139, 126), 11 * s, 1 * s, 15 * s, 4 * s, 2 * s);
    SelectObject(dc, old);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}
