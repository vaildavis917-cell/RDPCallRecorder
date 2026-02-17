#include "AudioMonitor.h"
#include "Config.h"
#include "Utils.h"
#include "Logger.h"
#include "ProcessUtils.h"
#include "Globals.h"
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

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

std::wstring BuildOutputPath(const std::wstring& processName, AudioFormat format) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tmNow;
    localtime_s(&tmNow, &time_t_now);

    std::wstring username = SanitizeForPath(GetCurrentFullName());
    wchar_t dateStr[32], timeStrBuf[32];
    swprintf_s(dateStr, 32, L"%04d-%02d-%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
    swprintf_s(timeStrBuf, 32, L"%02d-%02d-%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    std::wstring appName = processName;
    size_t dotPos = appName.rfind(L'.');
    if (dotPos != std::wstring::npos) appName = appName.substr(0, dotPos);

    auto config = GetConfigSnapshot();
    fs::path outputDir = fs::path(config.recordingPath) / username / dateStr;
    try { fs::create_directories(outputDir); }
    catch (const std::exception& e) {
        Log(L"Failed to create directory: " + outputDir.wstring() + L" - " + Utf8ToWide(e.what()), LogLevel::LOG_ERROR);
        outputDir = fs::temp_directory_path();
    }

    std::wstring filename = std::wstring(dateStr) + L"_" + username + L"_" + appName + L"_" + std::wstring(timeStrBuf) + GetFileExtension(format);
    return (outputDir / filename).wstring();
}

MicInfo GetDefaultMicrophone() {
    MicInfo info;
    info.found = false;

    AudioDeviceEnumerator devEnum;
    if (!devEnum.EnumerateInputDevices()) return info;

    const auto& devices = devEnum.GetInputDevices();
    if (devices.empty()) return info;

    int defaultIdx = devEnum.GetDefaultInputDeviceIndex();
    if (defaultIdx >= 0 && defaultIdx < static_cast<int>(devices.size())) {
        info.deviceId = devices[defaultIdx].deviceId;
        info.friendlyName = devices[defaultIdx].friendlyName;
    } else {
        info.deviceId = devices[0].deviceId;
        info.friendlyName = devices[0].friendlyName;
    }
    info.found = true;
    return info;
}

bool AudioSessionMonitor::CheckProcessRealAudio(DWORD processId, float threshold) {
    if (!EnsureDefaultDeviceCached()) return false;
    return CheckSessionsOnDevice(m_cachedSessionManager, processId, threshold);
}

float AudioSessionMonitor::GetProcessPeakLevel(DWORD processId) {
    if (!EnsureDefaultDeviceCached()) return 0.0f;
    if (!m_cachedSessionManager) return 0.0f;

    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    if (FAILED(m_cachedSessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) return 0.0f;

    int sessionCount = 0;
    sessionEnumerator->GetCount(&sessionCount);
    float maxPeak = 0.0f;

    for (int i = 0; i < sessionCount; i++) {
        ComPtr<IAudioSessionControl> sessionControl;
        if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl) continue;

        ComPtr<IAudioSessionControl2> sessionControl2;
        if (SUCCEEDED(sessionControl.As(&sessionControl2))) {
            DWORD sessionProcessId = 0;
            if (SUCCEEDED(sessionControl2->GetProcessId(&sessionProcessId))) {
                bool isMatch = (sessionProcessId == processId);
                if (!isMatch && sessionProcessId != 0)
                    isMatch = IsChildOfProcess(sessionProcessId, processId);

                if (isMatch) {
                    ComPtr<IAudioMeterInformation> meter;
                    if (SUCCEEDED(sessionControl.As(&meter))) {
                        float peakLevel = 0.0f;
                        if (SUCCEEDED(meter->GetPeakValue(&peakLevel)) && peakLevel > maxPeak)
                            maxPeak = peakLevel;
                    }
                }
            }
        }
    }
    return maxPeak;
}

bool AudioSessionMonitor::CheckSessionsOnDevice(ComPtr<IAudioSessionManager2>& sessionManager,
                                                  DWORD processId, float threshold) {
    if (!sessionManager) return false;

    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) return false;

    int sessionCount = 0;
    sessionEnumerator->GetCount(&sessionCount);

    for (int i = 0; i < sessionCount; i++) {
        ComPtr<IAudioSessionControl> sessionControl;
        if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl) continue;

        ComPtr<IAudioSessionControl2> sessionControl2;
        if (SUCCEEDED(sessionControl.As(&sessionControl2))) {
            DWORD sessionProcessId = 0;
            if (SUCCEEDED(sessionControl2->GetProcessId(&sessionProcessId))) {
                bool isMatch = (sessionProcessId == processId);
                if (!isMatch && sessionProcessId != 0)
                    isMatch = IsChildOfProcess(sessionProcessId, processId);

                if (isMatch) {
                    ComPtr<IAudioMeterInformation> meter;
                    if (SUCCEEDED(sessionControl.As(&meter))) {
                        float peakLevel = 0.0f;
                        if (SUCCEEDED(meter->GetPeakValue(&peakLevel)) && peakLevel > threshold)
                            return true;
                    }
                }
            }
        }
    }
    return false;
}

