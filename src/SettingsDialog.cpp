#include "SettingsDialog.h"
#include "Config.h"
#include "Utils.h"
#include "Logger.h"
#include "Globals.h"
#include <shlobj.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

#define IDC_PATH_EDIT      2001
#define IDC_PATH_BROWSE    2002
#define IDC_FORMAT_COMBO   2003
#define IDC_BITRATE_EDIT   2004
#define IDC_PROCESSES_EDIT 2005
#define IDC_POLL_EDIT      2006
#define IDC_SILENCE_EDIT   2007
#define IDC_SAVE_BTN       2008
#define IDC_CANCEL_BTN     2009
#define IDC_LOGGING_CHECK  2010
#define IDC_AUTOSTART_CHECK 2011

static HWND g_hSettingsDlg = nullptr;
static HFONT g_hSettingsFont = nullptr;

static void CreateLabel(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, p, nullptr, GetModuleHandle(nullptr), nullptr);
}

static HWND CreateEditBox(HWND p, const wchar_t* t, int x, int y, int w, int h, int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x, y, w, h, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
}

static HWND CreateButton(HWND p, const wchar_t* t, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
}

static HWND CreateCheckBox(HWND p, const wchar_t* t, int x, int y, int w, int h, int id, bool checked) {
    HWND hCheck = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    if (checked) SendMessageW(hCheck, BM_SETCHECK, BST_CHECKED, 0);
    return hCheck;
}

static HWND CreateComboBox(HWND p, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
}

