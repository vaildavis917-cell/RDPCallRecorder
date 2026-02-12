// main.cpp - RDP Call Recorder Agent v2.5
// Records WhatsApp/Telegram/Viber calls automatically in RDP sessions.
// Uses AudioCapture library (https://github.com/masonasons/AudioCapture)
//
// Features:
//   - Records BOTH voices (caller + callee) using mixed recording
//   - System tray icon with context menu
//   - Settings window (change recording folder, format, etc.)
//   - On first launch shows settings dialog
//   - If launched again, opens settings in running instance
//   - Auto-registers in startup on first run
//   - Desktop shortcut created by installer
//
// v2.5 Changes:
//   - [C1] Thread-safe config access via mutex
//   - [C2] Fixed GDI font handle leak in Settings dialog
//   - [C3] Fixed lossy string conversion (now uses MultiByteToWideChar)
//   - [C4] Fixed simultaneous calls breaking mixed recording
//   - [C5] Fixed Global mutex blocking all RDP sessions (now Local)
//   - [H1] Cached COM objects in CheckProcessRealAudio
//   - [H2] Cached GetCurrentFullName result
//   - [H3] Buffered log file (kept open)
//   - [H4] Cleanup of stale map entries for disappeared processes
//   - [H5] Fixed ExpandEnvironmentStringsW return value check
//   - [M2] Added INI value validation
//   - [M3] SanitizeForPath now handles leading dots
//   - [M5] Added all forward declarations
//   - [M6] Removed unused #include <iostream>
//   - [L2] Magic numbers moved to named constants
//   - [L3] COM managed via ComPtr (RAII)

#include "CaptureManager.h"
#include "ProcessEnumerator.h"
#include "AudioDeviceEnumerator.h"
#include "resource.h"
#include <windows.h>
#include <roapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>  // [L3] ComPtr for RAII COM management
#define SECURITY_WIN32
#include <security.h>  // for GetUserNameExW (NameDisplay = Full Name)

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "secur32.lib")

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;  // [L3] RAII COM pointers

// ============================================================
// Constants [L2] — no more magic numbers
// ============================================================
#define WM_TRAYICON        (WM_USER + 1)
#define WM_SHOW_SETTINGS   (WM_USER + 2)
#define IDI_TRAY           1
#define IDM_SETTINGS       1001
#define IDM_OPEN_FOLDER    1002
#define IDM_STATUS         1003
#define IDM_EXIT           1004

static const wchar_t* WINDOW_CLASS_NAME = L"RDPCallRecorderWndClass";
static const wchar_t* APP_TITLE = L"RDP Call Recorder";

// [C5] FIX: Changed from Global to Local — per RDP session, not per server
static const wchar_t* MUTEX_SINGLE_INSTANCE = L"Local\\RDPCallRecorder_SingleInstance";

// Custom message name for IPC (second instance -> first instance)
static UINT WM_OPEN_SETTINGS_MSG = 0;

// Fake session ID for microphone capture (must not collide with real PIDs)
static const DWORD MIC_SESSION_ID_BASE = 0xF0000000;

// [L2] Named constants for magic numbers
static const float AUDIO_PEAK_THRESHOLD = 0.01f;      // Minimum peak level to consider "real audio"
static const int INI_BUFFER_SIZE = 1024;               // Buffer size for INI string reads
static const int NAME_BUFFER_SIZE = 256;               // Buffer size for user name
static const int TRAY_TIP_MAX_LEN = 128;               // Max tooltip length
static const int LOG_TIMESTAMP_BUF = 64;               // Timestamp buffer size
static const int SETTINGS_DLG_WIDTH = 500;
static const int SETTINGS_DLG_HEIGHT = 420;
static const UINT32 MIN_MP3_BITRATE = 32000;
static const UINT32 MAX_MP3_BITRATE = 320000;

// ============================================================
// Global configuration
// ============================================================
struct AgentConfig {
    std::wstring recordingPath = L"";  // Will be set to %USERPROFILE%\CallRecordings at runtime
    std::wstring audioFormat = L"mp3";
    UINT32 mp3Bitrate = 128000;
    int pollIntervalSeconds = 2;
    int silenceThreshold = 3;
    int startThreshold = 2;  // How many consecutive polls with audio before starting recording
    std::vector<std::wstring> targetProcesses = { L"WhatsApp.exe", L"Telegram.exe", L"Viber.exe" };
    bool enableLogging = true;
    std::wstring logLevel = L"INFO";
    int maxLogSizeMB = 10;
    bool hideConsole = true;
    bool useMutex = true;
    std::wstring mutexName = L"Local\\RDPCallRecorderAgentMutex";  // [C5] Local, not Global
    std::wstring processPriority = L"BelowNormal";
    bool autoRegisterStartup = true;
};

AgentConfig g_config;
std::mutex g_configMutex;  // [C1] FIX: Thread-safe config access

// Helper to safely read config fields from monitor thread
AgentConfig GetConfigSnapshot() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;  // Copy
}

// ============================================================
// Global state
// ============================================================
HWND g_hWndMain = nullptr;
NOTIFYICONDATAW g_nid = {};
HANDLE g_hMutex = nullptr;
std::atomic<bool> g_running(true);
std::atomic<int> g_activeRecordings(0);
std::thread g_monitorThread;

// ============================================================
// Log levels
// ============================================================
enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

LogLevel g_logLevel = LogLevel::LOG_INFO;

// [H2] FIX: Cached user names — syscall only once
static std::wstring g_cachedFullName;
static std::wstring g_cachedLoginName;
static bool g_namesCached = false;

// ============================================================
// Forward declarations [M5] — complete list
// ============================================================
void ShowSettingsDialog(HWND hParent);
void MonitorThread();
void SaveConfig();
bool LoadConfig(AgentConfig& config);
void Log(const std::wstring& message, LogLevel level = LogLevel::LOG_INFO);
void RegisterAutoStart();
void CreateTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void UpdateTrayTooltip();
void ShowTrayMenu(HWND hWnd);
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// String conversion helper [C3] FIX: proper UTF-8 → wstring
// ============================================================
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (needed <= 0) {
        // Fallback: try ACP (system codepage)
        needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), nullptr, 0);
        if (needed <= 0) return L"(conversion error)";
        std::wstring result(needed, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &result[0], needed);
        return result;
    }
    std::wstring result(needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], needed);
    return result;
}

