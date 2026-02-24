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

    for (const auto& t : titles) {
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" window: \"" + t + L"\"", LogLevel::LOG_DEBUG);
    }

    // Telegram Desktop call window characteristics:
    // - Title is a contact name (does NOT start with "Telegram")
    // - Title does NOT contain common non-call patterns
    //
    // Known FALSE POSITIVES to filter out:
    // - Media viewer: title is filename or empty
    // - Profile/info panels: merged into main window (same HWND, not separate)
    // - Forward dialog: typically modal, not separate top-level

    bool hasCallWindow = false;

    for (const auto& title : titles) {
        std::wstring lower = title;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        // Skip main Telegram window
        if (lower.find(L"telegram") == 0)
            continue;

        // Skip empty titles
        if (title.empty())
            continue;

        // Skip likely media viewer windows (file extensions in title)
        if (lower.find(L".jpg") != std::wstring::npos ||
            lower.find(L".jpeg") != std::wstring::npos ||
            lower.find(L".png") != std::wstring::npos ||
            lower.find(L".gif") != std::wstring::npos ||
            lower.find(L".mp4") != std::wstring::npos ||
            lower.find(L".webm") != std::wstring::npos ||
            lower.find(L".webp") != std::wstring::npos ||
            lower.find(L".mov") != std::wstring::npos ||
            lower.find(L".pdf") != std::wstring::npos ||
            lower.find(L".mp3") != std::wstring::npos ||
            lower.find(L".ogg") != std::wstring::npos) {
            Log(L"[TG-WIN] PID=" + std::to_wstring(pid) +
                L" SKIPPING media window: \"" + title + L"\"", LogLevel::LOG_DEBUG);
            continue;
        }

        // This looks like a call window — contact name as title
        hasCallWindow = true;
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) +
            L" CALL WINDOW detected: \"" + title + L"\"", LogLevel::LOG_DEBUG);
    }

    if (hasCallWindow) {
        Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" -> CALL ACTIVE", LogLevel::LOG_DEBUG);
        return true;
    }

    Log(L"[TG-WIN] PID=" + std::to_wstring(pid) + L" -> no call detected", LogLevel::LOG_DEBUG);
    return false;
}
