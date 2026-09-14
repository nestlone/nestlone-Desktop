#include "log.h"

#include <cstdarg>
#include <cstdio>

namespace db {
namespace {

FILE* g_log = nullptr;

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring(L"C:\\");
    dir += L"\\nestlone-desktop";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

}  // namespace

std::wstring LogFilePath() { return AppDataDir() + L"\\nestlone-D.log"; }

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), need, nullptr, nullptr);
    return out;
}

void LogInit() {
    if (g_log) return;
    _wfopen_s(&g_log, LogFilePath().c_str(), L"a, ccs=UTF-8");
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(g_log,
             L"\n===== nestlone-D start %04d-%02d-%02d %02d:%02d:%02d pid=%lu =====\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             GetCurrentProcessId());
    fflush(g_log);
}

void LogClose() {
    if (g_log) {
        fclose(g_log);
        g_log = nullptr;
    }
}

void LogLine(const std::wstring& s) {
    if (!g_log) return;
    fwprintf(g_log, L"%s\n", s.c_str());
    fflush(g_log);
}

void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    // 统一走 UTF-8 -> UTF-16，避免依赖进程 locale
    int need = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
    if (need > 0) {
        std::wstring w((size_t)need, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, w.data(), need);
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        fwprintf(g_log, L"%s\n", w.c_str());
    }
    fflush(g_log);
}

}  // namespace db
