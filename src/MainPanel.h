#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

// ============================================================
// Shared data structures for UI ↔ MonitorThread communication
// ============================================================

struct ActiveRecordingInfo {
    DWORD pid = 0;
    std::wstring processName;
    std::wstring outputPath;
    std::chrono::steady_clock::time_point startTime;
    bool mixedEnabled = false;
};

// Thread-safe shared state for the Status panel
class StatusData {
public:
    void SetRecordings(const std::vector<ActiveRecordingInfo>& recs);
    std::vector<ActiveRecordingInfo> GetRecordings();

    void PushLogLine(const std::wstring& line);
    std::vector<std::wstring> GetLogLines();

    static const int MAX_LOG_LINES = 100;

private:
    std::mutex m_mutex;
    std::vector<ActiveRecordingInfo> m_recordings;
    std::vector<std::wstring> m_logRing;
};

extern StatusData g_statusData;

// ============================================================
// Main Panel functions
// ============================================================
void ShowMainPanel(HWND hParent);
void ShowMainPanelOnTab(int tabIndex); // 0 = Status, 1 = Settings