// ============================================================
// INI utilities
// ============================================================
std::wstring GetIniString(const std::wstring& section, const std::wstring& key,
                          const std::wstring& defaultValue, const std::wstring& iniPath) {
    wchar_t buffer[INI_BUFFER_SIZE];
    GetPrivateProfileStringW(section.c_str(), key.c_str(), defaultValue.c_str(),
                             buffer, INI_BUFFER_SIZE, iniPath.c_str());
    return std::wstring(buffer);
}

int GetIniInt(const std::wstring& section, const std::wstring& key,
              int defaultValue, const std::wstring& iniPath) {
    return GetPrivateProfileIntW(section.c_str(), key.c_str(), defaultValue, iniPath.c_str());
}

bool GetIniBool(const std::wstring& section, const std::wstring& key,
                bool defaultValue, const std::wstring& iniPath) {
    std::wstring val = GetIniString(section, key, defaultValue ? L"true" : L"false", iniPath);
    std::transform(val.begin(), val.end(), val.begin(), ::towlower);
    return (val == L"true" || val == L"1" || val == L"yes");
}

std::vector<std::wstring> SplitString(const std::wstring& str, wchar_t delimiter) {
    std::vector<std::wstring> tokens;
    std::wstringstream ss(str);
    std::wstring token;
    while (std::getline(ss, token, delimiter)) {
        size_t start = token.find_first_not_of(L" \t");
        size_t end = token.find_last_not_of(L" \t");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}

std::wstring GetDefaultRecordingPath() {
    wchar_t userProfile[MAX_PATH] = { 0 };
    // [H5] FIX: Check that result fits in buffer (> 0 AND <= MAX_PATH)
    DWORD result = ExpandEnvironmentStringsW(L"%USERPROFILE%\\CallRecordings", userProfile, MAX_PATH);
    if (result > 0 && result <= MAX_PATH) {
        return std::wstring(userProfile);
    }
    // Fallback: use LOCALAPPDATA
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        return std::wstring(localAppData) + L"\\RDPCallRecorder\\Recordings";
    }
    return L"C:\\CallRecordings";
}

std::wstring GetExePath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::wstring(exePath);
}

std::wstring GetConfigPath() {
    fs::path configPath = fs::path(GetExePath()).parent_path() / L"config.ini";
    return configPath.wstring();
}

LogLevel ParseLogLevel(const std::wstring& level) {
    std::wstring upper = level;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    if (upper == L"DEBUG") return LogLevel::LOG_DEBUG;
    if (upper == L"INFO")  return LogLevel::LOG_INFO;
    if (upper == L"WARN")  return LogLevel::LOG_WARN;
    if (upper == L"ERROR") return LogLevel::LOG_ERROR;
    return LogLevel::LOG_INFO;
}

void SetProcessPriorityFromConfig(const std::wstring& priority) {
    std::wstring upper = priority;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    DWORD priorityClass = NORMAL_PRIORITY_CLASS;
    if (upper == L"IDLE")             priorityClass = IDLE_PRIORITY_CLASS;
    else if (upper == L"BELOWNORMAL") priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    else if (upper == L"NORMAL")      priorityClass = NORMAL_PRIORITY_CLASS;
    else if (upper == L"ABOVENORMAL") priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (upper == L"HIGH")        priorityClass = HIGH_PRIORITY_CLASS;
    SetPriorityClass(GetCurrentProcess(), priorityClass);
}

// [H2] Get user's Full Name — cached after first call
std::wstring GetCurrentFullName() {
    if (g_namesCached) return g_cachedFullName;

    wchar_t fullName[NAME_BUFFER_SIZE] = { 0 };
    ULONG size = NAME_BUFFER_SIZE;
    if (GetUserNameExW(NameDisplay, fullName, &size) && size > 1 && wcslen(fullName) > 0) {
        g_cachedFullName = std::wstring(fullName);
        g_namesCached = true;  // Will also cache login name below
    }

    if (g_cachedFullName.empty()) {
        // Final fallback: regular login name
        wchar_t username[NAME_BUFFER_SIZE] = { 0 };
        DWORD usize = NAME_BUFFER_SIZE;
        if (GetUserNameW(username, &usize)) {
            g_cachedFullName = std::wstring(username);
        } else {
            g_cachedFullName = L"Unknown";
        }
    }

    // Cache login name too while we're at it
    if (g_cachedLoginName.empty()) {
        wchar_t username[NAME_BUFFER_SIZE] = { 0 };
        DWORD usize = NAME_BUFFER_SIZE;
        if (GetUserNameW(username, &usize)) {
            g_cachedLoginName = std::wstring(username);
        } else {
            g_cachedLoginName = L"Unknown";
        }
    }
    g_namesCached = true;
    return g_cachedFullName;
}

// Get login name (for logging purposes) — cached
std::wstring GetCurrentLoginName() {
    if (g_namesCached) return g_cachedLoginName;
    GetCurrentFullName();  // This caches both
    return g_cachedLoginName;
}

// [M3] FIX: Sanitize name for use in file/folder names — now handles leading dots
std::wstring SanitizeForPath(const std::wstring& name) {
    std::wstring result;
    for (wchar_t ch : name) {
        // Replace invalid filename chars with underscore
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            result += L'_';
        } else {
            result += ch;
        }
    }
    // Trim leading dots (would create hidden directories on Windows)
    while (!result.empty() && result.front() == L'.') {
        result.erase(result.begin());
    }
    // Trim trailing spaces/dots
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) {
        result.pop_back();
    }
    if (result.empty()) result = L"Unknown";
    return result;
}

