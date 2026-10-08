#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace nestlone {
struct Box {
    std::wstring id;
    std::wstring title;
    // Only used by a group root.  `title` remains the root tab's title.
    std::wstring groupTitle;
    RECT rect{80, 80, 380, 300};
    COLORREF color{RGB(184, 235, 238)};
    bool collapsed{false};
    bool iconView{false};
    int listScroll{0}; // transient vertical offset for overflowing list content
    bool selected{false}; // transient owned-view selection, not persisted
    // Empty means a standalone box. A member stores the id of its group root.
    std::wstring groupId;
    int tabOrder{0}; // Stable visual order, independent of the group's root.
    // Used by a group root; empty selects the root's own tab.
    std::wstring activeTabId;
    std::vector<std::wstring> items;
};

struct Layout {
    std::vector<Box> boxes;
    int opacity{88}; // 0..100, shared canvas background opacity
    int cornerRadius{12}; // 0..48, shared box corner diameter at 96 DPI
    int defaultBoxWidth{360};
    int defaultBoxHeight{320};
    COLORREF defaultBoxColor{RGB(184, 235, 238)};
    std::wstring fontFamily{L"Microsoft YaHei UI"};
    struct Placement { std::wstring path; POINT point; };
    std::vector<Placement> desktop;
    struct Widget {
        std::wstring id;
        std::wstring type; // note | weather
        RECT rect{120, 120, 380, 300};
        std::wstring text;
        std::wstring city{L"北京"};
        std::wstring weather;
        std::wstring weatherIcon{L"999"};
        int temperature{0};
        bool locked{false};
    };
    std::vector<Widget> widgets;
    struct AutoRule { std::wstring boxId; bool folders{false}; std::wstring extensions; };
    bool autoOrganize{false};
    std::vector<AutoRule> autoRules;
};

std::wstring LayoutPath();
Layout LoadLayout();
bool SaveLayout(const Layout& layout);
bool AddItem(Box& box, const std::wstring& path);
bool AssignDesktopItem(Layout& layout, const std::wstring& path, int boxIndex, POINT point);
}
