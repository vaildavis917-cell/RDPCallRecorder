#include "MainPanel.h"
#include "Globals.h"
#include "Config.h"
#include "Utils.h"
#include "Logger.h"
#include "AutoUpdate.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
// StatusData implementation (thread-safe shared state)
// ============================================================
StatusData g_statusData;

void StatusData::SetRecordings(const std::vector<ActiveRecordingInfo>& recs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recordings = recs;
}

std::vector<ActiveRecordingInfo> StatusData::GetRecordings() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recordings;
}

void StatusData::PushLogLine(const std::wstring& line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logRing.push_back(line);
    if (m_logRing.size() > MAX_LOG_LINES)
        m_logRing.erase(m_logRing.begin());
}

std::vector<std::wstring> StatusData::GetLogLines() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_logRing;
}

// ============================================================
// Constants
// ============================================================
static const int PANEL_WIDTH = 560;
static const int PANEL_HEIGHT = 520;
static const int TAB_MARGIN = 5;
static const UINT_PTR TIMER_REFRESH_STATUS = 5001;
static const int REFRESH_INTERVAL_MS = 1000;

// Tab indices
static const int TAB_STATUS = 0;
static const int TAB_SETTINGS = 1;

// Control IDs — Status tab
#define IDC_TAB_CONTROL     3001
#define IDC_STATUS_LABEL    3002
#define IDC_RECORDINGS_LIST 3003
#define IDC_LOG_EDIT        3004
#define IDC_VERSION_LABEL   3005

// Control IDs — Settings tab
#define IDC_PATH_EDIT       4001
#define IDC_PATH_BROWSE     4002
#define IDC_FORMAT_COMBO    4003
#define IDC_BITRATE_EDIT    4004
#define IDC_PROCESSES_EDIT  4005
#define IDC_POLL_EDIT       4006
#define IDC_SILENCE_EDIT    4007
#define IDC_SAVE_BTN        4008
#define IDC_CANCEL_BTN      4009
#define IDC_LOGGING_CHECK   4010
#define IDC_AUTOSTART_CHECK 4011
#define IDC_AUTOUPDATE_CHECK 4012

// ============================================================
// Panel state
// ============================================================
static HWND g_hPanel = nullptr;
static HWND g_hTabCtrl = nullptr;
static HFONT g_hPanelFont = nullptr;
static int g_currentTab = TAB_STATUS;

// Status tab controls
static HWND g_hStatusLabel = nullptr;
static HWND g_hRecordingsList = nullptr;
static HWND g_hLogEdit = nullptr;
static HWND g_hVersionLabel = nullptr;

// Settings tab controls
static HWND g_hPathEdit = nullptr;
static HWND g_hPathBrowse = nullptr;
static HWND g_hFormatCombo = nullptr;
static HWND g_hBitrateEdit = nullptr;
static HWND g_hProcessesEdit = nullptr;
static HWND g_hPollEdit = nullptr;
static HWND g_hSilenceEdit = nullptr;
static HWND g_hSaveBtn = nullptr;
static HWND g_hCancelBtn = nullptr;
static HWND g_hLoggingCheck = nullptr;
static HWND g_hAutostartCheck = nullptr;
static HWND g_hAutoUpdateCheck = nullptr;

// All settings controls for show/hide
static std::vector<HWND> g_statusControls;
static std::vector<HWND> g_settingsControls;

// ============================================================
// Helper: format duration from steady_clock time_point
// ============================================================
static std::wstring FormatDuration(std::chrono::steady_clock::time_point start) {
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto totalSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    int h = (int)(totalSec / 3600);
    int m = (int)((totalSec % 3600) / 60);
    int s = (int)(totalSec % 60);
    wchar_t buf[32];
    if (h > 0)
        swprintf_s(buf, 32, L"%d:%02d:%02d", h, m, s);
    else
        swprintf_s(buf, 32, L"%d:%02d", m, s);
    return std::wstring(buf);
}

// ============================================================
// Helper: extract filename from full path
// ============================================================
static std::wstring ExtractFilename(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) return path.substr(pos + 1);
    return path;
}

// ============================================================
// Helper: create controls with font
// ============================================================
static HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_LEFT,
        x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    return hw;
}

