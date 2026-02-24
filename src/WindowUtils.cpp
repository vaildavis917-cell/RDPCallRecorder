#include "WindowUtils.h"
#include "Logger.h"
#include <algorithm>

struct EnumWindowsData {
    DWORD targetPid;
    std::vector<std::wstring> titles;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowsData*>(lParam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid != data->targetPid)
        return TRUE;  // continue enumeration

    // Only consider visible windows
    if (!IsWindowVisible(hwnd))
        return TRUE;

    // Get window title
    wchar_t titleBuf[512] = {};
    int len = GetWindowTextW(hwnd, titleBuf, 512);
    if (len > 0) {
        data->titles.push_back(std::wstring(titleBuf, len));
    }

    return TRUE;  // continue enumeration
}

std::vector<std::wstring> GetWindowTitlesForPid(DWORD pid) {
    EnumWindowsData data;
    data.targetPid = pid;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.titles;
}

bool IsTelegramInCall(DWORD pid) {
    auto titles = GetWindowTitlesForPid(pid);

    if (titles.empty()) {
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" no visible windows found", LogLevel::LOG_DEBUG);
        return false;
    }

    // Log all window titles for debugging
    for (const auto& t : titles) {
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" window: \"" + t + L"\"", LogLevel::LOG_DEBUG);
    }

    // Telegram main window title starts with "Telegram" (e.g. "Telegram", "Telegram (5)")
    // During a call, Telegram creates a SEPARATE window with the contact's name as title.
    // That call window title does NOT start with "Telegram".
    //
    // Strategy: if there is any visible window whose title does NOT start with "Telegram"
    // and is not empty, then a call is likely active.
    //
    // Edge cases to handle:
    // - Media player popup: title could be song name (but these are usually not separate windows)
    // - System tray tooltip: not a real window
    // - "TelegramDesktop" class windows with empty titles

    bool hasMainWindow = false;
    bool hasCallWindow = false;

    for (const auto& title : titles) {
        std::wstring lower = title;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        if (lower.find(L"telegram") == 0) {
            // Main window: "Telegram", "Telegram (3)", etc.
            hasMainWindow = true;
        } else {
            // Non-Telegram titled window — likely a call panel
            // The call window title is the contact's name
            hasCallWindow = true;
            Log(L"[TG-WIN] PID=" + std::to_wstring(pid) +
                L" CALL WINDOW detected: \"" + title + L"\"", LogLevel::LOG_DEBUG);
        }
    }

    // A call is active if we have both the main window AND a separate call window
    // (or at minimum, a call window exists)
    if (hasCallWindow) {
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" -> CALL ACTIVE", LogLevel::LOG_DEBUG);
        return true;
    }

    Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" -> no call detected", LogLevel::LOG_DEBUG);
    return false;
}
