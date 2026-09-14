#include "DesktopHost.h"

#include <commctrl.h>

#include "log.h"

namespace db {
namespace {

const int kMaxChildrenPerNode = 25;

struct EnumCtx {
    std::vector<HWND>* out = nullptr;
};

BOOL CALLBACK EnumTopProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    ctx->out->push_back(hwnd);
    return TRUE;
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    ctx->out->push_back(hwnd);
    return TRUE;
}

void DumpNode(HWND h, int depth, int maxDepth) {
    RECT r{};
    GetWindowRect(h, &r);
    LONG style = GetWindowLongW(h, GWL_STYLE);
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    std::wstring title = TitleOf(h);
    if (title.size() > 60) title = title.substr(0, 60) + L"...";

    LogF("%*s%-24s hwnd=%p rect=(%ld,%ld,%ld,%ld) style=%#010lx ex=%#010lx %s%s \"%s\"",
         depth * 2, "",
         ToUtf8(ClassOf(h)).c_str(), (void*)h,
         r.left, r.top, r.right, r.bottom, (unsigned long)style, (unsigned long)ex,
         (style & WS_VISIBLE) ? "VISIBLE" : "hidden",
         (style & WS_CHILD) ? " CHILD" : "",
         ToUtf8(title).c_str());

    if (depth >= maxDepth) return;

    std::vector<HWND> children;
    for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        children.push_back(c);
    }
    int n = 0;
    for (HWND c : children) {
        if (n++ >= kMaxChildrenPerNode) {
            LogF("%*s... (+%d more children)", (depth + 1) * 2, "", (int)children.size() - kMaxChildrenPerNode);
            break;
        }
        DumpNode(c, depth + 1, maxDepth);
    }
}

}  // namespace

std::wstring ClassOf(HWND h) {
    if (!h) return L"";
    wchar_t buf[256] = {};
    GetClassNameW(h, buf, 255);
    return buf;
}

std::wstring TitleOf(HWND h) {
    if (!h) return L"";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s((size_t)len + 1, L'\0');
    int got = GetWindowTextW(h, s.data(), len + 1);
    s.resize(got > 0 ? (size_t)got : 0);
    return s;
}

std::wstring ChainOf(HWND h) {
    std::vector<std::wstring> parts;
    for (HWND cur = h; cur; cur = GetParent(cur)) parts.push_back(ClassOf(cur));
    std::wstring s;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += L" <- ";
        s += parts[i];
    }
    return s;
}

std::vector<HWND> TopLevels() {
    std::vector<HWND> v;
    EnumCtx ctx{&v};
    EnumWindows(EnumTopProc, reinterpret_cast<LPARAM>(&ctx));
    return v;
}

std::vector<HWND> TopLevelsOfClass(const wchar_t* cls) {
    std::vector<HWND> v;
    for (HWND h : TopLevels()) {
        if (ClassOf(h) == cls) v.push_back(h);
    }
    return v;
}

bool HasDescendantOfClass(HWND root, const wchar_t* cls) {
    if (!root) return false;
    if (ClassOf(root) == cls) return true;
    std::vector<HWND> kids;
    EnumCtx ctx{&kids};
    EnumChildWindows(root, EnumChildProc, reinterpret_cast<LPARAM>(&ctx));
    for (HWND h : kids) {
        if (ClassOf(h) == cls) return true;
    }
    return false;
}

HWND FindClassAnywhere(const wchar_t* cls) {
    for (HWND top : TopLevels()) {
        if (ClassOf(top) == cls) return top;
        std::vector<HWND> kids;
        EnumCtx ctx{&kids};
        EnumChildWindows(top, EnumChildProc, reinterpret_cast<LPARAM>(&ctx));
        for (HWND h : kids) {
            if (ClassOf(h) == cls) return h;
        }
    }
    return nullptr;
}

