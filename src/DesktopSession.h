#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace nestlone {
// Coordinates are physical pixels relative to the virtual-screen origin.
struct DesktopEntry {
    // `path` is the Shell parsing name.  It is a filesystem path for ordinary
    // items, but can be a namespace identifier (for example Recycle Bin).
    std::wstring path;
    // Shell's user-facing name.  Never derive this from `path`: virtual
    // desktop items do not have a filename to strip.
    std::wstring displayName;
    POINT position{};
    RECT bounds{};
    int index=-1;
};
struct DesktopSnapshot {
    HWND listview=nullptr;
    DWORD process=0;
    POINT spacing{76,91};
    int iconSize=32;
    bool autoArrange=false;
    bool iconMode=false;
    bool readable=false;
    std::wstring error;
    uint64_t applied=0;
    std::vector<DesktopEntry> entries;
};
struct DesktopMove {std::wstring path; POINT position;};
bool ReadDesktop(DesktopSnapshot& result);
bool ShellBackgroundMenuAvailable();
bool ShowShellBackgroundMenu(HWND owner, POINT screenPoint, bool* createBox=nullptr);
// The custom canvas owns the popup, so it must forward Shell's lazy submenu
// messages (notably the desktop "New" menu) while the popup is active.
bool ForwardShellContextMenuMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result);
// Coalesced requests are handled on a COM worker, never on the painting thread.
uint64_t QueueDesktopMoves(const std::vector<DesktopMove>& moves);
bool PollDesktop(DesktopSnapshot& result);
void CancelDesktopMoves();
void RestoreLegacyMask(HWND listview);
bool RestoreHiddenDesktopListView();
void ClaimHiddenDesktopListView(HWND listview);
void ReleaseHiddenDesktopListView(HWND listview);
// Only for old recovery subprocess command lines, no new helper is launched.
int DesktopRecovery(const wchar_t* arguments);
}
