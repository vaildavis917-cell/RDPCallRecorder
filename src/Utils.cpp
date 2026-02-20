#include "Utils.h"
#include "Logger.h"
#include "Globals.h"
#include <shlobj.h>
#include <filesystem>
#include <algorithm>
#include <sstream>

#define SECURITY_WIN32
#include <security.h>

namespace fs = std::filesystem;

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), nullptr, 0);
        if (needed <= 0) return L"(conversion error)";
        std::wstring result(needed, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &result[0], needed);
        return result;
    }
    std::wstring result(needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], needed);
    return result;
}

std::vector<std::wstring> SplitString(const std::wstring& str, wchar_t delimiter) {
    std::vector<std::wstring> tokens;
    std::wstringstream ss(str);
    std::wstring token;
    while (std::getline(ss, token, delimiter)) {
        size_t start = token.find_first_not_of(L" \t");
        size_t end = token.find_last_not_of(L" \t");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}

static bool IsWhitespaceOrInvisible(wchar_t ch) {
    // Standard ASCII whitespace
    if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') return true;
    // Unicode whitespace and invisible characters
    if (ch == 0x00A0) return true;  // Non-breaking space
    if (ch == 0x2000) return true;  // En quad
    if (ch == 0x2001) return true;  // Em quad
    if (ch == 0x2002) return true;  // En space
    if (ch == 0x2003) return true;  // Em space
    if (ch == 0x2004) return true;  // Three-per-em space
    if (ch == 0x2005) return true;  // Four-per-em space
    if (ch == 0x2006) return true;  // Six-per-em space
    if (ch == 0x2007) return true;  // Figure space
    if (ch == 0x2008) return true;  // Punctuation space
    if (ch == 0x2009) return true;  // Thin space
    if (ch == 0x200A) return true;  // Hair space
    if (ch == 0x200B) return true;  // Zero-width space
    if (ch == 0x200C) return true;  // Zero-width non-joiner
    if (ch == 0x200D) return true;  // Zero-width joiner
    if (ch == 0x200E) return true;  // Left-to-right mark
    if (ch == 0x200F) return true;  // Right-to-left mark
    if (ch == 0x202F) return true;  // Narrow no-break space
    if (ch == 0x205F) return true;  // Medium mathematical space
    if (ch == 0x2060) return true;  // Word joiner
    if (ch == 0x3000) return true;  // Ideographic space
    if (ch == 0xFEFF) return true;  // BOM / zero-width no-break space
    return false;
}

std::wstring SanitizeForPath(const std::wstring& name) {
    std::wstring result;
    for (wchar_t ch : name) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            result += L'_';
        } else if (IsWhitespaceOrInvisible(ch) && ch != L' ') {
            // Replace Unicode whitespace with regular space (except normal space)
            result += L' ';
        } else {
            result += ch;
        }
    }
    while (!result.empty() && (result.front() == L'.' || result.front() == L' ')) result.erase(result.begin());
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) result.pop_back();
    // Collapse multiple consecutive spaces into one
    std::wstring collapsed;
    bool lastWasSpace = false;
    for (wchar_t ch : result) {
        if (ch == L' ') {
            if (!lastWasSpace) collapsed += ch;
            lastWasSpace = true;
        } else {
            collapsed += ch;
            lastWasSpace = false;
        }
    }
    result = collapsed;
    if (result.empty()) result = L"Unknown";
    return result;
}

std::wstring GetDefaultRecordingPath() {
    wchar_t userProfile[MAX_PATH] = { 0 };
    DWORD result = ExpandEnvironmentStringsW(L"%USERPROFILE%\\CallRecordings", userProfile, MAX_PATH);
    if (result > 0 && result <= MAX_PATH) return std::wstring(userProfile);
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
        return std::wstring(localAppData) + L"\\RDPCallRecorder\\Recordings";
    return L"C:\\CallRecordings";
}

std::wstring GetExePath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::wstring(exePath);
}

std::wstring GetConfigPath() {
    fs::path configPath = fs::path(GetExePath()).parent_path() / L"config.ini";
    return configPath.wstring();
}

LogLevel ParseLogLevel(const std::wstring& level) {
    std::wstring upper = level;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    if (upper == L"DEBUG") return LogLevel::LOG_DEBUG;
    if (upper == L"INFO")  return LogLevel::LOG_INFO;
    if (upper == L"WARN")  return LogLevel::LOG_WARN;
    if (upper == L"ERROR") return LogLevel::LOG_ERROR;
    return LogLevel::LOG_INFO;
}

void SetProcessPriorityFromConfig(const std::wstring& priority) {
    std::wstring upper = priority;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    DWORD priorityClass = NORMAL_PRIORITY_CLASS;
    if (upper == L"IDLE")             priorityClass = IDLE_PRIORITY_CLASS;
    else if (upper == L"BELOWNORMAL") priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    else if (upper == L"NORMAL")      priorityClass = NORMAL_PRIORITY_CLASS;
    else if (upper == L"ABOVENORMAL") priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (upper == L"HIGH")        priorityClass = HIGH_PRIORITY_CLASS;
    SetPriorityClass(GetCurrentProcess(), priorityClass);
}

static std::wstring g_cachedFullName;
static std::wstring g_cachedLoginName;
static bool g_namesCached = false;

std::wstring GetCurrentFullName() {
    if (g_namesCached) return g_cachedFullName;

    wchar_t fullName[NAME_BUFFER_SIZE] = { 0 };
    ULONG size = NAME_BUFFER_SIZE;
    if (GetUserNameExW(NameDisplay, fullName, &size) && size > 1 && wcslen(fullName) > 0) {
        g_cachedFullName = std::wstring(fullName);
        g_namesCached = true;
    }

    if (g_cachedFullName.empty()) {
        wchar_t username[NAME_BUFFER_SIZE] = { 0 };
        DWORD usize = NAME_BUFFER_SIZE;
        if (GetUserNameW(username, &usize)) g_cachedFullName = std::wstring(username);
        else g_cachedFullName = L"Unknown";
    }

    if (g_cachedLoginName.empty()) {
        wchar_t username[NAME_BUFFER_SIZE] = { 0 };
        DWORD usize = NAME_BUFFER_SIZE;
        if (GetUserNameW(username, &usize)) g_cachedLoginName = std::wstring(username);
        else g_cachedLoginName = L"Unknown";
    }
    g_namesCached = true;
    return g_cachedFullName;
}

std::wstring GetCurrentLoginName() {
    if (g_namesCached) return g_cachedLoginName;
    GetCurrentFullName();
    return g_cachedLoginName;
}

void RegisterAutoStart() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS) return;

    wchar_t existingPath[MAX_PATH] = { 0 };
    DWORD dataSize = sizeof(existingPath);
    DWORD dataType = 0;
    result = RegQueryValueExW(hKey, L"RDPCallRecorder", nullptr, &dataType,
                              reinterpret_cast<LPBYTE>(existingPath), &dataSize);

    std::wstring exePath = GetExePath();
    if (result == ERROR_SUCCESS && dataType == REG_SZ) {
        if (_wcsicmp(existingPath, exePath.c_str()) == 0) { RegCloseKey(hKey); return; }
    }

    RegSetValueExW(hKey, L"RDPCallRecorder", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(exePath.c_str()),
                   static_cast<DWORD>((exePath.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
}