// ============================================================
// Load / Save configuration [C1] — now uses mutex
// ============================================================
bool LoadConfig(AgentConfig& config) {
    // Set default recording path based on user profile
    if (config.recordingPath.empty()) {
        config.recordingPath = GetDefaultRecordingPath();
    }

    std::wstring iniPath = GetConfigPath();
    if (!fs::exists(iniPath)) return false;

    std::lock_guard<std::mutex> lock(g_configMutex);

    config.recordingPath = GetIniString(L"Recording", L"RecordingPath", config.recordingPath, iniPath);
    config.audioFormat   = GetIniString(L"Recording", L"AudioFormat", config.audioFormat, iniPath);

    // [M2] FIX: Validate bitrate
    int rawBitrate = GetIniInt(L"Recording", L"MP3Bitrate", static_cast<int>(config.mp3Bitrate), iniPath);
    if (rawBitrate >= static_cast<int>(MIN_MP3_BITRATE) && rawBitrate <= static_cast<int>(MAX_MP3_BITRATE)) {
        config.mp3Bitrate = static_cast<UINT32>(rawBitrate);
    }

    config.pollIntervalSeconds = GetIniInt(L"Monitoring", L"PollInterval", config.pollIntervalSeconds, iniPath);
    config.silenceThreshold    = GetIniInt(L"Monitoring", L"SilenceThreshold", config.silenceThreshold, iniPath);
    config.startThreshold      = GetIniInt(L"Monitoring", L"StartThreshold", config.startThreshold, iniPath);
    // [M2] Validate ranges
    if (config.pollIntervalSeconds < 1) config.pollIntervalSeconds = 1;
    if (config.pollIntervalSeconds > 60) config.pollIntervalSeconds = 60;
    if (config.silenceThreshold < 1) config.silenceThreshold = 1;
    if (config.silenceThreshold > 100) config.silenceThreshold = 100;
    if (config.startThreshold < 1) config.startThreshold = 1;
    if (config.startThreshold > 100) config.startThreshold = 100;

    std::wstring processesStr = GetIniString(L"Processes", L"TargetProcesses", L"WhatsApp.exe,Telegram.exe,Viber.exe", iniPath);
    auto parsed = SplitString(processesStr, L',');
    if (!parsed.empty()) config.targetProcesses = parsed;

    config.enableLogging = GetIniBool(L"Logging", L"EnableLogging", config.enableLogging, iniPath);
    config.logLevel      = GetIniString(L"Logging", L"LogLevel", config.logLevel, iniPath);
    config.maxLogSizeMB  = GetIniInt(L"Logging", L"MaxLogSizeMB", config.maxLogSizeMB, iniPath);
    if (config.maxLogSizeMB < 1) config.maxLogSizeMB = 1;
    if (config.maxLogSizeMB > 1000) config.maxLogSizeMB = 1000;

    config.hideConsole         = GetIniBool(L"Advanced", L"HideConsole", config.hideConsole, iniPath);
    config.useMutex            = GetIniBool(L"Advanced", L"UseMutex", config.useMutex, iniPath);
    config.mutexName           = GetIniString(L"Advanced", L"MutexName", config.mutexName, iniPath);
    config.processPriority     = GetIniString(L"Advanced", L"ProcessPriority", config.processPriority, iniPath);
    config.autoRegisterStartup = GetIniBool(L"Advanced", L"AutoRegisterStartup", config.autoRegisterStartup, iniPath);

    return true;
}

