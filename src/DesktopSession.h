#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace nestlone {
// Coordinates are physical pixels relative to the virtual-screen origin.
struct DesktopEntry {
    std::wstring path;
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
bool ShowShellBackgroundMenu(HWND owner, POINT screenPoint);
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
