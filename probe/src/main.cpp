// 诊断探针：内容完全无害（只写一个日志文件，不碰任何桌面/窗口 API）。
// 用途：区分“本机新编译的 exe 一律被拦”与“只有某些内容的 exe 被拦”。
#include <windows.h>
#include <cstdio>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t dir[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (n == 0) return 1;
    wcscat_s(dir, L"\\DeskBox");
    CreateDirectoryW(dir, nullptr);

    wchar_t path[MAX_PATH] = {};
    wcscpy_s(path, dir);
    wcscat_s(path, L"\\probe_plain.log");

    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a, ccs=UTF-8");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d] trivial probe RAN from %s (pid=%lu)\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 self, GetCurrentProcessId());
        fclose(f);
    }
    return 0;
}
