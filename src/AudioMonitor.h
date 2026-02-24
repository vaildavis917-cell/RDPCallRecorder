#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include "CaptureManager.h"
#include "AudioDeviceEnumerator.h"
#include "ProcessUtils.h"

using Microsoft::WRL::ComPtr;

AudioFormat GetAudioFormatFromConfig();
std::wstring GetFileExtension(AudioFormat format);
std::wstring BuildOutputPath(const std::wstring& processName, AudioFormat format);

struct MicInfo {
    std::wstring deviceId;
    std::wstring friendlyName;
    bool found;
};

MicInfo GetDefaultMicrophone();

class AudioSessionMonitor {
public:
    AudioSessionMonitor() = default;
    ~AudioSessionMonitor() = default;

    // Legacy (each call may create snapshots internally)
    bool CheckProcessRealAudio(DWORD processId, float threshold = 0.01f);
    float GetProcessPeakLevel(DWORD processId);
    bool IsSessionActive(DWORD processId);
    void DumpAudioSessions();

    // Snapshot-based overloads (use these in hot path)
    float GetProcessPeakLevel(DWORD processId, const ProcessSnapshot& snap);
    bool IsSessionActive(DWORD processId, const ProcessSnapshot& snap);
    void DumpAudioSessions(const ProcessSnapshot& snap);

    struct DetectedSession {
        DWORD pid;
        std::wstring processName;
        std::wstring parentName;
        DWORD parentPid;
        float peakLevel;
    };

    std::vector<DetectedSession> FindActiveTargetSessions(
        const std::vector<std::wstring>& targetNames, float threshold = 0.01f);
    void Reset();

private:
    bool EnsureEnumeratorInitialized();

    ComPtr<IMMDeviceEnumerator> m_deviceEnumerator;
    bool m_initialized = false;
};
