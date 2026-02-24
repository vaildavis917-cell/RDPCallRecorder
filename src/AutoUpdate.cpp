#include "AutoUpdate.h"
#include "Config.h"
#include "Logger.h"
#include "Globals.h"
#include "TrayIcon.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <tuple>
#include <thread>
#include <chrono>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

static bool IsNewerVersion(const std::wstring& remote, const std::wstring& local) {
    auto parseVer = [](const std::wstring& v) -> std::tuple<int,int,int> {
        int major = 0, minor = 0, patch = 0;
        swscanf_s(v.c_str(), L"%d.%d.%d", &major, &minor, &patch);
        return {major, minor, patch};
    };
    return parseVer(remote) > parseVer(local);
}

static std::wstring StripVersionPrefix(const std::wstring& tag) {
    if (!tag.empty() && (tag[0] == L'v' || tag[0] == L'V')) return tag.substr(1);
    return tag;
}

static std::wstring ExtractJsonString(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return L"";
    pos = json.find(L':', pos + search.length());
    if (pos == std::wstring::npos) return L"";
    pos = json.find(L'"', pos + 1);
    if (pos == std::wstring::npos) return L"";
    size_t end = json.find(L'"', pos + 1);
    if (end == std::wstring::npos) return L"";
    return json.substr(pos + 1, end - pos - 1);
}

static std::wstring WinHttpGet(const std::wstring& host, const std::wstring& path) {
    std::wstring result;
    HINTERNET hSession = WinHttpOpen(L"RDPCallRecorder-AutoUpdate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return result;
    }

    std::string body;
    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        body.append(buffer, bytesRead);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!body.empty()) {
        int needed = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), (int)body.size(), nullptr, 0);
        if (needed > 0) {
            result.resize(needed);
            MultiByteToWideChar(CP_UTF8, 0, body.c_str(), (int)body.size(), &result[0], needed);
        }
    }
    return result;
}

// Download file with manual redirect handling (GitHub redirects to objects.githubusercontent.com)
static bool WinHttpDownloadFile(const std::wstring& url, const std::wstring& localPath) {
    std::wstring currentUrl = url;
    int maxRedirects = 5;

    for (int redirect = 0; redirect < maxRedirects; redirect++) {
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t hostBuf[256] = {}, pathBuf[4096] = {};
        urlComp.lpszHostName = hostBuf; urlComp.dwHostNameLength = 256;
        urlComp.lpszUrlPath = pathBuf; urlComp.dwUrlPathLength = 4096;

        if (!WinHttpCrackUrl(currentUrl.c_str(), (DWORD)currentUrl.length(), 0, &urlComp)) {
            Log(L"[UPDATE] Failed to parse URL: " + currentUrl, LogLevel::LOG_ERROR);
            return false;
        }

        HINTERNET hSession = WinHttpOpen(L"RDPCallRecorder-AutoUpdate/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

        // Disable automatic redirects — handle them manually to support cross-host redirects
        DWORD optionValue = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &optionValue, sizeof(optionValue));

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        // Handle redirects (301, 302, 303, 307, 308)
        if (statusCode >= 300 && statusCode < 400) {
            wchar_t locationBuf[4096] = {};
            DWORD locationSize = sizeof(locationBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX, locationBuf, &locationSize, WINHTTP_NO_HEADER_INDEX)) {
                currentUrl = locationBuf;
                Log(L"[UPDATE] Redirect " + std::to_wstring(statusCode) + L" -> " + currentUrl);
            } else {
                Log(L"[UPDATE] Redirect without Location header", LogLevel::LOG_ERROR);
                WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
                return false;
            }
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            continue; // Follow redirect
        }

        if (statusCode != 200) {
            Log(L"[UPDATE] Download HTTP " + std::to_wstring(statusCode), LogLevel::LOG_ERROR);
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }

        // Status 200 — download the file
        HANDLE hFile = CreateFileW(localPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }

        char buffer[8192];
        DWORD bytesRead = 0, bytesWritten = 0;
        DWORD totalBytes = 0;
        bool ok = true;
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, nullptr)) { ok = false; break; }
            totalBytes += bytesRead;
        }

        CloseHandle(hFile);
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);

        Log(L"[UPDATE] Downloaded " + std::to_wstring(totalBytes) + L" bytes");

        // Verify downloaded file is a valid PE executable (starts with "MZ")
        if (ok && totalBytes > 1024) {
            HANDLE hVerify = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hVerify != INVALID_HANDLE_VALUE) {
                char header[2] = {};
                DWORD headerRead = 0;
                ReadFile(hVerify, header, 2, &headerRead, nullptr);
                CloseHandle(hVerify);
                if (headerRead < 2 || header[0] != 'M' || header[1] != 'Z') {
                    Log(L"[UPDATE] Downloaded file is not a valid executable (no MZ header)", LogLevel::LOG_ERROR);
                    DeleteFileW(localPath.c_str());
                    return false;
                }
            }
        } else if (totalBytes <= 1024) {
            Log(L"[UPDATE] Downloaded file too small (" + std::to_wstring(totalBytes) + L" bytes)", LogLevel::LOG_ERROR);
            DeleteFileW(localPath.c_str());
            return false;
        }

        return ok;
    }

    Log(L"[UPDATE] Too many redirects", LogLevel::LOG_ERROR);
    return false;
}

