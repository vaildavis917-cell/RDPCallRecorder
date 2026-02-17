#include "MonitorThread.h"
#include "Config.h"
#include "Logger.h"
#include "Utils.h"
#include "Globals.h"
#include "ProcessUtils.h"
#include "AudioMonitor.h"
#include "TrayIcon.h"
#include "MainPanel.h"
#include "CaptureManager.h"
#include "ProcessEnumerator.h"
#include <roapi.h>
#include <map>
#include <set>
#include <deque>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>

// Telegram-specific silence detection constants
static const float  TELEGRAM_SILENCE_PEAK_THRESHOLD = 0.03f;  // avg peak below this = silence
static const int    TELEGRAM_PEAK_HISTORY_SIZE       = 5;      // rolling window size (cycles)
static const int    TELEGRAM_SILENCE_CYCLES          = 5;      // cycles of low avg peak to stop (= 10s at 2s poll)

static bool IsTelegramProcess(const std::wstring& name) {
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    return lower.find(L"telegram") != std::wstring::npos;
}

void MonitorThread() {
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE)
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    CaptureManager captureManager;
    ProcessEnumerator processEnum;
    AudioFormat audioFormat = GetAudioFormatFromConfig();
    AudioSessionMonitor audioMonitor;

    std::map<DWORD, CallRecordingState> callState;
    std::map<DWORD, int> silenceCounter;
    std::map<DWORD, int> startCounter;
    std::map<DWORD, std::deque<float>> peakHistory;  // rolling peak history per PID
    DWORD nextMicSessionId = MIC_SESSION_ID_BASE;
    int activeMixedCount = 0;

    while (g_running) {
        try {
            audioFormat = GetAudioFormatFromConfig();
            AgentConfig config = GetConfigSnapshot();
            std::vector<FoundProcess> targetProcs = FindTargetProcesses(config);
            std::set<DWORD> currentPids;

            for (auto& tp : targetProcs)
                Log(L"[DIAG] Found target: " + tp.name + L" PID=" + std::to_wstring(tp.pid), LogLevel::LOG_DEBUG);

            static int diagCounter = 0;
            if (++diagCounter >= 15) { diagCounter = 0; audioMonitor.DumpAudioSessions(); }

            for (auto& tp : targetProcs) {
                DWORD pid = tp.pid;
                std::wstring name = tp.name;
                currentPids.insert(pid);

                bool isTelegram = IsTelegramProcess(name);
                float currentPeak = audioMonitor.GetProcessPeakLevel(pid);
                bool hasRealAudio = (currentPeak > AUDIO_PEAK_THRESHOLD);

                // Maintain rolling peak history for all processes (used for Telegram)
                auto& history = peakHistory[pid];
                history.push_back(currentPeak);
                if ((int)history.size() > TELEGRAM_PEAK_HISTORY_SIZE)
                    history.pop_front();

                if (hasRealAudio && !callState[pid].isRecording) {
                    startCounter[pid]++;
                    Log(L"Audio detected: " + name + L" PID=" + std::to_wstring(pid) +
                        L" peak=" + std::to_wstring(currentPeak) +
                        L" count=" + std::to_wstring(startCounter[pid]) + L"/" + std::to_wstring(config.startThreshold), LogLevel::LOG_DEBUG);

                    if (startCounter[pid] < config.startThreshold) continue;

                    startCounter[pid] = 0;
                    silenceCounter[pid] = 0;
                    std::wstring outputPath = BuildOutputPath(name, audioFormat);
                    DWORD micSessId = nextMicSessionId++;
                    if (nextMicSessionId >= 0xFFFFFFFF) nextMicSessionId = MIC_SESSION_ID_BASE;

                    bool procStarted = captureManager.StartCapture(pid, name, outputPath, audioFormat, config.mp3Bitrate, false, L"", true);
                    if (!procStarted) { Log(L"REC FAIL (process): " + name, LogLevel::LOG_ERROR); continue; }

                    bool micStarted = false;
                    MicInfo mic = GetDefaultMicrophone();
                    if (mic.found) {
                        micStarted = captureManager.StartCaptureFromDevice(micSessId, mic.friendlyName, mic.deviceId, true,
                            outputPath, audioFormat, config.mp3Bitrate, false, true);
                        if (!micStarted) Log(L"Mic capture failed: " + mic.friendlyName, LogLevel::LOG_WARN);
                    }

                    bool mixedOk = captureManager.EnableMixedRecording(outputPath, audioFormat, config.mp3Bitrate);
                    if (!mixedOk) {
                        Log(L"Mixed recording failed, falling back to process-only", LogLevel::LOG_WARN);
                        captureManager.StopCapture(pid);
                        if (micStarted) captureManager.StopCapture(micSessId);
                        bool directStarted = captureManager.StartCapture(pid, name, outputPath, audioFormat, config.mp3Bitrate, false, L"", false);
                        if (!directStarted) { Log(L"REC FAIL (fallback): " + name, LogLevel::LOG_ERROR); continue; }
                        micSessId = 0;
                    }

                    callState[pid] = { true, outputPath, name, pid, micStarted ? micSessId : (DWORD)0, mixedOk,
                                       std::chrono::steady_clock::now() };
                    g_activeRecordings++;
                    if (mixedOk) activeMixedCount++;
                    Log(L"REC START: " + name + L" PID=" + std::to_wstring(pid) + L" -> " + outputPath);
                    UpdateTrayTooltip();
                    ShowTrayBalloon(L"Recording Started", name + L" — call recording in progress");

                } else if (!hasRealAudio && !callState[pid].isRecording) {
                    if (startCounter[pid] > 0) startCounter[pid]--;

                } else if (callState[pid].isRecording) {
                    // ===== SILENCE DETECTION DURING RECORDING =====
                    // For Telegram: use rolling average peak because Telegram keeps audio session
                    // active with micro-peaks even when no call is in progress.
                    // For other apps: use simple hasRealAudio check (peak > 0.01).

                    bool isSilent = false;

                    if (isTelegram) {
                        // Calculate average peak over the rolling window
                        float avgPeak = 0.0f;
                        if (!history.empty()) {
                            avgPeak = std::accumulate(history.begin(), history.end(), 0.0f) / (float)history.size();
                        }
                        isSilent = (avgPeak < TELEGRAM_SILENCE_PEAK_THRESHOLD);

                        Log(L"[TG] PID=" + std::to_wstring(pid) +
                            L" peak=" + std::to_wstring(currentPeak) +
                            L" avgPeak=" + std::to_wstring(avgPeak) +
                            L" silent=" + (isSilent ? L"YES" : L"NO") +
                            L" counter=" + std::to_wstring(silenceCounter[pid]) +
                            L"/" + std::to_wstring(TELEGRAM_SILENCE_CYCLES), LogLevel::LOG_DEBUG);
                    } else {
                        isSilent = !hasRealAudio;
                    }

                    if (isSilent) {
                        silenceCounter[pid]++;
                        int threshold = isTelegram ? TELEGRAM_SILENCE_CYCLES : config.silenceThreshold;

                        if (silenceCounter[pid] >= threshold) {
                            auto& cs = callState[pid];
                            if (cs.mixedEnabled) {
                                activeMixedCount--;
                                if (activeMixedCount <= 0) { captureManager.DisableMixedRecording(); activeMixedCount = 0; }
                            }
                            if (cs.micSessionId != 0) captureManager.StopCapture(cs.micSessionId);
                            captureManager.StopCapture(pid);
                            Log(L"REC STOP: " + cs.processName + L" PID=" + std::to_wstring(pid) + L" -> " + cs.outputPath);
                            ShowTrayBalloon(L"Recording Stopped", cs.processName + L" — recording saved");
                            cs = {};
                            silenceCounter[pid] = 0;
                            peakHistory.erase(pid);
                            g_activeRecordings--;
                            UpdateTrayTooltip();
                        }
                    } else {
                        silenceCounter[pid] = 0;
                    }
                }
            }

            std::vector<DWORD> toRemove;
            for (auto& [pid, cs] : callState) {
                if (currentPids.find(pid) == currentPids.end()) {
                    if (cs.isRecording) {
                        if (cs.mixedEnabled) {
                            activeMixedCount--;
                            if (activeMixedCount <= 0) { captureManager.DisableMixedRecording(); activeMixedCount = 0; }
                        }
                        if (cs.micSessionId != 0) captureManager.StopCapture(cs.micSessionId);
                        captureManager.StopCapture(pid);
                        Log(L"REC STOP (exited): " + cs.processName + L" PID=" + std::to_wstring(pid), LogLevel::LOG_WARN);
                        ShowTrayBalloon(L"Recording Stopped", cs.processName + L" — process exited, recording saved");
                        cs.isRecording = false;
                        g_activeRecordings--;
                    }
                    toRemove.push_back(pid);
                }
            }
            for (DWORD p : toRemove) { callState.erase(p); silenceCounter.erase(p); startCounter.erase(p); peakHistory.erase(p); }
            UpdateTrayTooltip();

            // Push active recordings to shared StatusData for UI
            {
                std::vector<ActiveRecordingInfo> activeRecs;
                for (auto& [pid, cs] : callState) {
                    if (cs.isRecording) {
                        ActiveRecordingInfo info;
                        info.pid = cs.processPid;
                        info.processName = cs.processName;
                        info.outputPath = cs.outputPath;
                        info.startTime = cs.startTime;
                        info.mixedEnabled = cs.mixedEnabled;
                        activeRecs.push_back(info);
                    }
                }
                g_statusData.SetRecordings(activeRecs);
            }

        } catch (const std::exception& e) {
            Log(L"Exception: " + Utf8ToWide(e.what()), LogLevel::LOG_ERROR);
        } catch (...) {
            Log(L"Unknown exception", LogLevel::LOG_ERROR);
        }

        // Check for force start request from UI
        if (g_forceStartRecording.exchange(false)) {
            Log(L"[UI] Force start recording requested");
            AgentConfig cfgStart = GetConfigSnapshot();
            std::vector<FoundProcess> forceProcs = FindTargetProcesses(cfgStart);
            for (auto& tp : forceProcs) {
                DWORD pid = tp.pid;
                if (callState[pid].isRecording) continue;
                std::wstring outputPath = BuildOutputPath(tp.name, audioFormat);
                DWORD micSessId = nextMicSessionId++;
                if (nextMicSessionId >= 0xFFFFFFFF) nextMicSessionId = MIC_SESSION_ID_BASE;

                bool procStarted = captureManager.StartCapture(pid, tp.name, outputPath, audioFormat, cfgStart.mp3Bitrate, false, L"", true);
                if (!procStarted) { Log(L"REC FAIL (forced): " + tp.name, LogLevel::LOG_ERROR); continue; }

                bool micStarted = false;
                MicInfo mic = GetDefaultMicrophone();
                if (mic.found) {
                    micStarted = captureManager.StartCaptureFromDevice(micSessId, mic.friendlyName, mic.deviceId, true,
                        outputPath, audioFormat, cfgStart.mp3Bitrate, false, true);
                    if (!micStarted) Log(L"Mic capture failed: " + mic.friendlyName, LogLevel::LOG_WARN);
                }

                bool mixedOk = captureManager.EnableMixedRecording(outputPath, audioFormat, cfgStart.mp3Bitrate);
                if (!mixedOk) {
                    captureManager.StopCapture(pid);
                    if (micStarted) captureManager.StopCapture(micSessId);
                    bool directStarted = captureManager.StartCapture(pid, tp.name, outputPath, audioFormat, cfgStart.mp3Bitrate, false, L"", false);
                    if (!directStarted) { Log(L"REC FAIL (forced fallback): " + tp.name, LogLevel::LOG_ERROR); continue; }
                    micSessId = 0;
                }

                callState[pid] = { true, outputPath, tp.name, pid, micStarted ? micSessId : (DWORD)0, mixedOk,
                                   std::chrono::steady_clock::now() };
                g_activeRecordings++;
                if (mixedOk) activeMixedCount++;
                Log(L"REC START (forced): " + tp.name + L" PID=" + std::to_wstring(pid) + L" -> " + outputPath);
                UpdateTrayTooltip();
                ShowTrayBalloon(L"Recording Started", tp.name + L" — forced recording");
                break; // start recording for first found target
            }
        }

        // Check for force stop request from UI
        if (g_forceStopRecording.exchange(false)) {
            Log(L"[UI] Force stop recording requested");
            for (auto& [pid, cs] : callState) {
                if (cs.isRecording) {
                    if (cs.mixedEnabled) {
                        activeMixedCount--;
                        if (activeMixedCount <= 0) { captureManager.DisableMixedRecording(); activeMixedCount = 0; }
                    }
                    if (cs.micSessionId != 0) captureManager.StopCapture(cs.micSessionId);
                    captureManager.StopCapture(pid);
                    Log(L"REC STOP (forced): " + cs.processName + L" PID=" + std::to_wstring(pid) + L" -> " + cs.outputPath);
                    g_activeRecordings--;
                }
            }
            callState.clear();
            silenceCounter.clear();
            startCounter.clear();
            peakHistory.clear();
            g_statusData.SetRecordings({});
            UpdateTrayTooltip();
        }

        AgentConfig config = GetConfigSnapshot();
        for (int i = 0; i < config.pollIntervalSeconds * 10 && g_running; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    captureManager.DisableMixedRecording();
    captureManager.StopAllCaptures();
    RoUninitialize();
}
