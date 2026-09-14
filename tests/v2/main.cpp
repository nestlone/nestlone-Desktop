// v2 诊断变体：只做桌面宿主探测（FindWindow Progman / FindWindowEx SHELLDLL_DefView
// / SendMessageTimeout 0x052C）。不做窗口枚举。
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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND defview = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    DWORD_PTR res = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);
    LogV(L"[v2] progman-bits RAN progman=%p defview=%p res=%llu",
         (void*)progman, (void*)defview, (unsigned long long)res);
    return 0;
}