// Create a helper .bat script that:
// 1. Waits for the current process to exit (by PID, not process name!)
// 2. Runs the installer silently
// 3. Restarts the app
// 4. Cleans up after itself
static bool CreateUpdateBat(const std::wstring& batPath, const std::wstring& installerPath,
                            const std::wstring& exePath, DWORD currentPid) {
    std::ofstream f(batPath, std::ios::trunc);
    if (!f.is_open()) return false;

    // Convert wide strings to narrow for the bat file
    char installerNarrow[MAX_PATH] = {}, exeNarrow[MAX_PATH] = {}, batNarrow[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, installerPath.c_str(), -1, installerNarrow, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, exePath.c_str(), -1, exeNarrow, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, batPath.c_str(), -1, batNarrow, MAX_PATH, nullptr, nullptr);

    f << "@echo off\r\n";
    f << "echo Waiting for RDP Call Recorder (PID " << currentPid << ") to close...\r\n";
    // Wait for the current process to exit (poll every 1 second, max 30 seconds)
    f << "set /a WAIT_COUNT=0\r\n";
    f << ":waitloop\r\n";
    f << "tasklist /FI \"PID eq " << currentPid << "\" 2>NUL | find /I \"" << currentPid << "\" >NUL\r\n";
    f << "if %ERRORLEVEL%==0 (\r\n";
    f << "    set /a WAIT_COUNT+=1\r\n";
    f << "    if %WAIT_COUNT% GEQ 30 goto forcekill\r\n";
    f << "    timeout /t 1 /nobreak >NUL\r\n";
    f << "    goto waitloop\r\n";
    f << ")\r\n";
    f << "goto doinstall\r\n";
    // Force kill ONLY our PID as safety net (not all instances!)
    f << ":forcekill\r\n";
    f << "echo Force killing PID " << currentPid << "...\r\n";
    f << "taskkill /F /PID " << currentPid << " >NUL 2>&1\r\n";
    f << "timeout /t 2 /nobreak >NUL\r\n";
    // Install
    f << ":doinstall\r\n";
    f << "echo Installing update...\r\n";
    f << "\"" << installerNarrow << "\" /S\r\n";
    // Wait for installer to finish
    f << "echo Starting RDP Call Recorder...\r\n";
    f << "timeout /t 3 /nobreak >NUL\r\n";
    f << "start \"\" \"" << exeNarrow << "\"\r\n";
    // Clean up
    f << "del \"" << installerNarrow << "\" >NUL 2>&1\r\n";
    f << "del \"%~f0\" >NUL 2>&1\r\n";
    f.close();
    return true;
}

