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
#include "DesktopSession.h"
#include "resource.h"
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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    if(wcsncmp(arguments,L"--restore-desktop ",18)==0)return nestlone::DesktopRecovery(arguments);
    g_mutex = CreateMutexW(nullptr, TRUE, kMutex);
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowExW(HWND_MESSAGE,nullptr,kControlClass,nullptr);
        if (existing) PostMessageW(existing, WM_COMMAND, nestlone::ID_TRAY_TOGGLE, 0);
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }
    db::LogInit();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring layoutFile=nestlone::LayoutPath();
    if(GetFileAttributesW(layoutFile.c_str())!=INVALID_FILE_ATTRIBUTES)
        CopyFileW(layoutFile.c_str(),(layoutFile+L".pre-hosting").c_str(),TRUE);
    g_layout = nestlone::LoadLayout();
    if (g_layout.boxes.empty()) { nestlone::Box box; box.id = L"default"; box.title = L"我的盒子"; g_layout.boxes.push_back(box); }
    // Hosting affects only presentation; file paths and attributes are unchanged.

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = ControlProc;
    wc.lpszClassName = kControlClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_NESTLONE));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_NESTLONE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!wc.hIcon) wc.hIcon = nestlone::CreateNestloneIcon(GetSystemMetrics(SM_CXICON));
    if (!wc.hIconSm) wc.hIconSm = nestlone::CreateNestloneIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);
    g_control = CreateWindowExW(0, kControlClass, L"nestlone-D", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_control) return 1;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // Always expose an exit/settings route before any desktop discovery.
    if(!nestlone::TrayInstall(g_control,instance)) {
        MessageBoxW(nullptr,L"无法建立托盘入口，程序将退出，桌面保持不变。",L"nestlone-D",MB_OK|MB_ICONWARNING);
        DestroyWindow(g_control);return 1;
    }

    db::HostInfo host = db::DiscoverDesktopHost();
    db::LogHostInfo(host);
    HWND canvasParent = host.wallpaperWorker ? host.wallpaperWorker : GetDesktopWindow();
    if (!nestlone::CreateCanvas(instance, canvasParent, &g_layout)) {
        db::LogF("CreateCanvas failed: %lu", GetLastError());
        MessageBoxW(nullptr,L"桌面接管未能启动，原生桌面保持不变。请查看日志。",L"nestlone-D",MB_OK|MB_ICONWARNING);
        DestroyWindow(g_control);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    db::LogClose();
    CoUninitialize();
    CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
