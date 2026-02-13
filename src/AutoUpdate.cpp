#include "AutoUpdate.h"
#include "Config.h"
#include "Logger.h"
#include "Globals.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <tuple>
#include <thread>
#include <chrono>

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

static bool WinHttpDownloadFile(const std::wstring& url, const std::wstring& localPath) {
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {}, pathBuf[2048] = {};
    urlComp.lpszHostName = hostBuf; urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = pathBuf; urlComp.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) return false;

    HINTERNET hSession = WinHttpOpen(L"RDPCallRecorder-AutoUpdate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD optionValue = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &optionValue, sizeof(optionValue));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    HANDLE hFile = CreateFileW(localPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead = 0, bytesWritten = 0;
    bool ok = true;
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, nullptr)) { ok = false; break; }

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return ok;
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

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring installerPath = std::wstring(tempPath) + L"RDPCallRecorder_Setup_update.exe";

    if (!WinHttpDownloadFile(downloadUrl, installerPath)) {
        Log(L"[UPDATE] Download failed", LogLevel::LOG_ERROR);
        MessageBoxW(nullptr, L"Download failed.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"\"" + installerPath + L"\" /S";
    if (CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        g_running = false;
        PostQuitMessage(0);
    } else {
        MessageBoxW(nullptr, L"Failed to launch installer.", APP_TITLE, MB_OK | MB_ICONERROR);
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
