#include <windows.h>
#include <objbase.h>
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include "BoxModel.h"
#include "DesktopCanvas.h"
#include "DesktopHost.h"
#include "Tray.h"
#include "IconFactory.h"
#include "Settings.h"
#include "DesktopItems.h"
#include "log.h"

namespace {
constexpr wchar_t kControlClass[] = L"nestlone-D.Control";
constexpr wchar_t kMutex[] = L"Local\\nestlone-desktop-single-instance";
HANDLE g_mutex = nullptr;
HWND g_control = nullptr;
nestlone::Layout g_layout;
UINT g_taskbarCreated = 0;

void DispatchCanvas(nestlone::CanvasCommand command) { nestlone::HandleCanvasCommand(command); }

LRESULT CALLBACK ControlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreated) { nestlone::TrayInstall(hwnd, GetModuleHandleW(nullptr)); return 0; }
    switch (message) {
    case nestlone::WM_TRAY:
        if (lParam == WM_RBUTTONUP) nestlone::TrayShowMenu(hwnd);
        else if (lParam == WM_LBUTTONDBLCLK) DispatchCanvas(nestlone::CanvasCommand::Toggle);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case nestlone::ID_TRAY_TOGGLE: DispatchCanvas(nestlone::CanvasCommand::Toggle); break;
        case nestlone::ID_TRAY_NEW_BOX: DispatchCanvas(nestlone::CanvasCommand::NewBox); break;
        case nestlone::ID_TRAY_RELOAD: DispatchCanvas(nestlone::CanvasCommand::Reload); break;
        case nestlone::ID_TRAY_SETTINGS: nestlone::ShowSettings(GetModuleHandleW(nullptr), &g_layout); break;
        case nestlone::ID_TRAY_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_DESTROY:
        nestlone::SaveLayout(g_layout);
        nestlone::DestroyCanvas();
        nestlone::TrayRemove(hwnd);
        PostQuitMessage(0);
        return 0;
    default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_mutex = CreateMutexW(nullptr, TRUE, kMutex);
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kControlClass, nullptr);
        if (existing) PostMessageW(existing, WM_COMMAND, nestlone::ID_TRAY_TOGGLE, 0);
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }
    db::LogInit();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_layout = nestlone::LoadLayout();
    if (g_layout.boxes.empty()) { nestlone::Box box; box.id = L"default"; box.title = L"我的盒子"; g_layout.boxes.push_back(box); }
    // Items already managed by a box remain hidden from the desktop after restart.
    for (const auto& box : g_layout.boxes) for (const auto& item : box.items) nestlone::SetDesktopItemHidden(item, true);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = ControlProc;
    wc.lpszClassName = kControlClass;
    wc.hIcon = nestlone::CreateNestloneIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = nestlone::CreateNestloneIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);
    g_control = CreateWindowExW(0, kControlClass, L"nestlone-D", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_control) return 1;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    db::HostInfo host = db::DiscoverDesktopHost();
    db::LogHostInfo(host);
    HWND canvasParent = host.wallpaperWorker ? host.wallpaperWorker : GetDesktopWindow();
    if (!nestlone::CreateCanvas(instance, canvasParent, &g_layout)) {
        db::LogF("CreateCanvas failed: %lu", GetLastError());
        DestroyWindow(g_control);
    } else {
        nestlone::TrayInstall(g_control, instance);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    db::LogClose();
    if (wc.hIcon) DestroyIcon(wc.hIcon);
    if (wc.hIconSm) DestroyIcon(wc.hIconSm);
    CoUninitialize();
    CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
