#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <windows.h>

struct AgentConfig {
    std::wstring recordingPath = L"";
    std::wstring audioFormat = L"mp3";
    UINT32 mp3Bitrate = 128000;
    int pollIntervalSeconds = 2;
    int silenceThreshold = 15;
    int startThreshold = 2;
    int minRecordingSeconds = 60;
    float telegramSilencePeakThreshold = 0.03f;
    int telegramPeakHistorySize = 5;
    int telegramSilenceCycles = 3;
    std::vector<std::wstring> targetProcesses = { L"WhatsApp.exe", L"WhatsApp.Root.exe", L"Telegram.exe", L"Viber.exe" };
    bool enableLogging = true;
    std::wstring logLevel = L"INFO";
    int maxLogSizeMB = 10;
    bool hideConsole = true;
    bool useMutex = true;
    std::wstring mutexName = L"Local\\RDPCallRecorderAgentMutex";
    std::wstring processPriority = L"BelowNormal";
    bool autoRegisterStartup = true;
    bool autoUpdate = true;
    int updateCheckIntervalHours = 6;
};

extern AgentConfig g_config;
extern std::mutex g_configMutex;

AgentConfig GetConfigSnapshot();
bool LoadConfig(AgentConfig& config);
void SaveConfig();
bool IsFirstLaunch();
