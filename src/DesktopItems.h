#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace nestlone {

struct DesktopItem {
    std::wstring name;
    std::wstring path;
    std::wstring type;
    FILETIME modified{};
    bool directory = false;
};

std::wstring DesktopDirectory();
std::vector<DesktopItem> EnumerateDesktopItems();
bool OpenItem(const DesktopItem& item);
bool RevealItem(const DesktopItem& item);
bool DeleteItem(const DesktopItem& item);
bool SetDesktopItemHidden(const std::wstring& path, bool hidden);
std::wstring FormatModified(const FILETIME& time);

}
