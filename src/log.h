#pragma once
#include <windows.h>
#include <string>

// 极简日志：追加写 %APPDATA%\\nestlone-desktop\\nestlone-D.log（UTF-8）。
// 之所以先做日志而不是 UI：Windows 桌面程序没有控制台，
// 日志文件是 WSL 侧唯一能回读的验证通道。
namespace db {

void LogInit();
void LogClose();
void LogF(const char* fmt, ...);          // printf 风格，UTF-8 输入
void LogLine(const std::wstring& s);
std::string ToUtf8(const std::wstring& w);
std::wstring LogFilePath();

}  // namespace db
