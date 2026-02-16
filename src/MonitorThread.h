#pragma once

#include <string>
#include <windows.h>
#include <chrono>

struct CallRecordingState {
    bool isRecording = false;
    std::wstring outputPath;
    std::wstring processName;
    DWORD processPid = 0;
    DWORD micSessionId = 0;
    bool mixedEnabled = false;
    std::chrono::steady_clock::time_point startTime;
};

void MonitorThread();
