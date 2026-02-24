#include "Logger.h"
#include "Config.h"
#include "Utils.h"
#include "Globals.h"
#include "MainPanel.h"
#include <fstream>
#include <chrono>
#include <mutex>
#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

std::atomic<LogLevel> g_logLevel(LogLevel::LOG_INFO);

// Bug 16: Changed from std::wofstream to std::ofstream with UTF-8 encoding.
// std::wofstream with default "C" locale cannot convert non-ASCII characters
// (Cyrillic usernames, paths, etc.), causing the stream to enter fail state
// and silently drop ALL subsequent log output.
static std::ofstream g_logFile;
static std::wstring g_logFilePath;
static std::mutex g_logMutex;

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "(conversion error)";
    std::string result(needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], needed, nullptr, nullptr);
    return result;
}

// Bug 9: cached config values to avoid GetConfigSnapshot() on every Log() call
static std::atomic<bool> g_loggingEnabled(true);
static std::atomic<int> g_maxLogSizeMB(10);

// Bug 14: periodic flush counter
static int g_flushCounter = 0;

// Log directory is next to the exe: {ExeDir}/logs/
static fs::path GetLogDir() {
    return fs::path(GetExePath()).parent_path() / L"logs";
}

static void EnsureLogFileOpen(const fs::path& logDir) {
    fs::path logFile = logDir / L"agent.log";
    std::wstring logPath = logFile.wstring();

    if (g_logFile.is_open() && g_logFilePath == logPath) {
        try {
            if (fs::exists(logFile)) {
                auto fileSize = fs::file_size(logFile);
                auto maxSize = static_cast<uintmax_t>(g_maxLogSizeMB.load(std::memory_order_relaxed)) * 1024 * 1024;
                if (fileSize > maxSize) {
                    g_logFile.close();
                    fs::path backupLog = logDir / L"agent.log.old";
                    try {
                        if (fs::exists(backupLog)) fs::remove(backupLog);
                        fs::rename(logFile, backupLog);
                    } catch (...) {}
                }
            }
        } catch (...) {}
    }

    if (!g_logFile.is_open()) {
        try { fs::create_directories(logDir); } catch (...) {}
        // Bug 16: use fs::path overload to handle non-ASCII paths correctly
        // (narrow ofstream::open with UTF-8 string won't work — Windows uses ANSI codepage)
        bool isNewFile = !fs::exists(logFile);
        g_logFile.open(logFile, std::ios::app | std::ios::binary);
        g_logFilePath = logPath;
        // Write UTF-8 BOM for new files so text editors detect encoding
        if (isNewFile && g_logFile.is_open()) {
            g_logFile.write("\xEF\xBB\xBF", 3);
        }
    }
}

void InitLogger() {
    // Create log directory and open log file immediately at startup
    // Logs are stored next to the exe: {InstallDir}\logs\agent.log
    try {
        fs::path logDir = GetLogDir();
        fs::create_directories(logDir);

        std::lock_guard<std::mutex> lock(g_logMutex);
        EnsureLogFileOpen(logDir);
    } catch (...) {
        // Silently fail — Log() will retry later
    }
}

// Bug 9: Called from MonitorThread to update cached config values
void UpdateLoggerConfig() {
    AgentConfig config = GetConfigSnapshot();
    g_loggingEnabled.store(config.enableLogging, std::memory_order_relaxed);
    g_maxLogSizeMB.store(config.maxLogSizeMB, std::memory_order_relaxed);
}

void Log(const std::wstring& message, LogLevel level) {
    // Bug 9: use cached atomic instead of GetConfigSnapshot()
    if (!g_loggingEnabled.load(std::memory_order_relaxed)) return;
    if (level < g_logLevel.load(std::memory_order_relaxed)) return;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tmNow;
    localtime_s(&tmNow, &time_t_now);

    std::wstring levelStr;
    switch (level) {
        case LogLevel::LOG_DEBUG: levelStr = L"DEBUG"; break;
        case LogLevel::LOG_INFO:  levelStr = L"INFO "; break;
        case LogLevel::LOG_WARN:  levelStr = L"WARN "; break;
        case LogLevel::LOG_ERROR: levelStr = L"ERROR"; break;
    }

    wchar_t timeStr[LOG_TIMESTAMP_BUF];
    swprintf_s(timeStr, LOG_TIMESTAMP_BUF, L"[%04d-%02d-%02d %02d:%02d:%02d]",
        tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
        tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    std::wstring logLine = std::wstring(timeStr) + L" [" + levelStr + L"] " + message + L"\r\n";

    // Push to UI ring buffer (thread-safe, no lock contention with file I/O)
    g_statusData.PushLogLine(logLine);

    std::lock_guard<std::mutex> lock(g_logMutex);
    fs::path logDir = GetLogDir();
    try {
        EnsureLogFileOpen(logDir);
        if (g_logFile.is_open()) {
            // Bug 16: convert to UTF-8 and write to narrow ofstream
            std::string utf8Line = WideToUtf8(logLine);
            g_logFile.write(utf8Line.c_str(), utf8Line.size());
            // Bug 14: flush every 10 lines or immediately on WARN/ERROR
            if (level >= LogLevel::LOG_WARN || ++g_flushCounter >= 10) {
                g_logFile.flush();
                g_flushCounter = 0;
            }
        }
    } catch (const std::exception&) {}
}

void CloseLogFile() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.flush();
        g_logFile.close();
    }
}
