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

LogLevel g_logLevel = LogLevel::LOG_INFO;

static std::wofstream g_logFile;
static std::wstring g_logFilePath;
static std::mutex g_logMutex;

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
        g_logFile.open(logPath, std::ios::app);
        g_logFilePath = logPath;
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
    if (level < g_logLevel) return;

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
            // Write \n to file (not \r\n — file uses Unix line endings)
            std::wstring fileLine = std::wstring(timeStr) + L" [" + levelStr + L"] " + message + L"\n";
            g_logFile << fileLine;
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
