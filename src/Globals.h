#pragma once

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>

#define WM_TRAYICON        (WM_USER + 1)
#define WM_SHOW_SETTINGS   (WM_USER + 2)
#define IDI_TRAY           1
#define IDM_SETTINGS       1001
#define IDM_OPEN_FOLDER    1002
#define IDM_STATUS         1003
#define IDM_EXIT           1004
#define IDM_CHECK_UPDATE   1005

inline constexpr wchar_t WINDOW_CLASS_NAME[] = L"RDPCallRecorderWndClass";
inline constexpr wchar_t APP_TITLE[] = L"RDP Call Recorder";
inline constexpr wchar_t APP_VERSION[] = L"2.7.1";
inline constexpr wchar_t GITHUB_REPO_OWNER[] = L"vaildavis917-cell";
inline constexpr wchar_t GITHUB_REPO_NAME[] = L"RDPCallRecorder";
inline constexpr int UPDATE_CHECK_INTERVAL_HOURS = 6;
inline constexpr wchar_t MUTEX_SINGLE_INSTANCE[] = L"Local\\RDPCallRecorder_SingleInstance";
inline constexpr DWORD MIC_SESSION_ID_BASE = 0xF0000000;

inline constexpr float AUDIO_PEAK_THRESHOLD = 0.01f;
inline constexpr int INI_BUFFER_SIZE = 1024;
inline constexpr int NAME_BUFFER_SIZE = 256;
inline constexpr int TRAY_TIP_MAX_LEN = 128;
inline constexpr int LOG_TIMESTAMP_BUF = 64;
inline constexpr int SETTINGS_DLG_WIDTH = 500;
inline constexpr int SETTINGS_DLG_HEIGHT = 420;
inline constexpr UINT32 MIN_MP3_BITRATE = 32000;
inline constexpr UINT32 MAX_MP3_BITRATE = 320000;

extern HWND g_hWndMain;
extern NOTIFYICONDATAW g_nid;
extern HANDLE g_hMutex;
extern std::atomic<bool> g_running;
extern std::atomic<int> g_activeRecordings;
extern std::atomic<bool> g_forceStopRecording;
extern std::atomic<bool> g_forceStartRecording;
extern std::thread g_monitorThread;
extern std::thread g_updateThread;
extern UINT WM_OPEN_SETTINGS_MSG;
