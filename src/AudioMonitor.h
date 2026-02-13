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

    bool CheckProcessRealAudio(DWORD processId, float threshold = 0.01f);

    struct DetectedSession {
        DWORD pid;
        std::wstring processName;
        std::wstring parentName;
        DWORD parentPid;
        float peakLevel;
    };

    std::vector<DetectedSession> FindActiveTargetSessions(
        const std::vector<std::wstring>& targetNames, float threshold = 0.01f);
    void DumpAudioSessions();
    void Reset();

private:
    bool CheckSessionsOnDevice(ComPtr<IAudioSessionManager2>& sessionManager,
                                DWORD processId, float threshold);
    bool EnsureEnumeratorInitialized();
    bool EnsureDefaultDeviceCached();

    ComPtr<IMMDeviceEnumerator> m_deviceEnumerator;
    ComPtr<IMMDevice> m_cachedDevice;
    ComPtr<IAudioSessionManager2> m_cachedSessionManager;
    bool m_initialized = false;
};
