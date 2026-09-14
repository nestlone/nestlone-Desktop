// v3 诊断变体：只有类名字符串，不调用任何窗口 API（连 user32 的窗口函数都不碰）。
// 用来判断拦截是否纯静态字符串特征。
#include <windows.h>

#include <cstdarg>
#include <cstdio>

static const wchar_t* kCls[] = {
    L"Progman",
    L"SHELLDLL_DefView",
    L"WorkerW",
    L"SysListView32",
};

static void LogV(const wchar_t* fmt, ...) {
    wchar_t path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH)) return;
    wcscat_s(path, L"\\DeskBox");
    CreateDirectoryW(path, nullptr);
    wcscat_s(path, L"\\vtest.log");
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a, ccs=UTF-8");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fwprintf(f, L"\n");
    fclose(f);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    size_t n = 0;
    for (const wchar_t* s : kCls) n += wcslen(s);
    LogV(L"[v3] strings-only RAN (sum of name lengths = %zu)", n);
    return 0;
}