static HWND MakeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | ES_AUTOHSCROLL | extraStyle,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    return hw;
}

static HWND MakeButton(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | BS_PUSHBUTTON,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    return hw;
}

static HWND MakeCheckBox(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, bool checked) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | BS_AUTOCHECKBOX,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr), nullptr);
    if (checked) SendMessageW(hw, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    return hw;
}

// ============================================================
// Show/hide tab controls
// ============================================================
static void SwitchTab(int tab) {
    g_currentTab = tab;
    int statusShow = (tab == TAB_STATUS) ? SW_SHOW : SW_HIDE;
    int settingsShow = (tab == TAB_SETTINGS) ? SW_SHOW : SW_HIDE;

    for (HWND hw : g_statusControls) ShowWindow(hw, statusShow);
    for (HWND hw : g_settingsControls) ShowWindow(hw, settingsShow);

    if (tab == TAB_STATUS) {
        // Force immediate refresh
        SendMessageW(g_hPanel, WM_TIMER, TIMER_REFRESH_STATUS, 0);
    }
}

// ============================================================
// Refresh Status tab data
// ============================================================
static void RefreshStatusTab() {
    if (!g_hPanel || g_currentTab != TAB_STATUS) return;

    // Update status label
    int count = g_activeRecordings.load();
    std::wstring statusText;
    if (count > 0)
        statusText = L"  Status: Recording (" + std::to_wstring(count) + L" active)";
    else
        statusText = L"  Status: Monitoring...";
    SetWindowTextW(g_hStatusLabel, statusText.c_str());

    // Update recordings ListView
    auto recordings = g_statusData.GetRecordings();
    int itemCount = (int)SendMessageW(g_hRecordingsList, LVM_GETITEMCOUNT, 0, 0);

    // Clear and repopulate (simple approach, fine for <10 items)
    SendMessageW(g_hRecordingsList, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < recordings.size(); i++) {
        const auto& rec = recordings[i];

        // App name (strip .exe)
        std::wstring appName = rec.processName;
        size_t dotPos = appName.rfind(L'.');
        if (dotPos != std::wstring::npos) appName = appName.substr(0, dotPos);

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<wchar_t*>(appName.c_str());
        SendMessageW(g_hRecordingsList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        // PID
        std::wstring pidStr = std::to_wstring(rec.pid);
        lvi.iSubItem = 1;
        lvi.pszText = const_cast<wchar_t*>(pidStr.c_str());
        SendMessageW(g_hRecordingsList, LVM_SETITEMW, 0, (LPARAM)&lvi);

        // Duration
        std::wstring dur = FormatDuration(rec.startTime);
        lvi.iSubItem = 2;
        lvi.pszText = const_cast<wchar_t*>(dur.c_str());
        SendMessageW(g_hRecordingsList, LVM_SETITEMW, 0, (LPARAM)&lvi);

        // Mode
        std::wstring mode = rec.mixedEnabled ? L"Mixed" : L"App only";
        lvi.iSubItem = 3;
        lvi.pszText = const_cast<wchar_t*>(mode.c_str());
        SendMessageW(g_hRecordingsList, LVM_SETITEMW, 0, (LPARAM)&lvi);

        // File
        std::wstring fname = ExtractFilename(rec.outputPath);
        lvi.iSubItem = 4;
        lvi.pszText = const_cast<wchar_t*>(fname.c_str());
        SendMessageW(g_hRecordingsList, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }

    // Update log view
    auto logLines = g_statusData.GetLogLines();
    std::wstring logText;
    for (const auto& line : logLines) {
        logText += line;
    }
    SetWindowTextW(g_hLogEdit, logText.c_str());
    // Scroll to bottom
    SendMessageW(g_hLogEdit, EM_SETSEL, logText.size(), logText.size());
    SendMessageW(g_hLogEdit, EM_SCROLLCARET, 0, 0);
    // Deselect
    SendMessageW(g_hLogEdit, EM_SETSEL, (WPARAM)-1, 0);
}

// ============================================================
// Browse for folder
// ============================================================
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

// ============================================================
// Save settings from Settings tab
// ============================================================
static void SaveSettingsFromUI(HWND hWnd) {
    wchar_t buf[INI_BUFFER_SIZE];
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        GetWindowTextW(g_hPathEdit, buf, INI_BUFFER_SIZE);
        g_config.recordingPath = buf;

        int sel = (int)SendMessageW(g_hFormatCombo, CB_GETCURSEL, 0, 0);
        g_config.audioFormat = (sel == 1) ? L"wav" : L"mp3";

        GetWindowTextW(g_hBitrateEdit, buf, INI_BUFFER_SIZE);
        int bitrate = _wtoi(buf);
        if (bitrate > 0) g_config.mp3Bitrate = bitrate * 1000;

        GetWindowTextW(g_hProcessesEdit, buf, INI_BUFFER_SIZE);
        auto parsed = SplitString(buf, L',');
        if (!parsed.empty()) g_config.targetProcesses = parsed;

        GetWindowTextW(g_hPollEdit, buf, INI_BUFFER_SIZE);
        int poll = _wtoi(buf);
        if (poll >= 1) g_config.pollIntervalSeconds = poll;

        GetWindowTextW(g_hSilenceEdit, buf, INI_BUFFER_SIZE);
        int silence = _wtoi(buf);
        if (silence >= 1) g_config.silenceThreshold = silence;

        g_config.enableLogging = (SendMessageW(g_hLoggingCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
        g_config.autoRegisterStartup = (SendMessageW(g_hAutostartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
        g_config.autoUpdate = (SendMessageW(g_hAutoUpdateCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    SaveConfig();
    try { fs::create_directories(GetConfigSnapshot().recordingPath); } catch (...) {}
    Log(L"Settings saved. Path: " + GetConfigSnapshot().recordingPath);
    MessageBoxW(hWnd, L"Settings saved!", APP_TITLE, MB_OK | MB_ICONINFORMATION);
}

// ============================================================
// Create Status tab controls
// ============================================================
static void CreateStatusTabControls(HWND hWnd) {
    int tabTop = 35;  // Below tab control header
    int x = TAB_MARGIN + 5;
    int w = PANEL_WIDTH - 2 * TAB_MARGIN - 30;

    // Status label (big, bold-ish)
    g_hStatusLabel = MakeLabel(hWnd, L"  Status: Monitoring...", x, tabTop, w, 24);
    g_statusControls.push_back(g_hStatusLabel);

    // Active recordings ListView
    int lvTop = tabTop + 30;
    int lvHeight = 130;
    g_hRecordingsList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
        x, lvTop, w, lvHeight, hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RECORDINGS_LIST)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(g_hRecordingsList, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    ListView_SetExtendedListViewStyle(g_hRecordingsList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Add columns
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = const_cast<wchar_t*>(L"App");
    col.cx = 80; col.iSubItem = 0;
    ListView_InsertColumn(g_hRecordingsList, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"PID");
    col.cx = 55; col.iSubItem = 1;
    ListView_InsertColumn(g_hRecordingsList, 1, &col);

    col.pszText = const_cast<wchar_t*>(L"Duration");
    col.cx = 70; col.iSubItem = 2;
    ListView_InsertColumn(g_hRecordingsList, 2, &col);

    col.pszText = const_cast<wchar_t*>(L"Mode");
    col.cx = 65; col.iSubItem = 3;
    ListView_InsertColumn(g_hRecordingsList, 3, &col);

    col.pszText = const_cast<wchar_t*>(L"File");
    col.cx = 240; col.iSubItem = 4;
    ListView_InsertColumn(g_hRecordingsList, 4, &col);

    g_statusControls.push_back(g_hRecordingsList);

    // Log label
    int logLabelTop = lvTop + lvHeight + 8;
    HWND hLogLabel = MakeLabel(hWnd, L"Recent log:", x, logLabelTop, 100, 18);
    g_statusControls.push_back(hLogLabel);

    // Log edit (read-only, multiline, scrollable)
    int logTop = logLabelTop + 20;
    int logHeight = PANEL_HEIGHT - logTop - 60;
    g_hLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        x, logTop, w, logHeight, hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_EDIT)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(g_hLogEdit, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    g_statusControls.push_back(g_hLogEdit);

    // Version label at bottom
    std::wstring verText = L"v" + std::wstring(APP_VERSION);
    g_hVersionLabel = MakeLabel(hWnd, verText.c_str(), PANEL_WIDTH - 80, PANEL_HEIGHT - 50, 60, 18);
    g_statusControls.push_back(g_hVersionLabel);
}

// ============================================================
// Create Settings tab controls
// ============================================================
static void CreateSettingsTabControls(HWND hWnd) {
    AgentConfig config = GetConfigSnapshot();
    int tabTop = 40;
    int labelX = TAB_MARGIN + 10;
    int editX = 185;
    int editW = PANEL_WIDTH - editX - 30;
    int rowH = 28;
    int gap = 6;
    int y = tabTop;

    // Recording folder
    HWND lbl1 = MakeLabel(hWnd, L"Recording folder:", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl1);
    g_hPathEdit = MakeEdit(hWnd, config.recordingPath.c_str(), editX, y, editW - 40, 24, IDC_PATH_EDIT);
    g_settingsControls.push_back(g_hPathEdit);
    g_hPathBrowse = MakeButton(hWnd, L"...", editX + editW - 35, y, 35, 24, IDC_PATH_BROWSE);
    g_settingsControls.push_back(g_hPathBrowse);
    y += rowH + gap;

    // Audio format
    HWND lbl2 = MakeLabel(hWnd, L"Audio format:", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl2);
    g_hFormatCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        editX, y, 100, 120, hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FORMAT_COMBO)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(g_hFormatCombo, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);
    SendMessageW(g_hFormatCombo, CB_ADDSTRING, 0, (LPARAM)L"mp3");
    SendMessageW(g_hFormatCombo, CB_ADDSTRING, 0, (LPARAM)L"wav");
    std::wstring fmtLower = config.audioFormat;
    std::transform(fmtLower.begin(), fmtLower.end(), fmtLower.begin(), ::towlower);
    SendMessageW(g_hFormatCombo, CB_SETCURSEL, (fmtLower == L"wav") ? 1 : 0, 0);
    g_settingsControls.push_back(g_hFormatCombo);
    y += rowH + gap;

    // MP3 Bitrate
    HWND lbl3 = MakeLabel(hWnd, L"MP3 Bitrate (kbps):", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl3);
    g_hBitrateEdit = MakeEdit(hWnd, std::to_wstring(config.mp3Bitrate / 1000).c_str(), editX, y, 80, 24, IDC_BITRATE_EDIT);
    g_settingsControls.push_back(g_hBitrateEdit);
    y += rowH + gap;

    // Target processes
    std::wstring procStr;
    for (size_t i = 0; i < config.targetProcesses.size(); i++) {
        if (i > 0) procStr += L", ";
        procStr += config.targetProcesses[i];
    }
    HWND lbl4 = MakeLabel(hWnd, L"Target processes:", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl4);
    g_hProcessesEdit = MakeEdit(hWnd, procStr.c_str(), editX, y, editW, 24, IDC_PROCESSES_EDIT);
    g_settingsControls.push_back(g_hProcessesEdit);
    y += rowH + gap;

    // Poll interval
    HWND lbl5 = MakeLabel(hWnd, L"Poll interval (sec):", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl5);
    g_hPollEdit = MakeEdit(hWnd, std::to_wstring(config.pollIntervalSeconds).c_str(), editX, y, 80, 24, IDC_POLL_EDIT);
    g_settingsControls.push_back(g_hPollEdit);
    y += rowH + gap;

    // Silence threshold
    HWND lbl6 = MakeLabel(hWnd, L"Silence threshold:", labelX, y + 3, 160, 20);
    g_settingsControls.push_back(lbl6);
    g_hSilenceEdit = MakeEdit(hWnd, std::to_wstring(config.silenceThreshold).c_str(), editX, y, 80, 24, IDC_SILENCE_EDIT);
    g_settingsControls.push_back(g_hSilenceEdit);
    y += rowH + gap;

    // Checkboxes
    g_hLoggingCheck = MakeCheckBox(hWnd, L"Enable logging", labelX, y, 200, 24, IDC_LOGGING_CHECK, config.enableLogging);
    g_settingsControls.push_back(g_hLoggingCheck);
    y += rowH;

    g_hAutostartCheck = MakeCheckBox(hWnd, L"Auto-start with Windows", labelX, y, 250, 24, IDC_AUTOSTART_CHECK, config.autoRegisterStartup);
    g_settingsControls.push_back(g_hAutostartCheck);
    y += rowH;

    g_hAutoUpdateCheck = MakeCheckBox(hWnd, L"Auto-update from GitHub", labelX, y, 250, 24, IDC_AUTOUPDATE_CHECK, config.autoUpdate);
    g_settingsControls.push_back(g_hAutoUpdateCheck);
    y += rowH + gap + 5;

    // Buttons
    g_hSaveBtn = MakeButton(hWnd, L"Save", editX, y, 100, 30, IDC_SAVE_BTN);
    g_settingsControls.push_back(g_hSaveBtn);
    g_hCancelBtn = MakeButton(hWnd, L"Cancel", editX + 110, y, 100, 30, IDC_CANCEL_BTN);
    g_settingsControls.push_back(g_hCancelBtn);
}

// ============================================================
// Panel WndProc
// ============================================================
static LRESULT CALLBACK PanelWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create font
        if (g_hPanelFont) { DeleteObject(g_hPanelFont); g_hPanelFont = nullptr; }
        g_hPanelFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Init common controls for Tab + ListView
        INITCOMMONCONTROLSEX icex = {};
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icex);

        // Create Tab Control
        g_hTabCtrl = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, PANEL_WIDTH - 16, PANEL_HEIGHT - 40, hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_CONTROL)),
            GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hTabCtrl, WM_SETFONT, (WPARAM)g_hPanelFont, TRUE);

        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<wchar_t*>(L"Status");
        TabCtrl_InsertItem(g_hTabCtrl, TAB_STATUS, &tie);
        tie.pszText = const_cast<wchar_t*>(L"Settings");
        TabCtrl_InsertItem(g_hTabCtrl, TAB_SETTINGS, &tie);

        // Create controls for both tabs
        g_statusControls.clear();
        g_settingsControls.clear();
        CreateStatusTabControls(hWnd);
        CreateSettingsTabControls(hWnd);

        // Show Status tab by default
        SwitchTab(TAB_STATUS);

        // Start refresh timer
        SetTimer(hWnd, TIMER_REFRESH_STATUS, REFRESH_INTERVAL_MS, nullptr);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_REFRESH_STATUS) {
            RefreshStatusTab();
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR* pnm = reinterpret_cast<NMHDR*>(lParam);
        if (pnm->hwndFrom == g_hTabCtrl && pnm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g_hTabCtrl);
            SwitchTab(sel);
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_PATH_BROWSE) {
            std::wstring newPath = BrowseForFolder(hWnd);
            if (!newPath.empty()) SetWindowTextW(g_hPathEdit, newPath.c_str());
        }
        else if (wmId == IDC_SAVE_BTN) {
            SaveSettingsFromUI(hWnd);
        }
        else if (wmId == IDC_CANCEL_BTN) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_REFRESH_STATUS);
        if (g_hPanelFont) { DeleteObject(g_hPanelFont); g_hPanelFont = nullptr; }
        g_statusControls.clear();
        g_settingsControls.clear();
        g_hPanel = nullptr;
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// Public API
// ============================================================
void ShowMainPanel(HWND hParent) {
    ShowMainPanelOnTab(TAB_STATUS);
}

void ShowMainPanelOnTab(int tabIndex) {
    if (g_hPanel) {
        // Already open — switch to requested tab and bring to front
        if (g_hTabCtrl) {
            TabCtrl_SetCurSel(g_hTabCtrl, tabIndex);
            SwitchTab(tabIndex);
        }
        SetForegroundWindow(g_hPanel);
        return;
    }

    // Register window class (once)
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = PanelWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"RDPCallRecorderPanelClass";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    g_hPanel = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"RDPCallRecorderPanelClass",
        L"RDP Call Recorder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (screenW - PANEL_WIDTH) / 2, (screenH - PANEL_HEIGHT) / 2,
        PANEL_WIDTH, PANEL_HEIGHT,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    // Switch to requested tab
    if (g_hTabCtrl) {
        TabCtrl_SetCurSel(g_hTabCtrl, tabIndex);
        SwitchTab(tabIndex);
    }

    ShowWindow(g_hPanel, SW_SHOW);
    UpdateWindow(g_hPanel);
    SetForegroundWindow(g_hPanel);
}
