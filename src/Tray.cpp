#include "Tray.h"
#include "IconFactory.h"
#include "resource.h"
#include <shellapi.h>

namespace nestlone {
namespace { NOTIFYICONDATAW g_data{}; HICON g_trayIcon = nullptr; }

bool TrayInstall(HWND owner, HINSTANCE) {
    g_data = {};
    g_data.cbSize = sizeof(g_data);
    g_data.hWnd = owner;
    g_data.uID = 1;
    g_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_data.uCallbackMessage = WM_TRAY;
    if (!g_trayIcon) g_trayIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_NESTLONE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!g_trayIcon) g_trayIcon = CreateNestloneIcon(GetSystemMetrics(SM_CXSMICON));
    g_data.hIcon = g_trayIcon;
    wcscpy_s(g_data.szTip, L"nestlone-D 桌面盒子");
    return Shell_NotifyIconW(NIM_ADD, &g_data) != FALSE;
}

void TrayRemove(HWND) {
    if (g_data.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_data);
    if (g_trayIcon) { DestroyIcon(g_trayIcon); g_trayIcon = nullptr; }
    g_data = {};
}

void TrayShowMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE, L"显示 / 隐藏盒子（恢复桌面图标）");
    AppendMenuW(menu, MF_STRING, ID_TRAY_NEW_BOX, L"新建盒子");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD, L"重新加载布局");
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"设置中心");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出 nestlone-D");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(owner);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
}