std::vector<AudioSessionMonitor::DetectedSession> AudioSessionMonitor::FindActiveTargetSessions(
    const std::vector<std::wstring>& targetNames, float threshold) {
    std::vector<DetectedSession> result;
    if (!EnsureEnumeratorInitialized()) return result;

    ComPtr<IMMDeviceCollection> deviceCollection;
    if (FAILED(m_deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection)) || !deviceCollection)
        return result;

    UINT deviceCount = 0;
    deviceCollection->GetCount(&deviceCount);

    for (UINT d = 0; d < deviceCount; d++) {
        ComPtr<IMMDevice> device;
        if (FAILED(deviceCollection->Item(d, &device)) || !device) continue;

        ComPtr<IAudioSessionManager2> sessionManager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
            nullptr, reinterpret_cast<void**>(sessionManager.GetAddressOf()))) || !sessionManager) continue;

        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) continue;

        int sessionCount = 0;
        sessionEnumerator->GetCount(&sessionCount);

        for (int i = 0; i < sessionCount; i++) {
            ComPtr<IAudioSessionControl> sessionControl;
            if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl) continue;

            ComPtr<IAudioSessionControl2> sessionControl2;
            if (!SUCCEEDED(sessionControl.As(&sessionControl2))) continue;

            DWORD sessionPid = 0;
            if (!SUCCEEDED(sessionControl2->GetProcessId(&sessionPid)) || sessionPid == 0) continue;

            float peakLevel = 0.0f;
            ComPtr<IAudioMeterInformation> meter;
            if (SUCCEEDED(sessionControl.As(&meter))) meter->GetPeakValue(&peakLevel);
            if (peakLevel <= threshold) continue;

            std::wstring procName = GetProcessNameByPid(sessionPid);
            DWORD parentPid = GetParentProcessId(sessionPid);
            std::wstring parentName = (parentPid != 0) ? GetProcessNameByPid(parentPid) : L"";

            bool matched = false;
            for (const auto& target : targetNames) {
                if (_wcsicmp(procName.c_str(), target.c_str()) == 0) { matched = true; break; }
                if (_wcsicmp(parentName.c_str(), target.c_str()) == 0) { matched = true; break; }
                if (parentPid != 0) {
                    DWORD gpPid = GetParentProcessId(parentPid);
                    if (gpPid != 0 && _wcsicmp(GetProcessNameByPid(gpPid).c_str(), target.c_str()) == 0)
                        { matched = true; break; }
                }
            }

            if (matched)
                result.push_back({ sessionPid, procName, parentName, parentPid, peakLevel });
        }
    }
    return result;
}

void AudioSessionMonitor::DumpAudioSessions() {
    if (!EnsureEnumeratorInitialized()) return;

    ComPtr<IMMDeviceCollection> deviceCollection;
    if (FAILED(m_deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection)) || !deviceCollection)
        return;

    UINT deviceCount = 0;
    deviceCollection->GetCount(&deviceCount);
    Log(L"[DIAG] Active render devices: " + std::to_wstring(deviceCount), LogLevel::LOG_DEBUG);

    for (UINT d = 0; d < deviceCount; d++) {
        ComPtr<IMMDevice> device;
        if (FAILED(deviceCollection->Item(d, &device)) || !device) continue;

        LPWSTR deviceId = nullptr;
        device->GetId(&deviceId);
        std::wstring devIdStr = deviceId ? deviceId : L"(unknown)";
        if (deviceId) CoTaskMemFree(deviceId);

        ComPtr<IAudioSessionManager2> sessionManager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
            nullptr, reinterpret_cast<void**>(sessionManager.GetAddressOf()))) || !sessionManager) continue;

        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) continue;

        int sessionCount = 0;
        sessionEnumerator->GetCount(&sessionCount);
        Log(L"[DIAG] Device " + std::to_wstring(d) + L" (" + devIdStr.substr(0, 40) + L"): " +
            std::to_wstring(sessionCount) + L" sessions", LogLevel::LOG_DEBUG);

        for (int i = 0; i < sessionCount; i++) {
            ComPtr<IAudioSessionControl> sessionControl;
            if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl) continue;

            ComPtr<IAudioSessionControl2> sessionControl2;
            if (SUCCEEDED(sessionControl.As(&sessionControl2))) {
                DWORD sessionPid = 0;
                sessionControl2->GetProcessId(&sessionPid);
                float peakLevel = 0.0f;
                ComPtr<IAudioMeterInformation> meter;
                if (SUCCEEDED(sessionControl.As(&meter))) meter->GetPeakValue(&peakLevel);

                DWORD parentPid = GetParentProcessId(sessionPid);
                Log(L"[DIAG] Dev" + std::to_wstring(d) + L" Sess" + std::to_wstring(i) +
                    L": PID=" + std::to_wstring(sessionPid) +
                    L" Name=" + GetProcessNameByPid(sessionPid) +
                    L" ParentPID=" + std::to_wstring(parentPid) +
                    L" ParentName=" + ((parentPid != 0) ? GetProcessNameByPid(parentPid) : L"(none)") +
                    L" Peak=" + std::to_wstring(peakLevel), LogLevel::LOG_DEBUG);
            }
        }
    }
}

void AudioSessionMonitor::Reset() {
    m_cachedSessionManager.Reset();
    m_cachedDevice.Reset();
    m_deviceEnumerator.Reset();
    m_initialized = false;
}

bool AudioSessionMonitor::EnsureEnumeratorInitialized() {
    if (m_initialized && m_deviceEnumerator) return true;
    m_deviceEnumerator.Reset();
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&m_deviceEnumerator))))
        return false;
    m_initialized = true;
    return true;
}

bool AudioSessionMonitor::EnsureDefaultDeviceCached() {
    if (m_cachedSessionManager) return true;
    if (!EnsureEnumeratorInitialized()) return false;
    if (FAILED(m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_cachedDevice)) || !m_cachedDevice) {
        Reset(); return false;
    }
    if (FAILED(m_cachedDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(m_cachedSessionManager.GetAddressOf()))) || !m_cachedSessionManager) {
        Reset(); return false;
    }
    return true;
}
