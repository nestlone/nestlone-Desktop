#pragma once
#include <windows.h>

#include <string>
#include <vector>

// 桌面宿主发现：找到承载桌面图标的那套窗口，以及“图标之下、壁纸之上”的绘制层。
// 注意：Windows 10 和 Windows 11 的层级不一样，所以这里全部靠运行时探测，
// 不写死任何假设。
namespace db {

struct HostInfo {
    HWND progman = nullptr;          // Progman
    HWND defview = nullptr;          // SHELLDLL_DefView（图标宿主容器）
    HWND defviewParent = nullptr;    // SHELLDLL_DefView 的父窗口
    HWND listview = nullptr;         // SysListView32（真正的图标列表）
    HWND wallpaperWorker = nullptr;  // 图标之下的 WorkerW，盒子窗口要挂到这里
    long long iconCount = -1;        // LVM_GETITEMCOUNT 结果，-1 表示未取到
    DWORD iconCountErr = 0;          // SendMessage 失败时的 GetLastError
    std::wstring listviewChain;      // 图标列表的祖先链
    RECT listviewRect{};             // 屏幕坐标
    RECT listviewClient{};           // 客户区尺寸
};

std::wstring ClassOf(HWND h);
std::wstring TitleOf(HWND h);
std::wstring ChainOf(HWND h);                    // 祖先链：SysListView32 <- SHELLDLL_DefView <- Progman
HWND FindClassAnywhere(const wchar_t* cls);      // 跨顶层窗口递归查找
std::vector<HWND> TopLevelsOfClass(const wchar_t* cls);
std::vector<HWND> TopLevels();
bool HasDescendantOfClass(HWND root, const wchar_t* cls);
void DumpWindowTree(int maxDepth);
HostInfo DiscoverDesktopHost();
void LogHostInfo(const HostInfo& info);

}  // namespace db
