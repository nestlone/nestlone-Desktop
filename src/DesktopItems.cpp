#include "DesktopItems.h"

#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace nestlone {

std::wstring DesktopDirectory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &path))) return {};
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

std::vector<DesktopItem> EnumerateDesktopItems() {
    std::vector<DesktopItem> items;
    const std::wstring root = DesktopDirectory();
    if (root.empty()) return items;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        DesktopItem item;
        item.path = entry.path().wstring();
        item.name = entry.path().filename().wstring();
        item.directory = entry.is_directory(ec);
        item.type = item.directory ? L"文件夹" : entry.path().extension().wstring();
        if (item.type.empty() && !item.directory) item.type = L"文件";
        std::filesystem::file_time_type t = entry.last_write_time(ec);
        if (!ec) {
            auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const auto value = std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, 10000000>>>(sys.time_since_epoch()).count();
            ULARGE_INTEGER ft{};
            ft.QuadPart = static_cast<ULONGLONG>(value) + 116444736000000000ULL;
            item.modified.dwLowDateTime = ft.LowPart;
            item.modified.dwHighDateTime = ft.HighPart;
        }
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const DesktopItem& a, const DesktopItem& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return items;
}

bool OpenItem(const DesktopItem& item) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", item.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool RevealItem(const DesktopItem& item) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(item.path.c_str(), nullptr, &pidl, 0, nullptr))) return false;
    const bool ok = SUCCEEDED(SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0));
    CoTaskMemFree(pidl);
    return ok;
}

bool DeleteItem(const DesktopItem& item) {
    SHFILEOPSTRUCTW op{};
    std::wstring path = item.path;
    path.push_back(L'\0');
    op.wFunc = FO_DELETE;
    op.pFrom = path.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}


std::wstring FormatModified(const FILETIME& time) {
    if (time.dwHighDateTime == 0 && time.dwLowDateTime == 0) return L"-";
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"-";
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
    return buf;
}

}
