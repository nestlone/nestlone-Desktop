#include "BoxModel.h"
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace nestlone {
namespace {
std::wstring Escape(const std::wstring& value) {
    std::wstring out;
    for (wchar_t c : value) {
        if (c == L'\\' || c == L'"') out += L'\\';
        if (c == L'\n') { out += L"\\n"; continue; }
        if (c == L'\r') { out += L"\\r"; continue; }
        out += c;
    }
    return out;
}

bool ReadUtf8(const std::wstring& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), {});
    return true;
}

bool WriteUtf8(const std::wstring& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}

bool ReadNumber(const std::wstring& object, const wchar_t* name, long& value) {
    const std::wstring key = std::wstring(L"\"") + name + L"\":";
    size_t at = object.find(key);
    if (at == std::wstring::npos) return false;
    at += key.size();
    wchar_t* end = nullptr;
    const long parsed = wcstol(object.c_str() + at, &end, 10);
    if (end == object.c_str() + at) return false;
    value = parsed;
    return true;
}

bool ReadString(const std::wstring& object, const wchar_t* name, std::wstring& value) {
    const std::wstring key = std::wstring(L"\"") + name + L"\":\"";
    size_t at = object.find(key);
    if (at == std::wstring::npos) return false;
    at += key.size();
    std::wstring out;
    bool escaped = false;
    for (; at < object.size(); ++at) {
        const wchar_t c = object[at];
        if (!escaped && c == L'"') { value = out; return true; }
        if (!escaped && c == L'\\') { escaped = true; continue; }
        if (escaped && c == L'n') out += L'\n';
        else if (escaped && c == L'r') out += L'\r';
        else out += c;
        escaped = false;
    }
    return false;
}

std::vector<std::wstring> ReadItems(const std::wstring& object) {
    std::vector<std::wstring> items;
    size_t at = object.find(L"\"items\":[");
    if (at == std::wstring::npos) return items;
    at = object.find(L'[', at) + 1;
    while (at < object.size()) {
        while (at < object.size() && (object[at] == L' ' || object[at] == L',')) ++at;
        if (at >= object.size() || object[at] == L']') break;
        if (object[at++] != L'"') break;
        std::wstring item;
        bool escaped = false;
        while (at < object.size()) {
            wchar_t c = object[at++];
            if (!escaped && c == L'"') break;
            if (!escaped && c == L'\\') { escaped = true; continue; }
            item += escaped && c == L'n' ? L'\n' : (escaped && c == L'r' ? L'\r' : c);
            escaped = false;
        }
        if (!item.empty()) items.push_back(std::move(item));
    }
    return items;
}

std::wstring ObjectAt(const std::wstring& json, size_t begin, size_t& next) {
    int depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = begin; i < json.size(); ++i) {
        wchar_t c = json[i];
        if (quoted) { if (!escaped && c == L'"') quoted = false; escaped = !escaped && c == L'\\'; if (c != L'\\') escaped = false; continue; }
        if (c == L'"') { quoted = true; continue; }
        if (c == L'{') ++depth;
        if (c == L'}' && --depth == 0) { next = i + 1; return json.substr(begin, i - begin + 1); }
    }
    next = json.size();
    return {};
}
}

std::wstring LayoutPath() {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::filesystem::path directory = std::filesystem::path(appData) / L"nestlone-desktop";
    CoTaskMemFree(appData);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return (directory / L"layout.json").wstring();
}

bool AddItem(Box& box, const std::wstring& input) {
    std::error_code error;
    std::filesystem::path path(input);
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    const std::wstring normalized = (error ? path : absolute).lexically_normal().wstring();
    if (normalized.empty()) return false;
    const auto existing = std::find_if(box.items.begin(), box.items.end(), [&](const std::wstring& item) { return _wcsicmp(item.c_str(), normalized.c_str()) == 0; });
    if (existing != box.items.end()) return false;
    box.items.push_back(normalized);
    return true;
}

Layout LoadLayout() {
    Layout layout;
    std::string bytes;
    if (!ReadUtf8(LayoutPath(), bytes)) return layout;
    const std::wstring json = ToWide(bytes);
    if (json.empty()) return layout;
    long savedOpacity = layout.opacity;
    if (ReadNumber(json, L"opacity", savedOpacity)) layout.opacity = static_cast<int>(std::clamp(savedOpacity, 20L, 100L));
    size_t at = 0;
    while ((at = json.find(L"{\"id\":", at)) != std::wstring::npos) {
        size_t next = at;
        const std::wstring object = ObjectAt(json, at, next);
        at = next;
        Box box;
        long left, top, right, bottom, color, collapsed;
        if (!ReadString(object, L"id", box.id) || !ReadString(object, L"title", box.title) ||
            !ReadNumber(object, L"left", left) || !ReadNumber(object, L"top", top) ||
            !ReadNumber(object, L"right", right) || !ReadNumber(object, L"bottom", bottom) ||
            !ReadNumber(object, L"color", color) || !ReadNumber(object, L"collapsed", collapsed) || right - left < 140 || bottom - top < 70) continue;
        box.rect = {left, top, right, bottom};
        box.color = static_cast<COLORREF>(color);
        box.collapsed = collapsed != 0;
        long iconView = 0;
        if (ReadNumber(object, L"iconView", iconView)) box.iconView = iconView != 0;
        for (const auto& item : ReadItems(object)) AddItem(box, item);
        layout.boxes.push_back(std::move(box));
    }
    return layout;
}

bool SaveLayout(const Layout& layout) {
    const std::wstring path = LayoutPath();
    if (path.empty()) return false;
    const int opacity = std::clamp(layout.opacity, 20, 100);
    std::wstring json = L"{\"version\":3,\"opacity\":" + std::to_wstring(opacity) + L",\"boxes\":[";
    for (size_t i = 0; i < layout.boxes.size(); ++i) {
        const Box& box = layout.boxes[i];
        if (i) json += L',';
        json += L"{\"id\":\"" + Escape(box.id) + L"\",\"title\":\"" + Escape(box.title) + L"\",\"left\":" + std::to_wstring(box.rect.left) + L",\"top\":" + std::to_wstring(box.rect.top) + L",\"right\":" + std::to_wstring(box.rect.right) + L",\"bottom\":" + std::to_wstring(box.rect.bottom) + L",\"color\":" + std::to_wstring(static_cast<unsigned long>(box.color)) + L",\"collapsed\":" + std::to_wstring(box.collapsed ? 1 : 0) + L",\"iconView\":" + std::to_wstring(box.iconView ? 1 : 0) + L",\"items\":[";
        for (size_t j = 0; j < box.items.size(); ++j) { if (j) json += L','; json += L"\"" + Escape(box.items[j]) + L"\""; }
        json += L"]}";
    }
    json += L"]}";
    const std::wstring temporary = path + L".tmp";
    if (!WriteUtf8(temporary, ToUtf8(json))) return false;
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
}
