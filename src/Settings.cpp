#include "Settings.h"
#include "DesktopCanvas.h"
#include "Theme.h"
#include <commctrl.h>

namespace nestlone {
namespace {
constexpr wchar_t kClassName[] = L"nestlone-D.Settings";
constexpr int kSlider = 2001;
constexpr int kValue = 2002;
constexpr int kBoxSelect = 2100;
constexpr int kColorBase = 2110;
constexpr int kSidebarTheme = 2201;
constexpr int kSidebarLayout = 2202;
HFONT g_font = nullptr;
HBRUSH g_surface = nullptr;
HWND g_window = nullptr;
HWND g_value = nullptr;
HWND g_boxSelect = nullptr;
Layout* g_layout = nullptr;
int g_selectedBox = 0;
constexpr COLORREF kColors[] = {RGB(184, 235, 238), RGB(197, 226, 250), RGB(210, 245, 228), RGB(255, 225, 190), RGB(245, 211, 240), RGB(218, 220, 250)};

void UpdateValue() {
    if (!g_layout || !g_value) return;
    wchar_t text[32];
    swprintf_s(text, L"%d%%", g_layout->opacity);
    SetWindowTextW(g_value, text);
    CanvasSetOpacity(g_layout->opacity);
    SaveLayout(*g_layout);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HFONT font = g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND sidebar = CreateWindowW(L"STATIC", L"nestlone-D", WS_CHILD | WS_VISIBLE, 20, 24, 120, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(sidebar, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND theme = CreateWindowW(L"BUTTON", L"主题设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 60, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarTheme)), nullptr, nullptr);
        SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND layout = CreateWindowW(L"BUTTON", L"桌面外观", WS_CHILD | WS_VISIBLE | BS_FLAT | WS_DISABLED, 12, 100, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarLayout)), nullptr, nullptr);
        SendMessageW(layout, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND title = CreateWindowW(L"STATIC", L"主题设置", WS_CHILD | WS_VISIBLE, 170, 20, 250, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND hint = CreateWindowW(L"STATIC", L"背景不透明度", WS_CHILD | WS_VISIBLE, 170, 66, 180, 24, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_value = CreateWindowW(L"STATIC", L"88%", WS_CHILD | WS_VISIBLE | SS_RIGHT, 390, 66, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kValue)), nullptr, nullptr);
        SendMessageW(g_value, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 170, 98, 290, 40, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlider)), nullptr, nullptr);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(20, 100));
        SendMessageW(slider, TBM_SETPOS, TRUE, g_layout ? g_layout->opacity : 88);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND boxLabel = CreateWindowW(L"STATIC", L"当前盒子", WS_CHILD | WS_VISIBLE, 170, 150, 90, 24, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(boxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_boxSelect = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 260, 146, 200, 180, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoxSelect)), nullptr, nullptr);
        for (const Box& box : g_layout->boxes) SendMessageW(g_boxSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(box.title.c_str()));
        if (!g_layout->boxes.empty()) SendMessageW(g_boxSelect, CB_SETCURSEL, 0, 0);
        HWND colorLabel = CreateWindowW(L"STATIC", L"盒子背景色", WS_CHILD | WS_VISIBLE, 170, 188, 100, 24, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(colorLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (int i = 0; i < 6; ++i) {
            HWND swatch = CreateWindowW(L"BUTTON", L"  ", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 280 + i * 30, 184, 24, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kColorBase + i)), nullptr, nullptr);
            SendMessageW(swatch, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        HWND close = CreateWindowW(L"BUTTON", L"完成", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 368, 224, 92, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(close, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(g_boxSelect, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_selectedBox = 0;
        wchar_t value[32]{};
        swprintf_s(value,L"%d%%",g_layout->opacity);
        SetWindowTextW(g_value,value);
        HWND note=CreateWindowW(L"STATIC",L"即时预览 · 自动保存 · 图标与文字保持清晰",
            WS_CHILD|WS_VISIBLE,170,270,330,24,hwnd,nullptr,nullptr,nullptr);
        SendMessageW(note,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        SetTextColor(dc,theme::title);
        SetBkColor(dc,RGB(246,250,252));
        return reinterpret_cast<LRESULT>(g_surface);
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kSlider) {
            g_layout->opacity = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0));
            UpdateValue();
        }
        return 0;
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw && (draw->CtlID == kSidebarTheme || draw->CtlID == IDCANCEL)) {
            HBRUSH brush=CreateSolidBrush(RGB(217,242,245));
            HPEN pen=CreatePen(PS_SOLID,1,RGB(190,224,230));
            auto ob=SelectObject(draw->hDC,brush); auto op=SelectObject(draw->hDC,pen);
            const RECT& r=draw->rcItem;
            RoundRect(draw->hDC,r.left,r.top,r.right,r.bottom,12,12);
            SelectObject(draw->hDC,ob); SelectObject(draw->hDC,op);
            DeleteObject(brush); DeleteObject(pen);
            auto of=SelectObject(draw->hDC,g_font);
            SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,theme::title);
            RECT label=r; DrawTextW(draw->hDC,draw->CtlID==IDCANCEL ? L"完成" : L"主题设置",-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(draw->itemState & ODS_FOCUS) { InflateRect(&label,-3,-3); DrawFocusRect(draw->hDC,&label); }
            SelectObject(draw->hDC,of);
            return TRUE;
        }
        if (draw && draw->CtlID >= kColorBase && draw->CtlID < kColorBase + 6) {
            HBRUSH brush = CreateSolidBrush(kColors[draw->CtlID - kColorBase]);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            FrameRect(draw->hDC, &draw->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kBoxSelect && HIWORD(wParam) == CBN_SELCHANGE) {
            g_selectedBox = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            return 0;
        }
        if (LOWORD(wParam) >= kColorBase && LOWORD(wParam) < kColorBase + 6 && g_layout && g_selectedBox >= 0 && g_selectedBox < static_cast<int>(g_layout->boxes.size())) {
            g_layout->boxes[g_selectedBox].color = kColors[LOWORD(wParam) - kColorBase];
            SaveLayout(*g_layout);
            CanvasSetOpacity(g_layout->opacity);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: if (g_font) { DeleteObject(g_font); g_font=nullptr; } g_window = nullptr; g_value = nullptr; g_layout = nullptr; return 0;
    default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

void ShowSettings(HINSTANCE instance, Layout* layout) {
    g_layout = layout;
    if (g_window) { ShowWindow(g_window, SW_SHOWNORMAL); SetForegroundWindow(g_window); return; }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = SettingsProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!g_surface) g_surface=CreateSolidBrush(RGB(246,250,252));
    wc.hbrBackground = g_surface;
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"nestlone-D 设置中心", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 350, nullptr, nullptr, instance, nullptr);
    if (!g_window) return;
    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);
}

}
