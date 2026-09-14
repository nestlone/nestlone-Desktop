#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace nestlone {
struct DesktopEntry {
    std::wstring path;
    std::wstring name;
    POINT position{};
};
bool ReadDesktop(std::vector<DesktopEntry>& entries);
bool BeginDesktopSession();
void EndDesktopSession();
bool DesktopSessionAlive();
int DesktopRecovery(const wchar_t* arguments);
}
