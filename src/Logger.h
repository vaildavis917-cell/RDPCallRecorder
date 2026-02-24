#pragma once

#include <string>
#include <atomic>

enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

extern std::atomic<LogLevel> g_logLevel;

void InitLogger();
void Log(const std::wstring& message, LogLevel level = LogLevel::LOG_INFO);
void UpdateLoggerConfig();
void CloseLogFile();
