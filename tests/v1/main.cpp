// v1 诊断变体：只做窗口枚举（EnumWindows / GetClassNameW / GetWindowRect）
// 不含 Progman、不含 0x052C、不含任何桌面类名字符串。
#include <windows.h>

#include <cstdarg>
#include <cstdio>

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

static int g_top = 0;

static BOOL CALLBACK OnTop(HWND h, LPARAM) {
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 127);
    RECT r{};
    GetWindowRect(h, &r);
    g_top++;
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    EnumWindows(OnTop, 0);
    LogV(L"[v1] window-enum-only RAN, top-level windows = %d", g_top);
    return 0;
}
