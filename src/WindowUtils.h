#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Get all window titles for a given process ID
std::vector<std::wstring> GetWindowTitlesForPid(DWORD pid);

// Check if Telegram is currently in a call by examining window titles.
// Telegram Desktop creates a separate call window with the contact's name as title.
// The main window title starts with "Telegram" (e.g. "Telegram" or "Telegram (3)").
// If a non-"Telegram" titled window exists for the process, a call is active.
// Returns true if any window title indicates an active call.
bool IsTelegramInCall(DWORD pid);
