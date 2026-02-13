#pragma once

#include <string>
#include <vector>
#include <windows.h>

std::wstring Utf8ToWide(const std::string& str);
std::vector<std::wstring> SplitString(const std::wstring& str, wchar_t delimiter);
std::wstring SanitizeForPath(const std::wstring& name);
std::wstring GetDefaultRecordingPath();
std::wstring GetExePath();
std::wstring GetConfigPath();

enum class LogLevel;
LogLevel ParseLogLevel(const std::wstring& level);
void SetProcessPriorityFromConfig(const std::wstring& priority);

std::wstring GetCurrentFullName();
std::wstring GetCurrentLoginName();
void RegisterAutoStart();