static std::wstring BrowseForFolder(HWND hOwner) {
    wchar_t path[MAX_PATH] = { 0 };
    BROWSEINFOW bi = {};
    bi.hwndOwner = hOwner;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select recording folder:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) { SHGetPathFromIDListW(pidl, path); CoTaskMemFree(pidl); return std::wstring(path); }
    return L"";
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        if (g_hSettingsFont) { DeleteObject(g_hSettingsFont); g_hSettingsFont = nullptr; }
        g_hSettingsFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        AgentConfig config = GetConfigSnapshot();
        int y = 15, labelX = 15, editX = 180, editW = 280, rowH = 28, gap = 8;

        CreateLabel(hWnd, L"Recording folder:", labelX, y + 3, 160, 20);
        HWND hPathEdit = CreateEditBox(hWnd, config.recordingPath.c_str(), editX, y, editW - 40, 24, IDC_PATH_EDIT);
        CreateButton(hWnd, L"...", editX + editW - 35, y, 35, 24, IDC_PATH_BROWSE);
        SendMessageW(hPathEdit, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        CreateLabel(hWnd, L"Audio format:", labelX, y + 3, 160, 20);
        HWND hCombo = CreateComboBox(hWnd, editX, y, 100, 120, IDC_FORMAT_COMBO);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"mp3");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"wav");
        std::wstring fmtLower = config.audioFormat;
        std::transform(fmtLower.begin(), fmtLower.end(), fmtLower.begin(), ::towlower);
        SendMessageW(hCombo, CB_SETCURSEL, (fmtLower == L"wav") ? 1 : 0, 0);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        CreateLabel(hWnd, L"MP3 Bitrate (kbps):", labelX, y + 3, 160, 20);
        HWND hBitrate = CreateEditBox(hWnd, std::to_wstring(config.mp3Bitrate / 1000).c_str(), editX, y, 80, 24, IDC_BITRATE_EDIT);
        SendMessageW(hBitrate, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        std::wstring procStr;
        for (size_t i = 0; i < config.targetProcesses.size(); i++) {
            if (i > 0) procStr += L", ";
            procStr += config.targetProcesses[i];
        }
        CreateLabel(hWnd, L"Target processes:", labelX, y + 3, 160, 20);
        HWND hProc = CreateEditBox(hWnd, procStr.c_str(), editX, y, editW, 24, IDC_PROCESSES_EDIT);
        SendMessageW(hProc, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        CreateLabel(hWnd, L"Poll interval (sec):", labelX, y + 3, 160, 20);
        HWND hPoll = CreateEditBox(hWnd, std::to_wstring(config.pollIntervalSeconds).c_str(), editX, y, 80, 24, IDC_POLL_EDIT);
        SendMessageW(hPoll, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        CreateLabel(hWnd, L"Silence threshold:", labelX, y + 3, 160, 20);
        HWND hSilence = CreateEditBox(hWnd, std::to_wstring(config.silenceThreshold).c_str(), editX, y, 80, 24, IDC_SILENCE_EDIT);
        SendMessageW(hSilence, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        y += rowH + gap;

        CreateCheckBox(hWnd, L"Enable logging", labelX, y, 200, 24, IDC_LOGGING_CHECK, config.enableLogging);
        y += rowH;
        CreateCheckBox(hWnd, L"Auto-start with Windows", labelX, y, 250, 24, IDC_AUTOSTART_CHECK, config.autoRegisterStartup);
        y += rowH + gap + 5;

        HWND hSaveBtn = CreateButton(hWnd, L"Save", editX, y, 100, 30, IDC_SAVE_BTN);
        HWND hCancelBtn = CreateButton(hWnd, L"Cancel", editX + 110, y, 100, 30, IDC_CANCEL_BTN);
        SendMessageW(hSaveBtn, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);
        SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)g_hSettingsFont, TRUE);

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE); return TRUE;
        }, (LPARAM)g_hSettingsFont);
        return 0;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_PATH_BROWSE) {
            std::wstring newPath = BrowseForFolder(hWnd);
            if (!newPath.empty()) SetDlgItemTextW(hWnd, IDC_PATH_EDIT, newPath.c_str());
        }
        else if (wmId == IDC_SAVE_BTN) {
            wchar_t buf[INI_BUFFER_SIZE];
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                GetDlgItemTextW(hWnd, IDC_PATH_EDIT, buf, INI_BUFFER_SIZE); g_config.recordingPath = buf;
                int sel = (int)SendDlgItemMessageW(hWnd, IDC_FORMAT_COMBO, CB_GETCURSEL, 0, 0);
                g_config.audioFormat = (sel == 1) ? L"wav" : L"mp3";
                GetDlgItemTextW(hWnd, IDC_BITRATE_EDIT, buf, INI_BUFFER_SIZE);
                int bitrate = _wtoi(buf); if (bitrate > 0) g_config.mp3Bitrate = bitrate * 1000;
                GetDlgItemTextW(hWnd, IDC_PROCESSES_EDIT, buf, INI_BUFFER_SIZE);
                auto parsed = SplitString(buf, L','); if (!parsed.empty()) g_config.targetProcesses = parsed;
                GetDlgItemTextW(hWnd, IDC_POLL_EDIT, buf, INI_BUFFER_SIZE);
                int poll = _wtoi(buf); if (poll >= 1) g_config.pollIntervalSeconds = poll;
                GetDlgItemTextW(hWnd, IDC_SILENCE_EDIT, buf, INI_BUFFER_SIZE);
                int silence = _wtoi(buf); if (silence >= 1) g_config.silenceThreshold = silence;
                g_config.enableLogging = (SendDlgItemMessageW(hWnd, IDC_LOGGING_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_config.autoRegisterStartup = (SendDlgItemMessageW(hWnd, IDC_AUTOSTART_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            SaveConfig();
            try { fs::create_directories(GetConfigSnapshot().recordingPath); } catch (...) {}
            Log(L"Settings saved. Path: " + GetConfigSnapshot().recordingPath);
            MessageBoxW(hWnd, L"Settings saved!", APP_TITLE, MB_OK | MB_ICONINFORMATION);
            DestroyWindow(hWnd);
        }
        else if (wmId == IDC_CANCEL_BTN) DestroyWindow(hWnd);
        return 0;
    }
    case WM_DESTROY:
        if (g_hSettingsFont) { DeleteObject(g_hSettingsFont); g_hSettingsFont = nullptr; }
        g_hSettingsDlg = nullptr;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowSettingsDialog(HWND hParent) {
    (void)hParent;
    if (g_hSettingsDlg) { SetForegroundWindow(g_hSettingsDlg); return; }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"RDPCallRecorderSettingsClass";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
    g_hSettingsDlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"RDPCallRecorderSettingsClass",
        L"RDP Call Recorder - Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (screenW - SETTINGS_DLG_WIDTH) / 2, (screenH - SETTINGS_DLG_HEIGHT) / 2,
        SETTINGS_DLG_WIDTH, SETTINGS_DLG_HEIGHT, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(g_hSettingsDlg, SW_SHOW);
    UpdateWindow(g_hSettingsDlg);
    SetForegroundWindow(g_hSettingsDlg);
}
