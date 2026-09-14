#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace nestlone {
struct DesktopEntry {
    std::wstring path;
    std::wstring name;
    POINT position{};
    RECT bounds{};
    std::vector<DWORD> largeIcon,smallIcon;
};
bool ReadDesktop(std::vector<DesktopEntry>& entries);
bool BeginDesktopSession();
void EndDesktopSession();
bool DesktopSessionAlive();
int DesktopRecovery(const wchar_t* arguments);
bool PollDesktop(std::vector<DesktopEntry>& entries);
bool MaskDesktopItems(const std::vector<DesktopEntry>& entries,const std::vector<std::wstring>& paths);
void PlaceDesktopItem(const std::wstring& path,POINT point);
std::vector<DWORD> IconPixels(HICON icon,int size);
}
