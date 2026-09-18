#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace nestlone {
struct Box {
    std::wstring id;
    std::wstring title;
    RECT rect{80, 80, 380, 300};
    COLORREF color{RGB(184, 235, 238)};
    bool collapsed{false};
    bool iconView{false};
    bool selected{false}; // transient owned-view selection, not persisted
    std::vector<std::wstring> items;
};

struct Layout {
    std::vector<Box> boxes;
    int opacity{88}; // 20..100, shared canvas opacity
    struct Placement { std::wstring path; POINT point; };
    std::vector<Placement> desktop;
};

std::wstring LayoutPath();
Layout LoadLayout();
bool SaveLayout(const Layout& layout);
bool AddItem(Box& box, const std::wstring& path);
bool AssignDesktopItem(Layout& layout, const std::wstring& path, int boxIndex, POINT point);
}
