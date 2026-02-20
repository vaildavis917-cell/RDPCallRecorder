#include "Config.h"
#include "Utils.h"
#include "Globals.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

AgentConfig g_config;
std::mutex g_configMutex;

AgentConfig GetConfigSnapshot() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;
}

static std::wstring GetIniString(const std::wstring& section, const std::wstring& key,
                                  const std::wstring& defaultValue, const std::wstring& iniPath) {
    wchar_t buffer[INI_BUFFER_SIZE];
    GetPrivateProfileStringW(section.c_str(), key.c_str(), defaultValue.c_str(),
                             buffer, INI_BUFFER_SIZE, iniPath.c_str());
    return std::wstring(buffer);
}

static int GetIniInt(const std::wstring& section, const std::wstring& key,
                      int defaultValue, const std::wstring& iniPath) {
    return GetPrivateProfileIntW(section.c_str(), key.c_str(), defaultValue, iniPath.c_str());
}

static bool GetIniBool(const std::wstring& section, const std::wstring& key,
                        bool defaultValue, const std::wstring& iniPath) {
    std::wstring val = GetIniString(section, key, defaultValue ? L"true" : L"false", iniPath);
    std::transform(val.begin(), val.end(), val.begin(), ::towlower);
    return (val == L"true" || val == L"1" || val == L"yes");
}

bool LoadConfig(AgentConfig& config) {
    if (config.recordingPath.empty()) {
        config.recordingPath = GetDefaultRecordingPath();
    }

    std::wstring iniPath = GetConfigPath();
    if (!fs::exists(iniPath)) return false;

    std::lock_guard<std::mutex> lock(g_configMutex);

    config.recordingPath = GetIniString(L"Recording", L"RecordingPath", config.recordingPath, iniPath);
    // If INI has empty RecordingPath=, fall back to default
    if (config.recordingPath.empty()) {
        config.recordingPath = GetDefaultRecordingPath();
    }
    // Sanitize recording path: trim trailing spaces/dots from each path segment
    // Windows does not allow directory names ending with spaces or dots
    {
        fs::path sanitized;
        for (const auto& part : fs::path(config.recordingPath)) {
            std::wstring seg = part.wstring();
            while (!seg.empty() && (seg.back() == L' ' || seg.back() == L'.')) seg.pop_back();
            if (!seg.empty()) sanitized /= seg;
        }
        config.recordingPath = sanitized.wstring();
    }
    config.audioFormat   = GetIniString(L"Recording", L"AudioFormat", config.audioFormat, iniPath);

    int rawBitrate = GetIniInt(L"Recording", L"MP3Bitrate", static_cast<int>(config.mp3Bitrate), iniPath);
    if (rawBitrate >= static_cast<int>(MIN_MP3_BITRATE) && rawBitrate <= static_cast<int>(MAX_MP3_BITRATE)) {
        config.mp3Bitrate = static_cast<UINT32>(rawBitrate);
    }

    config.pollIntervalSeconds = GetIniInt(L"Monitoring", L"PollInterval", config.pollIntervalSeconds, iniPath);
    config.silenceThreshold    = GetIniInt(L"Monitoring", L"SilenceThreshold", config.silenceThreshold, iniPath);
    config.startThreshold      = GetIniInt(L"Monitoring", L"StartThreshold", config.startThreshold, iniPath);
    config.minRecordingSeconds = GetIniInt(L"Monitoring", L"MinRecordingSeconds", config.minRecordingSeconds, iniPath);

    // Telegram-specific parameters
    std::wstring tgPeakStr = GetIniString(L"Monitoring", L"TelegramSilencePeakThreshold", L"0.03", iniPath);
    config.telegramSilencePeakThreshold = (float)_wtof(tgPeakStr.c_str());
    config.telegramPeakHistorySize = GetIniInt(L"Monitoring", L"TelegramPeakHistorySize", config.telegramPeakHistorySize, iniPath);
    config.telegramSilenceCycles   = GetIniInt(L"Monitoring", L"TelegramSilenceCycles", config.telegramSilenceCycles, iniPath);

    if (config.pollIntervalSeconds < 1) config.pollIntervalSeconds = 1;
    if (config.pollIntervalSeconds > 60) config.pollIntervalSeconds = 60;
    if (config.silenceThreshold < 1) config.silenceThreshold = 1;
    if (config.silenceThreshold > 100) config.silenceThreshold = 100;
    if (config.startThreshold < 1) config.startThreshold = 1;
    if (config.startThreshold > 100) config.startThreshold = 100;
    if (config.minRecordingSeconds < 0) config.minRecordingSeconds = 0;
    if (config.minRecordingSeconds > 600) config.minRecordingSeconds = 600;
    if (config.telegramSilencePeakThreshold < 0.001f) config.telegramSilencePeakThreshold = 0.001f;
    if (config.telegramSilencePeakThreshold > 1.0f) config.telegramSilencePeakThreshold = 1.0f;
    if (config.telegramPeakHistorySize < 1) config.telegramPeakHistorySize = 1;
    if (config.telegramPeakHistorySize > 50) config.telegramPeakHistorySize = 50;
    if (config.telegramSilenceCycles < 1) config.telegramSilenceCycles = 1;
    if (config.telegramSilenceCycles > 100) config.telegramSilenceCycles = 100;

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
    config.autoUpdate          = GetIniBool(L"Advanced", L"AutoUpdate", config.autoUpdate, iniPath);
    config.updateCheckIntervalHours = GetIniInt(L"Advanced", L"UpdateCheckIntervalHours", config.updateCheckIntervalHours, iniPath);
    if (config.updateCheckIntervalHours < 1) config.updateCheckIntervalHours = 1;
    if (config.updateCheckIntervalHours > 168) config.updateCheckIntervalHours = 168;

    return true;
}

