#include "Globals.h"
#include "Config.h"
#include "Logger.h"
#include "Utils.h"
#include "TrayIcon.h"
#include "SettingsDialog.h"
#include "MainPanel.h"
#include "MonitorThread.h"
#include "AutoUpdate.h"
#include "resource.h"
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "secur32.lib")

HWND g_hWndMain = nullptr;
NOTIFYICONDATAW g_nid = {};
HANDLE g_hMutex = nullptr;
std::atomic<bool> g_running(true);
std::atomic<int> g_activeRecordings(0);
std::atomic<bool> g_forceStopRecording(false);
std::thread g_monitorThread;
std::thread g_updateThread;
UINT WM_OPEN_SETTINGS_MSG = 0;

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_OPEN_SETTINGS_MSG && WM_OPEN_SETTINGS_MSG != 0) {
        ShowMainPanel(hWnd);
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu(hWnd);
        else if (lParam == WM_LBUTTONDBLCLK) ShowMainPanel(hWnd);
        return 0;
    case WM_SHOW_SETTINGS:
        ShowSettingsDialog(hWnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_SETTINGS: ShowSettingsDialog(hWnd); break;
        case IDM_STATUS: ShowMainPanelOnTab(0); break;
        case IDM_OPEN_FOLDER: ShellExecuteW(nullptr, L"open", GetConfigSnapshot().recordingPath.c_str(), nullptr, nullptr, SW_SHOW); break;
        case IDM_CHECK_UPDATE: CheckForUpdates(true); break;
        case IDM_EXIT: g_running = false; RemoveTrayIcon(); PostQuitMessage(0); break;
        }
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    WM_OPEN_SETTINGS_MSG = RegisterWindowMessageW(L"RDPCallRecorder_OpenSettings");

    HANDLE hMutexSingle = CreateMutexW(nullptr, FALSE, MUTEX_SINGLE_INSTANCE);
    if (hMutexSingle && GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, WM_OPEN_SETTINGS_MSG, 0, 0);
        CloseHandle(hMutexSingle);
        return 0;
    }

    LoadConfig(g_config);
    g_logLevel = ParseLogLevel(g_config.logLevel);
    bool firstLaunch = IsFirstLaunch();

    HWND hConsole = GetConsoleWindow();
    if (hConsole) ShowWindow(hConsole, SW_HIDE);

    SetProcessPriorityFromConfig(g_config.processPriority);
    if (g_config.autoRegisterStartup) RegisterAutoStart();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Init common controls (needed for Tab Control and ListView)
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExW(&wc);

    g_hWndMain = CreateWindowExW(0, WINDOW_CLASS_NAME, APP_TITLE,
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    CreateTrayIcon(g_hWndMain);

    Log(L"=== RDP Call Recorder v" + std::wstring(APP_VERSION) + L" started ===");
    Log(L"User: " + GetCurrentFullName() + L" (login: " + GetCurrentLoginName() + L")");
    Log(L"Recording path: " + GetConfigSnapshot().recordingPath);

    if (firstLaunch) ShowSettingsDialog(g_hWndMain);

    g_monitorThread = std::thread(MonitorThread);
    if (g_config.autoUpdate) g_updateThread = std::thread(AutoUpdateThread);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running = false;
    if (g_monitorThread.joinable()) g_monitorThread.join();
    if (g_updateThread.joinable()) g_updateThread.join();
    RemoveTrayIcon();
    CloseLogFile();
    if (hMutexSingle) CloseHandle(hMutexSingle);
    if (g_hMutex) CloseHandle(g_hMutex);
    CoUninitialize();
    return 0;
}

int main() { return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_HIDE); }
