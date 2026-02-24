#include "ProcessUtils.h"
#include "Config.h"
#include <tlhelp32.h>

std::vector<FoundProcess> FindTargetProcesses(const AgentConfig& config) {
    std::vector<FoundProcess> result;
    DWORD currentSessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            for (const auto& target : config.targetProcesses) {
                if (_wcsicmp(pe32.szExeFile, target.c_str()) == 0) {
                    DWORD processSessionId = 0;
                    ProcessIdToSessionId(pe32.th32ProcessID, &processSessionId);
                    if (processSessionId == currentSessionId) {
                        result.push_back({ pe32.th32ProcessID, pe32.szExeFile });
                    }
                }
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return result;
}

std::wstring GetProcessNameByPid(DWORD pid) {
    if (pid == 0) return L"(system)";
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return L"(unknown)";
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    std::wstring result = L"(unknown)";
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == pid) { result = pe32.szExeFile; break; }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return result;
}

DWORD GetParentProcessId(DWORD pid) {
    if (pid == 0) return 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    DWORD parentPid = 0;
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == pid) { parentPid = pe32.th32ParentProcessID; break; }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return parentPid;
}

bool IsChildOfProcess(DWORD childPid, DWORD parentPid) {
    DWORD current = childPid;
    for (int depth = 0; depth < 3; depth++) {
        DWORD parent = GetParentProcessId(current);
        if (parent == 0 || parent == current) return false;
        if (parent == parentPid) return true;
        current = parent;
    }
    return false;
}
