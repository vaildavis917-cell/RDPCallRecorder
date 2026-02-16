#include "TrayIcon.h"
#include "Globals.h"
#include "Config.h"
#include "resource.h"
#include <shellapi.h>
#include <string>

void CreateTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = IDI_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"RDP Call Recorder - Monitoring...");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void UpdateTrayTooltip() {
    int count = g_activeRecordings.load();
    if (count > 0)
        swprintf_s(g_nid.szTip, TRAY_TIP_MAX_LEN, L"RDP Call Recorder - Recording (%d active)", count);
    else
        wcscpy_s(g_nid.szTip, L"RDP Call Recorder - Monitoring...");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    int count = g_activeRecordings.load();
    std::wstring statusText = (count > 0)
        ? L"Status: Recording (" + std::to_wstring(count) + L" active)"
        : L"Status: Monitoring";

    AppendMenuW(hMenu, MF_STRING, IDM_STATUS, statusText.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_FOLDER, L"Open recordings folder");
    AppendMenuW(hMenu, MF_STRING, IDM_CHECK_UPDATE, L"Check for updates...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}