void SaveConfig() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    std::wstring iniPath = GetConfigPath();

    WritePrivateProfileStringW(L"Recording", L"RecordingPath", g_config.recordingPath.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Recording", L"AudioFormat", g_config.audioFormat.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Recording", L"MP3Bitrate", std::to_wstring(g_config.mp3Bitrate).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"PollInterval", std::to_wstring(g_config.pollIntervalSeconds).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"SilenceThreshold", std::to_wstring(g_config.silenceThreshold).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"StartThreshold", std::to_wstring(g_config.startThreshold).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"MinRecordingSeconds", std::to_wstring(g_config.minRecordingSeconds).c_str(), iniPath.c_str());

    // Telegram-specific parameters
    wchar_t tgPeakBuf[32];
    swprintf_s(tgPeakBuf, 32, L"%.3f", g_config.telegramSilencePeakThreshold);
    WritePrivateProfileStringW(L"Monitoring", L"TelegramSilencePeakThreshold", tgPeakBuf, iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"TelegramPeakHistorySize", std::to_wstring(g_config.telegramPeakHistorySize).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Monitoring", L"TelegramSilenceCycles", std::to_wstring(g_config.telegramSilenceCycles).c_str(), iniPath.c_str());

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
    WritePrivateProfileStringW(L"Advanced", L"AutoUpdate", g_config.autoUpdate ? L"true" : L"false", iniPath.c_str());
    WritePrivateProfileStringW(L"Advanced", L"UpdateCheckIntervalHours", std::to_wstring(g_config.updateCheckIntervalHours).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Advanced", L"Configured", L"true", iniPath.c_str());
}

bool IsFirstLaunch() {
    std::wstring iniPath = GetConfigPath();
    if (!fs::exists(iniPath)) return true;
    std::wstring marker = GetIniString(L"Advanced", L"Configured", L"false", iniPath);
    std::transform(marker.begin(), marker.end(), marker.begin(), ::towlower);
    return (marker != L"true" && marker != L"1" && marker != L"yes");
}
