#include "MonitorThread.h"
#include "Config.h"
#include "Logger.h"
#include "Utils.h"
#include "Globals.h"
#include "ProcessUtils.h"
#include "AudioMonitor.h"
#include "WindowUtils.h"
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

// ============================================================
// Call detection strategy (hybrid approach):
//
// START recording:
//   - Telegram: audio peak detected + IsTelegramInCall() (window title check)
//   - Other apps: audio peak detected (startThreshold cycles)
//
// STOP recording (key change — no more peak-based silence!):
//   - Telegram: IsTelegramInCall() returns false (call window closed)
//              OR AudioSessionState becomes Inactive (call ended but chat window stays open)
//   - Other apps: AudioSessionState becomes Inactive (Windows reports session ended)
//   - Safety net: MinRecordingSeconds — first N seconds never stop
//   - Fallback: SilenceThreshold still used if session stays Active but no audio
//     for a very long time (e.g. app bug keeps session active after call)
//
// This eliminates recording fragmentation because:
//   - AudioSessionState stays Active for the entire call duration
//   - Pauses in conversation do NOT change session state
//   - Session becomes Inactive only when the call truly ends
// ============================================================

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
    std::map<DWORD, int> silenceCounter;     // fallback silence counter
    std::map<DWORD, int> inactiveCounter;    // session inactive counter
    std::map<DWORD, int> startCounter;
    std::map<DWORD, std::deque<float>> peakHistory;
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
                bool sessionActive = audioMonitor.IsSessionActive(pid);

                // For Telegram: check if call window exists
                bool telegramCallActive = false;
                if (isTelegram) {
                    telegramCallActive = IsTelegramInCall(pid);
                }

                // Maintain rolling peak history
                auto& history = peakHistory[pid];
                history.push_back(currentPeak);
                if ((int)history.size() > config.telegramPeakHistorySize)
                    history.pop_front();

                // ===== START RECORDING =====
                if (!callState[pid].isRecording) {
                    bool shouldStart = false;

                    if (isTelegram) {
                        // Telegram: require BOTH audio peak AND call window present
                        if (hasRealAudio && telegramCallActive) {
                            startCounter[pid]++;
                            Log(L"[TG] Call detected: PID=" + std::to_wstring(pid) +
                                L" peak=" + std::to_wstring(currentPeak) +
                                L" callWindow=YES" +
                                L" count=" + std::to_wstring(startCounter[pid]) + L"/" + std::to_wstring(config.startThreshold), LogLevel::LOG_DEBUG);
                            if (startCounter[pid] >= config.startThreshold)
                                shouldStart = true;
                        } else {
                            if (startCounter[pid] > 0) startCounter[pid]--;
                            if (hasRealAudio && !telegramCallActive) {
                                Log(L"[TG] Audio but NO call window: PID=" + std::to_wstring(pid) +
                                    L" peak=" + std::to_wstring(currentPeak) +
                                    L" — ignoring (notification/voice msg)", LogLevel::LOG_DEBUG);
                            }
                        }
                    } else {
                        // Non-Telegram: use audio peak + session state for start
                        if (hasRealAudio && sessionActive) {
                            startCounter[pid]++;
                            Log(L"Audio detected: " + name + L" PID=" + std::to_wstring(pid) +
                                L" peak=" + std::to_wstring(currentPeak) +
                                L" sessionActive=YES" +
                                L" count=" + std::to_wstring(startCounter[pid]) + L"/" + std::to_wstring(config.startThreshold), LogLevel::LOG_DEBUG);
                            if (startCounter[pid] >= config.startThreshold)
                                shouldStart = true;
                        } else {
                            if (startCounter[pid] > 0) startCounter[pid]--;
                        }
                    }

                    if (!shouldStart) continue;

                    // === Begin recording ===
                    startCounter[pid] = 0;
                    silenceCounter[pid] = 0;
                    inactiveCounter[pid] = 0;
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

                } else if (callState[pid].isRecording) {
                    // ===== STOP RECORDING =====
                    bool shouldStop = false;
                    auto& cs = callState[pid];

                    // Calculate how long we've been recording
                    auto elapsed = std::chrono::steady_clock::now() - cs.startTime;
                    int elapsedSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                    bool pastMinDuration = (elapsedSeconds >= config.minRecordingSeconds);

                    if (isTelegram) {
                        // Telegram STOP: two signals (either one triggers stop)
                        // 1. Call window disappeared (telegramCallActive=false)
                        // 2. Audio session became Inactive (call ended but chat window stays open)
                        bool tgSessionInactive = !sessionActive;

                        if (!telegramCallActive) {
                            // Signal 1: call window gone
                            inactiveCounter[pid]++;
                            Log(L"[TG] Call window GONE: PID=" + std::to_wstring(pid) +
                                L" counter=" + std::to_wstring(inactiveCounter[pid]) +
                                L"/" + std::to_wstring(config.telegramSilenceCycles) +
                                L" elapsed=" + std::to_wstring(elapsedSeconds) + L"s", LogLevel::LOG_DEBUG);
                            if (inactiveCounter[pid] >= config.telegramSilenceCycles)
                                shouldStop = true;
                        } else if (tgSessionInactive) {
                            // Signal 2: window still open (chat window) but audio session is dead
                            // This happens when call ends but chat remains open
                            inactiveCounter[pid]++;
                            Log(L"[TG] Window open but session INACTIVE: PID=" + std::to_wstring(pid) +
                                L" counter=" + std::to_wstring(inactiveCounter[pid]) +
                                L"/" + std::to_wstring(config.telegramSilenceCycles) +
                                L" elapsed=" + std::to_wstring(elapsedSeconds) + L"s", LogLevel::LOG_DEBUG);
                            if (inactiveCounter[pid] >= config.telegramSilenceCycles)
                                shouldStop = true;
                        } else {
                            inactiveCounter[pid] = 0;
                            Log(L"[TG] Call active: PID=" + std::to_wstring(pid) +
                                L" peak=" + std::to_wstring(currentPeak) +
                                L" sessionActive=YES" +
                                L" elapsed=" + std::to_wstring(elapsedSeconds) + L"s", LogLevel::LOG_DEBUG);
                        }
                    } else {
                        // Non-Telegram: PRIMARY stop signal is AudioSessionState becoming Inactive
                        if (!sessionActive) {
                            // Session is Inactive — call likely ended
                            inactiveCounter[pid]++;
                            silenceCounter[pid] = 0;
                            Log(L"Session INACTIVE: " + name + L" PID=" + std::to_wstring(pid) +
                                L" inactiveCount=" + std::to_wstring(inactiveCounter[pid]) +
                                L" elapsed=" + std::to_wstring(elapsedSeconds) + L"s", LogLevel::LOG_DEBUG);

                            // Wait a few cycles to confirm (session might briefly go inactive)
                            if (inactiveCounter[pid] >= 3 && pastMinDuration)
                                shouldStop = true;
                        } else {
                            // Session still Active — call ongoing
                            inactiveCounter[pid] = 0;

                            // Fallback: if session stays Active but no audio for a very long time
                            // This handles edge cases like app bugs keeping session active
                            if (!hasRealAudio) {
                                silenceCounter[pid]++;
                                if (silenceCounter[pid] >= config.silenceThreshold && pastMinDuration) {
                                    Log(L"Fallback silence stop: " + name + L" PID=" + std::to_wstring(pid) +
                                        L" silenceCount=" + std::to_wstring(silenceCounter[pid]) +
                                        L" elapsed=" + std::to_wstring(elapsedSeconds) + L"s", LogLevel::LOG_DEBUG);
                                    shouldStop = true;
                                }
                            } else {
                                silenceCounter[pid] = 0;
                            }
                        }
                    }

                    // MinRecordingSeconds protection — don't stop too early
                    if (shouldStop && !pastMinDuration) {
                        Log(L"Stop blocked by MinRecordingSeconds: " + name +
                            L" elapsed=" + std::to_wstring(elapsedSeconds) +
                            L" min=" + std::to_wstring(config.minRecordingSeconds), LogLevel::LOG_DEBUG);
                        shouldStop = false;
                    }

                    if (shouldStop) {
                        if (cs.mixedEnabled) {
                            activeMixedCount--;
                            if (activeMixedCount <= 0) { captureManager.DisableMixedRecording(); activeMixedCount = 0; }
                        }
                        if (cs.micSessionId != 0) captureManager.StopCapture(cs.micSessionId);
                        captureManager.StopCapture(pid);
                        Log(L"REC STOP: " + cs.processName + L" PID=" + std::to_wstring(pid) +
                            L" duration=" + std::to_wstring(elapsedSeconds) + L"s -> " + cs.outputPath);
                        ShowTrayBalloon(L"Recording Stopped", cs.processName + L" — recording saved");
                        cs = {};
                        silenceCounter[pid] = 0;
                        inactiveCounter[pid] = 0;
                        peakHistory.erase(pid);
                        g_activeRecordings--;
                        UpdateTrayTooltip();
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
            for (DWORD p : toRemove) {
                callState.erase(p); silenceCounter.erase(p); startCounter.erase(p);
                peakHistory.erase(p); inactiveCounter.erase(p);
            }
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
                    Log(L"Mixed recording failed, falling back to process-only", LogLevel::LOG_WARN);
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
                break;
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
            inactiveCounter.clear();
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
