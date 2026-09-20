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
    bool selected{false}; // transient owned-view selection, not persisted
    // Empty means a standalone box. A member stores the id of its group root.
    std::wstring groupId;
    // Used by a group root; empty selects the root's own tab.
    std::wstring activeTabId;
    std::vector<std::wstring> items;
};

struct Layout {
    std::vector<Box> boxes;
    int opacity{88}; // 20..100, shared canvas opacity
    struct Placement { std::wstring path; POINT point; };
    std::vector<Placement> desktop;
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
