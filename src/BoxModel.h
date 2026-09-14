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
    std::vector<std::wstring> items;
};

struct Layout {
    std::vector<Box> boxes;
    int opacity{88}; // 20..100, shared canvas opacity
};

std::wstring LayoutPath();
Layout LoadLayout();
bool SaveLayout(const Layout& layout);
bool AddItem(Box& box, const std::wstring& path);
}