void DumpWindowTree(int maxDepth) {
    LogF("[tree] ---- top-level window tree (depth<=%d) ----", maxDepth);
    for (HWND top : TopLevels()) {
        DumpNode(top, 0, maxDepth);
    }
    LogF("[tree] ---- end ----");
}

HostInfo DiscoverDesktopHost() {
    HostInfo info;
    info.progman = FindWindowW(L"Progman", nullptr);
    // 实测本机存在多个 SHELLDLL_DefView：远程桌面外壳 TscShellContainerClass 里也有一套，
    // 它的 SysListView32 只有 426x91。所以“取第一个匹配”是错的，
    // 必须以“SysListView32 面积最大（覆盖整屏）= 真正的桌面图标列表”为准。
    long long bestArea = -1;
    for (HWND top : TopLevels()) {
        std::vector<HWND> kids;
        EnumCtx ctx{&kids};
        EnumChildWindows(top, EnumChildProc, reinterpret_cast<LPARAM>(&ctx));
        for (HWND h : kids) {
            if (ClassOf(h) != L"SHELLDLL_DefView") continue;
            HWND lv = FindWindowExW(h, nullptr, L"SysListView32", nullptr);
            if (!lv) continue;
            RECT r{};
            GetWindowRect(lv, &r);
            long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
            if (area > bestArea) {
                bestArea = area;
                info.defview = h;
                info.listview = lv;
            }
        }
    }
    if (info.defview) info.defviewParent = GetParent(info.defview);
    if (info.listview) {
        info.listviewChain = ChainOf(info.listview);
        GetWindowRect(info.listview, &info.listviewRect);
        GetClientRect(info.listview, &info.listviewClient);

        DWORD_PTR res = 0;
        SetLastError(0);
        LRESULT ok = SendMessageTimeoutW(info.listview, LVM_GETITEMCOUNT, 0, 0,
                                         SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, &res);
        if (ok) {
            info.iconCount = (long long)res;  // 非 0 说明跨进程消息没被 UIPI 拦住
            info.iconCountErr = 0;
        } else {
            info.iconCount = -1;
            info.iconCountErr = GetLastError();
        }
    }

    // “图标之下、壁纸之上”的绘制层。实测本机有 14 个 WorkerW，绝大多数是 136x39 的隐藏窗口，
    // 还有一个是含 DefView 的图标宿主。正确目标是：可见 + 覆盖整屏 + 不含 SHELLDLL_DefView。
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    for (HWND w : TopLevelsOfClass(L"WorkerW")) {
        if (HasDescendantOfClass(w, L"SHELLDLL_DefView")) continue;
        if (!(GetWindowLongW(w, GWL_STYLE) & WS_VISIBLE)) continue;
        RECT r{};
        GetWindowRect(w, &r);
        if ((r.right - r.left) < screenW / 2 || (r.bottom - r.top) < screenH / 2) continue;
        info.wallpaperWorker = w;
        break;
    }
    return info;
}

void LogHostInfo(const HostInfo& info) {
    LogF("[host] Progman=%p", (void*)info.progman);
    LogF("[host] SHELLDLL_DefView=%p parent=%p", (void*)info.defview, (void*)info.defviewParent);
    LogF("[host] SysListView32=%p chain=%s", (void*)info.listview, ToUtf8(info.listviewChain).c_str());
    LogF("[host] listview rect=(%ld,%ld,%ld,%ld) client=%ldx%ld",
         info.listviewRect.left, info.listviewRect.top, info.listviewRect.right, info.listviewRect.bottom,
         info.listviewClient.right, info.listviewClient.bottom);
    if (info.iconCount >= 0) {
        LogF("[host] icon count = %lld  (跨进程消息可通)", info.iconCount);
    } else {
        LogF("[host] icon count = FAILED lastError=%lu (可能被 UIPI 拦截或非图标列表)",
             (unsigned long)info.iconCountErr);
    }
    LogF("[host] wallpaper WorkerW (盒子绘制层) = %p", (void*)info.wallpaperWorker);
}

}  // namespace db