void SaveConfig() {
    std::lock_guard<std::mutex> lock(g_configMutex);  // [C1]

    std::wstring iniPath = GetConfigPath();

    WritePrivateProfileStringW(L"Recording", L"RecordingPath", g_config.recordingPath.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Recording", L"AudioFormat", g_config.audioFormat.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Recording", L"MP3Bitrate", std::to_wstring(g_config.mp3Bitrate).c_str(), iniPath.c_str());

    WritePrivateProfileStringW(L"Monitoring", L"PollInterval", std::to_wstring(g_config.pollIntervalSeconds).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"SilenceThreshold", std::to_wstring(g_config.silenceThreshold).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"StartThreshold", std::to_wstring(g_config.startThreshold).c_str(), iniPath.c_str());

    std::wstring procStr;
    for (size_t i = 0; i < g_config.targetProcesses.size(); i++) {
        if (i > 0) procStr += L",";
        procStr += g_config.targetProcesses[i];
    }
    WritePrivateProfileStringW(L"Processes", L"TargetProcesses", procStr.c_str(), iniPath.c_str());

    WritePrivateProfileStringW(L"Logging", L"EnableLogging", g_config.enableLogging ? L"true" : L"false", iniPath.c_str());
    WritePrivateProfileStringW(L"Logging", L"LogLevel", g_config.logLevel.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Logging", L"MaxLogSizeMB", std::to_wstring(g_config.maxLogSizeMB).c_str(), iniPath.c_str());

    WritePrivateProfileStringW(L"Advanced", L"HideConsole", g_config.hideConsole ? L"true" : L"false", iniPath.c_str());
    WritePrivateProfileStringW(L"Advanced", L"AutoRegisterStartup", g_config.autoRegisterStartup ? L"true" : L"false", iniPath.c_str());
    WritePrivateProfileStringW(L"Advanced", L"ProcessPriority", g_config.processPriority.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Advanced", L"Configured", L"true", iniPath.c_str());
}

// ============================================================
// Auto-start registration
// ============================================================
void RegisterAutoStart() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS) return;

    wchar_t existingPath[MAX_PATH] = { 0 };
    DWORD dataSize = sizeof(existingPath);
    DWORD dataType = 0;
    result = RegQueryValueExW(hKey, L"RDPCallRecorder", nullptr, &dataType,
                              reinterpret_cast<LPBYTE>(existingPath), &dataSize);

    std::wstring exePath = GetExePath();
    if (result == ERROR_SUCCESS && dataType == REG_SZ) {
        if (_wcsicmp(existingPath, exePath.c_str()) == 0) {
            RegCloseKey(hKey);
            return;
        }
    }

    RegSetValueExW(hKey, L"RDPCallRecorder", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(exePath.c_str()),
                   static_cast<DWORD>((exePath.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
}

// ============================================================
// Logging [H3] — log file kept open, flushed periodically
// ============================================================
static std::wofstream g_logFile;
static std::wstring g_logFilePath;
static std::mutex g_logMutex;

void EnsureLogFileOpen(const fs::path& logDir) {
    fs::path logFile = logDir / L"agent.log";
    std::wstring logPath = logFile.wstring();

    if (g_logFile.is_open() && g_logFilePath == logPath) {
        // Check if rotation needed
        try {
            if (fs::exists(logFile)) {
                auto config = GetConfigSnapshot();
                auto fileSize = fs::file_size(logFile);
                auto maxSize = static_cast<uintmax_t>(config.maxLogSizeMB) * 1024 * 1024;
                if (fileSize > maxSize) {
                    g_logFile.close();
                    fs::path backupLog = logDir / L"agent.log.old";
                    try {
                        if (fs::exists(backupLog)) fs::remove(backupLog);
                        fs::rename(logFile, backupLog);
                    } catch (...) {}
                    // Will reopen below
                }
            }
        } catch (...) {}
    }

    if (!g_logFile.is_open()) {
        try {
            fs::create_directories(logDir);
        } catch (...) {}
        g_logFile.open(logPath, std::ios::app);
        g_logFilePath = logPath;
    }
}

void Log(const std::wstring& message, LogLevel level) {
    // Read config safely
    AgentConfig config = GetConfigSnapshot();
    if (!config.enableLogging) return;
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

    std::wstring logLine = std::wstring(timeStr) + L" [" + levelStr + L"] " + message + L"\n";

    std::lock_guard<std::mutex> lock(g_logMutex);

    fs::path logDir = fs::path(config.recordingPath) / SanitizeForPath(GetCurrentFullName()) / L"logs";
    try {
        EnsureLogFileOpen(logDir);
        if (g_logFile.is_open()) {
            g_logFile << logLine;
            // Flush only on WARN/ERROR for performance
            if (level >= LogLevel::LOG_WARN) {
                g_logFile.flush();
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

// ============================================================
// Audio format helpers
// ============================================================
AudioFormat GetAudioFormatFromConfig() {
    auto config = GetConfigSnapshot();
    std::wstring fmt = config.audioFormat;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::towlower);
    if (fmt == L"wav")  return AudioFormat::WAV;
    if (fmt == L"mp3")  return AudioFormat::MP3;
    if (fmt == L"opus") return AudioFormat::OPUS;
    if (fmt == L"flac") return AudioFormat::FLAC;
    return AudioFormat::MP3;
}

std::wstring GetFileExtension(AudioFormat format) {
    switch (format) {
        case AudioFormat::WAV:  return L".wav";
        case AudioFormat::MP3:  return L".mp3";
        case AudioFormat::OPUS: return L".opus";
        case AudioFormat::FLAC: return L".flac";
        default: return L".mp3";
    }
}

// File: {YYYY-MM-DD}_{RDP-user}_{App}_{HH-MM-SS}.mp3
std::wstring BuildOutputPath(const std::wstring& processName, AudioFormat format) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tmNow;
    localtime_s(&tmNow, &time_t_now);

    std::wstring username = SanitizeForPath(GetCurrentFullName());

    wchar_t dateStr[32];
    swprintf_s(dateStr, 32, L"%04d-%02d-%02d",
        tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);

    wchar_t timeStrBuf[32];
    swprintf_s(timeStrBuf, 32, L"%02d-%02d-%02d",
        tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    std::wstring appName = processName;
    size_t dotPos = appName.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        appName = appName.substr(0, dotPos);
    }

    auto config = GetConfigSnapshot();
    fs::path outputDir = fs::path(config.recordingPath) / username / dateStr;
    try {
        fs::create_directories(outputDir);
    } catch (const std::exception& e) {
        // [C3] FIX: proper string conversion
        Log(L"Failed to create directory: " + outputDir.wstring() + L" - " + Utf8ToWide(e.what()), LogLevel::LOG_ERROR);
        outputDir = fs::temp_directory_path();
    }

    std::wstring filename = std::wstring(dateStr) + L"_" + username + L"_" + appName + L"_" + std::wstring(timeStrBuf) + GetFileExtension(format);
    return (outputDir / filename).wstring();
}

// ============================================================
// Find target processes
// ============================================================
struct FoundProcess {
    DWORD pid;
    std::wstring name;
};

std::vector<FoundProcess> FindTargetProcesses(const AgentConfig& config) {
    std::vector<FoundProcess> result;
    DWORD currentSessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            for (const auto& target : config.targetProcesses) {
                if (_wcsicmp(pe32.szExeFile, target.c_str()) == 0) {
                    DWORD processSessionId = 0;
                    ProcessIdToSessionId(pe32.th32ProcessID, &processSessionId);
                    if (processSessionId == currentSessionId) {
                        FoundProcess fp;
                        fp.pid = pe32.th32ProcessID;
                        fp.name = pe32.szExeFile;
                        result.push_back(fp);
                    }
                }
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return result;
}

// ============================================================
// [H1] Cached audio session monitor — avoids COM re-creation every poll
// ============================================================
class AudioSessionMonitor {
public:
    AudioSessionMonitor() = default;
    ~AudioSessionMonitor() = default;  // ComPtr handles Release

    bool CheckProcessRealAudio(DWORD processId, float threshold = AUDIO_PEAK_THRESHOLD) {
        // Re-initialize COM objects if needed
        if (!EnsureInitialized()) return false;

        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        HRESULT hr = m_sessionManager->GetSessionEnumerator(&sessionEnumerator);
        if (FAILED(hr) || !sessionEnumerator) {
            // Session manager may be stale, reset and retry next poll
            Reset();
            return false;
        }

        int sessionCount = 0;
        sessionEnumerator->GetCount(&sessionCount);

        for (int i = 0; i < sessionCount; i++) {
            ComPtr<IAudioSessionControl> sessionControl;
            if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl)
                continue;

            ComPtr<IAudioSessionControl2> sessionControl2;
            if (SUCCEEDED(sessionControl.As(&sessionControl2))) {
                DWORD sessionProcessId = 0;
                if (SUCCEEDED(sessionControl2->GetProcessId(&sessionProcessId))) {
                    if (sessionProcessId == processId) {
                        ComPtr<IAudioMeterInformation> meter;
                        if (SUCCEEDED(sessionControl.As(&meter))) {
                            float peakLevel = 0.0f;
                            if (SUCCEEDED(meter->GetPeakValue(&peakLevel))) {
                                if (peakLevel > threshold) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    void Reset() {
        m_sessionManager.Reset();
        m_device.Reset();
        m_deviceEnumerator.Reset();
        m_initialized = false;
    }

private:
    bool EnsureInitialized() {
        if (m_initialized && m_sessionManager) return true;

        Reset();

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, IID_PPV_ARGS(&m_deviceEnumerator));
        if (FAILED(hr)) return false;

        hr = m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
        if (FAILED(hr)) return false;

        hr = m_device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
            nullptr, reinterpret_cast<void**>(m_sessionManager.GetAddressOf()));
        if (FAILED(hr)) return false;

        m_initialized = true;
        return true;
    }

    ComPtr<IMMDeviceEnumerator> m_deviceEnumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioSessionManager2> m_sessionManager;
    bool m_initialized = false;
};

// ============================================================
// Get default microphone device ID
// ============================================================
struct MicInfo {
    std::wstring deviceId;
    std::wstring friendlyName;
    bool found;
};

MicInfo GetDefaultMicrophone() {
    MicInfo info;
    info.found = false;

    AudioDeviceEnumerator devEnum;
    if (!devEnum.EnumerateInputDevices()) {
        return info;
    }

    const auto& devices = devEnum.GetInputDevices();
    if (devices.empty()) {
        return info;
    }

    int defaultIdx = devEnum.GetDefaultInputDeviceIndex();
    if (defaultIdx >= 0 && defaultIdx < static_cast<int>(devices.size())) {
        info.deviceId = devices[defaultIdx].deviceId;
        info.friendlyName = devices[defaultIdx].friendlyName;
        info.found = true;
    } else {
        info.deviceId = devices[0].deviceId;
        info.friendlyName = devices[0].friendlyName;
        info.found = true;
    }

    return info;
}

// ============================================================
// Recording state per call
// ============================================================
struct CallRecordingState {
    bool isRecording;
    std::wstring outputPath;
    std::wstring processName;
    DWORD processPid;
    DWORD micSessionId;
    bool mixedEnabled;
    CallRecordingState() : isRecording(false), processPid(0), micSessionId(0), mixedEnabled(false) {}
};

// ============================================================
// SETTINGS DIALOG (Win32 GUI)
// ============================================================

// Dialog control IDs
#define IDD_SETTINGS       2000
#define IDC_PATH_EDIT      2001
#define IDC_PATH_BROWSE    2002
#define IDC_FORMAT_COMBO   2003
#define IDC_BITRATE_EDIT   2004
#define IDC_PROCESSES_EDIT 2005
#define IDC_POLL_EDIT      2006
#define IDC_SILENCE_EDIT   2007
#define IDC_SAVE_BTN       2008
#define IDC_CANCEL_BTN     2009
#define IDC_LOGGING_CHECK  2010
#define IDC_AUTOSTART_CHECK 2011

static HWND g_hSettingsDlg = nullptr;
static HFONT g_hSettingsFont = nullptr;  // [C2] FIX: store font handle for cleanup

void CreateLabel(HWND hParent, const wchar_t* text, int x, int y, int w, int h) {
    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, hParent, nullptr, GetModuleHandle(nullptr), nullptr);
}

HWND CreateEditBox(HWND hParent, const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
    return hEdit;
}

HWND CreateButton(HWND hParent, const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
}

HWND CreateCheckBox(HWND hParent, const wchar_t* text, int x, int y, int w, int h, int id, bool checked) {
    HWND hCheck = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
    if (checked) SendMessageW(hCheck, BM_SETCHECK, BST_CHECKED, 0);
    return hCheck;
}

HWND CreateComboBox(HWND hParent, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
}

std::wstring BrowseForFolder(HWND hOwner, const std::wstring& currentPath) {
    (void)currentPath;
    wchar_t path[MAX_PATH] = { 0 };
    BROWSEINFOW bi = {};
    bi.hwndOwner = hOwner;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select recording folder:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        return std::wstring(path);
    }
    return L"";
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // [C2] FIX: Delete old font if any, store new one globally
        if (g_hSettingsFont) {
            DeleteObject(g_hSettingsFont);
            g_hSettingsFont = nullptr;
        }
        g_hSettingsFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Read config safely
        AgentConfig config = GetConfigSnapshot();

        int y = 15;
        int labelX = 15;
        int editX = 180;
        int editW = 280;
        int rowH = 28;
        int gap = 8;

        // Recording Path
        CreateLabel(hWnd, L"Recording folder:", labelX, y + 3, 160, 20);
        HWND hPathEdit = CreateEditBox(hWnd, config.recordingPath.c_str(), editX, y, editW - 40, 24, IDC_PATH_EDIT);
        CreateButton(hWnd, L"...", editX + editW - 35, y, 35, 24, IDC_PATH_BROWSE);
        SendMessageW(hPathEdit, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // Audio Format
        CreateLabel(hWnd, L"Audio format:", labelX, y + 3, 160, 20);
        HWND hCombo = CreateComboBox(hWnd, editX, y, 100, 120, IDC_FORMAT_COMBO);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"mp3");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"wav");
        std::wstring fmtLower = config.audioFormat;
        std::transform(fmtLower.begin(), fmtLower.end(), fmtLower.begin(), ::towlower);
        if (fmtLower == L"wav") SendMessageW(hCombo, CB_SETCURSEL, 1, 0);
        else SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // MP3 Bitrate
        CreateLabel(hWnd, L"MP3 Bitrate (kbps):", labelX, y + 3, 160, 20);
        HWND hBitrate = CreateEditBox(hWnd, std::to_wstring(config.mp3Bitrate / 1000).c_str(),
            editX, y, 80, 24, IDC_BITRATE_EDIT);
        SendMessageW(hBitrate, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // Target Processes
        std::wstring procStr;
        for (size_t i = 0; i < config.targetProcesses.size(); i++) {
            if (i > 0) procStr += L", ";
            procStr += config.targetProcesses[i];
        }
        CreateLabel(hWnd, L"Target processes:", labelX, y + 3, 160, 20);
        HWND hProc = CreateEditBox(hWnd, procStr.c_str(), editX, y, editW, 24, IDC_PROCESSES_EDIT);
        SendMessageW(hProc, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // Poll Interval
        CreateLabel(hWnd, L"Poll interval (sec):", labelX, y + 3, 160, 20);
        HWND hPoll = CreateEditBox(hWnd, std::to_wstring(config.pollIntervalSeconds).c_str(),
            editX, y, 80, 24, IDC_POLL_EDIT);
        SendMessageW(hPoll, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // Silence Threshold
        CreateLabel(hWnd, L"Silence threshold:", labelX, y + 3, 160, 20);
        HWND hSilence = CreateEditBox(hWnd, std::to_wstring(config.silenceThreshold).c_str(),
            editX, y, 80, 24, IDC_SILENCE_EDIT);
        SendMessageW(hSilence, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        // Checkboxes
        CreateCheckBox(hWnd, L"Enable logging", labelX, y, 200, 24, IDC_LOGGING_CHECK, config.enableLogging);
        y += rowH;
        CreateCheckBox(hWnd, L"Auto-start with Windows", labelX, y, 250, 24, IDC_AUTOSTART_CHECK, config.autoRegisterStartup);
        y += rowH + gap + 5;

        // Buttons
        HWND hSaveBtn = CreateButton(hWnd, L"Save", editX, y, 100, 30, IDC_SAVE_BTN);
        HWND hCancelBtn = CreateButton(hWnd, L"Cancel", editX + 110, y, 100, 30, IDC_CANCEL_BTN);
        SendMessageW(hSaveBtn, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);

        // Set font on all labels
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        }, (LPARAM)g_hSettingsFont);

        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);

        if (wmId == IDC_PATH_BROWSE) {
            wchar_t currentPath[INI_BUFFER_SIZE] = { 0 };
            GetDlgItemTextW(hWnd, IDC_PATH_EDIT, currentPath, INI_BUFFER_SIZE);
            std::wstring newPath = BrowseForFolder(hWnd, currentPath);
            if (!newPath.empty()) {
                SetDlgItemTextW(hWnd, IDC_PATH_EDIT, newPath.c_str());
            }
        }
        else if (wmId == IDC_SAVE_BTN) {
            wchar_t buf[INI_BUFFER_SIZE];

            // [C1] Lock config for writing
            {
                std::lock_guard<std::mutex> lock(g_configMutex);

                GetDlgItemTextW(hWnd, IDC_PATH_EDIT, buf, INI_BUFFER_SIZE);
                g_config.recordingPath = buf;

                int sel = (int)SendDlgItemMessageW(hWnd, IDC_FORMAT_COMBO, CB_GETCURSEL, 0, 0);
                if (sel == 1) g_config.audioFormat = L"wav";
                else g_config.audioFormat = L"mp3";

                GetDlgItemTextW(hWnd, IDC_BITRATE_EDIT, buf, INI_BUFFER_SIZE);
                int bitrate = _wtoi(buf);
                if (bitrate > 0) g_config.mp3Bitrate = bitrate * 1000;

                GetDlgItemTextW(hWnd, IDC_PROCESSES_EDIT, buf, INI_BUFFER_SIZE);
                auto parsed = SplitString(buf, L',');
                if (!parsed.empty()) g_config.targetProcesses = parsed;

                GetDlgItemTextW(hWnd, IDC_POLL_EDIT, buf, INI_BUFFER_SIZE);
                int poll = _wtoi(buf);
                if (poll >= 1) g_config.pollIntervalSeconds = poll;

                GetDlgItemTextW(hWnd, IDC_SILENCE_EDIT, buf, INI_BUFFER_SIZE);
                int silence = _wtoi(buf);
                if (silence >= 1) g_config.silenceThreshold = silence;

                g_config.enableLogging = (SendDlgItemMessageW(hWnd, IDC_LOGGING_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_config.autoRegisterStartup = (SendDlgItemMessageW(hWnd, IDC_AUTOSTART_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }

            SaveConfig();

            auto config = GetConfigSnapshot();
            try {
                fs::create_directories(config.recordingPath);
            } catch (...) {}

            Log(L"Settings saved. Recording path: " + config.recordingPath);
            MessageBoxW(hWnd, L"Settings saved!", APP_TITLE, MB_OK | MB_ICONINFORMATION);
            DestroyWindow(hWnd);
        }
        else if (wmId == IDC_CANCEL_BTN) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_DESTROY:
        // [C2] FIX: Clean up font handle
        if (g_hSettingsFont) {
            DeleteObject(g_hSettingsFont);
            g_hSettingsFont = nullptr;
        }
        g_hSettingsDlg = nullptr;
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowSettingsDialog(HWND hParent) {
    (void)hParent;
    if (g_hSettingsDlg != nullptr) {
        SetForegroundWindow(g_hSettingsDlg);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"RDPCallRecorderSettingsClass";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hSettingsDlg = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"RDPCallRecorderSettingsClass",
        L"RDP Call Recorder - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (screenW - SETTINGS_DLG_WIDTH) / 2, (screenH - SETTINGS_DLG_HEIGHT) / 2,
        SETTINGS_DLG_WIDTH, SETTINGS_DLG_HEIGHT,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(g_hSettingsDlg, SW_SHOW);
    UpdateWindow(g_hSettingsDlg);
    SetForegroundWindow(g_hSettingsDlg);
}

// ============================================================
// TRAY ICON
// ============================================================
void CreateTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = IDI_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"RDP Call Recorder - Monitoring...");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void UpdateTrayTooltip() {
    int count = g_activeRecordings.load();
    if (count > 0) {
        swprintf_s(g_nid.szTip, TRAY_TIP_MAX_LEN, L"RDP Call Recorder - Recording (%d active)", count);
    } else {
        wcscpy_s(g_nid.szTip, L"RDP Call Recorder - Monitoring...");
    }
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    int count = g_activeRecordings.load();
    std::wstring statusText = L"Status: Monitoring";
    if (count > 0) {
        statusText = L"Status: Recording (" + std::to_wstring(count) + L" active)";
    }
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS, statusText.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_FOLDER, L"Open recordings folder");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

// ============================================================
// MAIN WINDOW PROC (hidden, handles tray + IPC)
// ============================================================
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Handle IPC message from second instance
    if (msg == WM_OPEN_SETTINGS_MSG && WM_OPEN_SETTINGS_MSG != 0) {
        ShowSettingsDialog(hWnd);
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hWnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            ShowSettingsDialog(hWnd);
        }
        return 0;

    case WM_SHOW_SETTINGS:
        ShowSettingsDialog(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_SETTINGS:
            ShowSettingsDialog(hWnd);
            break;
        case IDM_OPEN_FOLDER: {
            auto config = GetConfigSnapshot();
            ShellExecuteW(nullptr, L"open", config.recordingPath.c_str(), nullptr, nullptr, SW_SHOW);
            break;
        }
        case IDM_EXIT:
            g_running = false;
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// MONITOR THREAD - records BOTH voices using mixed recording
//
// Strategy:
//   1. Detect active call (process has active audio session)
//   2. Start process-specific capture (callee voice) - monitorOnly
//   3. Start microphone capture (caller voice) - monitorOnly
//   4. Enable mixed recording -> one output file with both voices
//   5. On call end: disable mixed, stop all captures
//
// [C4] FIX: Track active mixed recording count to handle simultaneous calls
// ============================================================
void MonitorThread() {
    // Initialize WinRT/COM in this thread
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    CaptureManager captureManager;
    ProcessEnumerator processEnum;
    AudioFormat audioFormat = GetAudioFormatFromConfig();

    // [H1] Cached audio session monitor
    AudioSessionMonitor audioMonitor;

    // Track recording state per process
    std::map<DWORD, CallRecordingState> callState;
    std::map<DWORD, int> silenceCounter;
    std::map<DWORD, int> startCounter;
    DWORD nextMicSessionId = MIC_SESSION_ID_BASE;

    // [C4] FIX: Count of active mixed recordings
    int activeMixedCount = 0;

    while (g_running) {
        try {
            // Re-read format in case settings changed
            audioFormat = GetAudioFormatFromConfig();
            AgentConfig config = GetConfigSnapshot();

            std::vector<FoundProcess> targetProcs = FindTargetProcesses(config);
            std::set<DWORD> currentPids;

            for (size_t i = 0; i < targetProcs.size(); i++) {
                DWORD pid = targetProcs[i].pid;
                std::wstring name = targetProcs[i].name;
                currentPids.insert(pid);

                bool hasRealAudio = audioMonitor.CheckProcessRealAudio(pid);

                if (hasRealAudio && !callState[pid].isRecording) {
                    // ---- AUDIO DETECTED, CHECK START THRESHOLD ----
                    startCounter[pid]++;
                    Log(L"Audio detected: " + name + L" PID=" + std::to_wstring(pid) +
                        L" count=" + std::to_wstring(startCounter[pid]) +
                        L"/" + std::to_wstring(config.startThreshold), LogLevel::LOG_DEBUG);

                    if (startCounter[pid] < config.startThreshold) {
                        continue;
                    }

                    // ---- CALL CONFIRMED (sustained audio) ----
                    startCounter[pid] = 0;
                    silenceCounter[pid] = 0;
                    std::wstring outputPath = BuildOutputPath(name, audioFormat);

                    DWORD micSessId = nextMicSessionId++;
                    if (nextMicSessionId >= 0xFFFFFFFF) {
                        nextMicSessionId = MIC_SESSION_ID_BASE;
                    }

                    // Step 1: Start process audio capture (callee voice) - monitorOnly
                    bool procStarted = captureManager.StartCapture(
                        pid, name, outputPath, audioFormat,
                        config.mp3Bitrate, false, L"", true);

                    if (!procStarted) {
                        Log(L"REC FAIL (process capture): " + name + L" PID=" + std::to_wstring(pid), LogLevel::LOG_ERROR);
                        continue;
                    }
                    Log(L"Process capture started: " + name + L" PID=" + std::to_wstring(pid), LogLevel::LOG_DEBUG);

                    // Step 2: Start microphone capture (caller voice) - monitorOnly
                    bool micStarted = false;
                    MicInfo mic = GetDefaultMicrophone();
                    if (mic.found) {
                        micStarted = captureManager.StartCaptureFromDevice(
                            micSessId, mic.friendlyName, mic.deviceId, true,
                            outputPath, audioFormat,
                            config.mp3Bitrate, false, true);
                        if (micStarted) {
                            Log(L"Microphone capture started: " + mic.friendlyName, LogLevel::LOG_DEBUG);
                        } else {
                            Log(L"Microphone capture failed: " + mic.friendlyName, LogLevel::LOG_WARN);
                        }
                    } else {
                        Log(L"No microphone found - recording callee voice only", LogLevel::LOG_WARN);
                    }

                    // Step 3: Enable mixed recording -> one output file
                    bool mixedOk = captureManager.EnableMixedRecording(
                        outputPath, audioFormat, config.mp3Bitrate);

                    if (!mixedOk) {
                        // Fallback: mixed recording failed, try direct capture instead
                        Log(L"Mixed recording failed, falling back to process-only capture", LogLevel::LOG_WARN);
                        captureManager.StopCapture(pid);
                        if (micStarted) captureManager.StopCapture(micSessId);

                        bool directStarted = captureManager.StartCapture(
                            pid, name, outputPath, audioFormat,
                            config.mp3Bitrate, false, L"", false);

                        if (!directStarted) {
                            Log(L"REC FAIL (direct fallback): " + name + L" PID=" + std::to_wstring(pid), LogLevel::LOG_ERROR);
                            continue;
                        }
                        micSessId = 0;
                    }

                    callState[pid].isRecording = true;
                    callState[pid].outputPath = outputPath;
                    callState[pid].processName = name;
                    callState[pid].processPid = pid;
                    callState[pid].micSessionId = micStarted ? micSessId : 0;
                    callState[pid].mixedEnabled = mixedOk;
                    g_activeRecordings++;

                    if (mixedOk) {
                        activeMixedCount++;  // [C4]
                        Log(L"REC START (mixed): " + name + L" PID=" + std::to_wstring(pid) + L" -> " + outputPath);
                    } else {
                        Log(L"REC START (process-only): " + name + L" PID=" + std::to_wstring(pid) + L" -> " + outputPath);
                    }
                    UpdateTrayTooltip();

                } else if (!hasRealAudio && !callState[pid].isRecording) {
                    // No audio and not recording - reset start counter
                    startCounter[pid] = 0;

                } else if (!hasRealAudio && callState[pid].isRecording) {
                    // ---- SILENCE DETECTED ----
                    silenceCounter[pid]++;
                    if (silenceCounter[pid] >= config.silenceThreshold) {
                        // ---- CALL ENDED ----
                        CallRecordingState& cs = callState[pid];

                        // [C4] FIX: Only disable mixed recording if this is the LAST active mixed recording
                        if (cs.mixedEnabled) {
                            activeMixedCount--;
                            if (activeMixedCount <= 0) {
                                captureManager.DisableMixedRecording();
                                activeMixedCount = 0;
                            }
                        }

                        // Stop microphone capture
                        if (cs.micSessionId != 0) {
                            captureManager.StopCapture(cs.micSessionId);
                        }

                        // Stop process capture
                        captureManager.StopCapture(pid);

                        Log(L"REC STOP: " + cs.processName + L" PID=" + std::to_wstring(pid) + L" -> " + cs.outputPath);
                        cs.isRecording = false;
                        cs.outputPath.clear();
                        cs.micSessionId = 0;
                        cs.mixedEnabled = false;
                        silenceCounter[pid] = 0;
                        g_activeRecordings--;
                        UpdateTrayTooltip();
                    }

                } else if (hasRealAudio && callState[pid].isRecording) {
                    // Still active - reset silence counter
                    silenceCounter[pid] = 0;
                }
            }

            // Handle processes that disappeared
            std::vector<DWORD> toRemove;
            for (auto it = callState.begin(); it != callState.end(); ++it) {
                DWORD pid = it->first;
                CallRecordingState& cs = it->second;
                if (currentPids.find(pid) == currentPids.end()) {
                    if (cs.isRecording) {
                        // Process exited while recording
                        if (cs.mixedEnabled) {
                            activeMixedCount--;  // [C4]
                            if (activeMixedCount <= 0) {
                                captureManager.DisableMixedRecording();
                                activeMixedCount = 0;
                            }
                        }
                        if (cs.micSessionId != 0) {
                            captureManager.StopCapture(cs.micSessionId);
                        }
                        captureManager.StopCapture(pid);

                        Log(L"REC STOP (process exited): " + cs.processName + L" PID=" + std::to_wstring(pid), LogLevel::LOG_WARN);
                        cs.isRecording = false;
                        g_activeRecordings--;
                    }
                    // [H4] FIX: Remove ALL stale entries, not just actively recording ones
                    toRemove.push_back(pid);
                }
            }
            for (DWORD removePid : toRemove) {
                callState.erase(removePid);
                silenceCounter.erase(removePid);
                startCounter.erase(removePid);
            }

            UpdateTrayTooltip();

        } catch (const std::exception& e) {
            // [C3] FIX: proper string conversion
            Log(L"Exception: " + Utf8ToWide(e.what()), LogLevel::LOG_ERROR);
        } catch (...) {
            Log(L"Unknown exception", LogLevel::LOG_ERROR);
        }

        // Sleep in small intervals so we can stop quickly
        AgentConfig config = GetConfigSnapshot();
        for (int i = 0; i < config.pollIntervalSeconds * 10 && g_running; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    captureManager.DisableMixedRecording();
    captureManager.StopAllCaptures();
    RoUninitialize();
}

// ============================================================
// Check if first launch (no config or no recording path set)
// ============================================================
bool IsFirstLaunch() {
    std::wstring iniPath = GetConfigPath();
    if (!fs::exists(iniPath)) return true;
    std::wstring marker = GetIniString(L"Advanced", L"Configured", L"false", iniPath);
    std::transform(marker.begin(), marker.end(), marker.begin(), ::towlower);
    return (marker != L"true" && marker != L"1" && marker != L"yes");
}

// ============================================================
// MAIN
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // Register custom message for IPC
    WM_OPEN_SETTINGS_MSG = RegisterWindowMessageW(L"RDPCallRecorder_OpenSettings");

    // [C5] Check if already running — Local mutex, per-session
    HANDLE hMutexSingle = CreateMutexW(nullptr, FALSE, MUTEX_SINGLE_INSTANCE);
    if (hMutexSingle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, WM_OPEN_SETTINGS_MSG, 0, 0);
        CloseHandle(hMutexSingle);
        return 0;
    }

    // Load config
    bool configLoaded = LoadConfig(g_config);
    (void)configLoaded;
    g_logLevel = ParseLogLevel(g_config.logLevel);
    bool firstLaunch = IsFirstLaunch();

    // Hide console
    HWND hConsole = GetConsoleWindow();
    if (hConsole) ShowWindow(hConsole, SW_HIDE);

    // Set priority
    SetProcessPriorityFromConfig(g_config.processPriority);

    // Register autostart
    if (g_config.autoRegisterStartup) {
        RegisterAutoStart();
    }

    // Initialize COM for main thread
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExW(&wc);

    // Create hidden main window
    g_hWndMain = CreateWindowExW(0, WINDOW_CLASS_NAME, APP_TITLE,
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    // Create tray icon
    CreateTrayIcon(g_hWndMain);

    // Log startup
    Log(L"=== RDP Call Recorder Agent v2.5 started ===");
    Log(L"User: " + GetCurrentFullName() + L" (login: " + GetCurrentLoginName() + L")");
    Log(L"Recording path: " + GetConfigSnapshot().recordingPath);
    Log(L"Mode: Mixed recording (both voices)");

    // If first launch - show settings dialog
    if (firstLaunch) {
        ShowSettingsDialog(g_hWndMain);
    }

    // Start monitor thread
    g_monitorThread = std::thread(MonitorThread);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (g_hSettingsDlg != nullptr && IsDialogMessageW(g_hSettingsDlg, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    g_running = false;
    if (g_monitorThread.joinable()) {
        g_monitorThread.join();
    }
    RemoveTrayIcon();
    CloseLogFile();  // [H3] Close buffered log file
    if (hMutexSingle) CloseHandle(hMutexSingle);
    if (g_hMutex) CloseHandle(g_hMutex);
    CoUninitialize();
    return 0;
}

// Alternative entry point (for console debugging)
int main() {
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_HIDE);
}
