#pragma once

#include <string>
#include <windows.h>

struct CallRecordingState {
    bool isRecording = false;
    std::wstring outputPath;
    std::wstring processName;
    DWORD processPid = 0;
    DWORD micSessionId = 0;
    bool mixedEnabled = false;
};

void MonitorThread();
