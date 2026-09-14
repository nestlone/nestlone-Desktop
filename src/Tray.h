#pragma once
#include <windows.h>

namespace nestlone {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_CANVAS_COMMAND = WM_APP + 2;
constexpr UINT ID_TRAY_TOGGLE = 1001;
constexpr UINT ID_TRAY_NEW_BOX = 1002;
constexpr UINT ID_TRAY_RELOAD = 1003;
constexpr UINT ID_TRAY_EXIT = 1004;
constexpr UINT ID_TRAY_SETTINGS = 1005;

bool TrayInstall(HWND owner, HINSTANCE instance);
void TrayRemove(HWND owner);
void TrayShowMenu(HWND owner);
}