void CheckForUpdates(bool showNoUpdateMsg) {
    Log(L"[UPDATE] Checking for updates...");
    std::wstring apiPath = std::wstring(L"/repos/") + GITHUB_REPO_OWNER + L"/" + GITHUB_REPO_NAME + L"/releases/latest";
    std::wstring response = WinHttpGet(L"api.github.com", apiPath);

    if (response.empty()) {
        Log(L"[UPDATE] Failed to fetch release info", LogLevel::LOG_ERROR);
        if (showNoUpdateMsg) MessageBoxW(nullptr, L"Failed to connect to GitHub.", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring tagName = StripVersionPrefix(ExtractJsonString(response, L"tag_name"));
    if (tagName.empty()) { Log(L"[UPDATE] Could not parse tag_name", LogLevel::LOG_ERROR); return; }

    Log(L"[UPDATE] Latest: " + tagName + L", current: " + APP_VERSION);

    if (!IsNewerVersion(tagName, APP_VERSION)) {
        Log(L"[UPDATE] Already up to date");
        if (showNoUpdateMsg)
            MessageBoxW(nullptr, (std::wstring(L"Latest version (") + APP_VERSION + L").").c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring msg = L"New version " + tagName + L" available.\n\nDownload and install?";
    if (MessageBoxW(nullptr, msg.c_str(), APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    std::wstring downloadUrl;
    size_t pos = 0;
    while ((pos = response.find(L"browser_download_url", pos)) != std::wstring::npos) {
        size_t urlStart = response.find(L"https://", pos);
        if (urlStart == std::wstring::npos) break;
        size_t urlEnd = response.find(L'"', urlStart);
        if (urlEnd == std::wstring::npos) break;
        std::wstring candidateUrl = response.substr(urlStart, urlEnd - urlStart);
        if (candidateUrl.find(L"Setup.exe") != std::wstring::npos ||
            candidateUrl.find(L"setup.exe") != std::wstring::npos ||
            candidateUrl.find(L".exe") != std::wstring::npos) {
            downloadUrl = candidateUrl; break;
        }
        pos = urlEnd;
    }

    if (downloadUrl.empty()) {
        Log(L"[UPDATE] No installer in release assets", LogLevel::LOG_ERROR);
        MessageBoxW(nullptr, L"No installer found. Update manually.", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }

    Log(L"[UPDATE] Downloading: " + downloadUrl);

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring installerPath = std::wstring(tempPath) + L"RDPCallRecorder_Setup_update.exe";
    std::wstring batPath = std::wstring(tempPath) + L"RDPCallRecorder_update.bat";

    if (!WinHttpDownloadFile(downloadUrl, installerPath)) {
        Log(L"[UPDATE] Download failed", LogLevel::LOG_ERROR);
        MessageBoxW(nullptr, L"Download failed.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    Log(L"[UPDATE] Download complete, preparing update...");

    // Get current exe path for restart after update
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    DWORD currentPid = GetCurrentProcessId();

    if (!CreateUpdateBat(batPath, installerPath, exePath, currentPid)) {
        Log(L"[UPDATE] Failed to create update script", LogLevel::LOG_ERROR);
        MessageBoxW(nullptr, L"Failed to prepare update.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    // Launch the bat script hidden
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"cmd.exe /c \"" + batPath + L"\"";
    if (CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Log(L"[UPDATE] Update script launched, shutting down for update...");
        // Clean shutdown — the bat script waits for us to exit
        g_running = false;
        RemoveTrayIcon();
        PostQuitMessage(0);
    } else {
        Log(L"[UPDATE] Failed to launch update script", LogLevel::LOG_ERROR);
        MessageBoxW(nullptr, L"Failed to launch update.", APP_TITLE, MB_OK | MB_ICONERROR);
        // Clean up
        DeleteFileW(batPath.c_str());
        DeleteFileW(installerPath.c_str());
    }
}

void AutoUpdateThread() {
    std::this_thread::sleep_for(std::chrono::minutes(2));
    while (g_running) {
        auto config = GetConfigSnapshot();
        if (config.autoUpdate) CheckForUpdates(false);
        int hours = config.updateCheckIntervalHours;
        for (int i = 0; i < hours * 60 && g_running; i++)
            std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}
