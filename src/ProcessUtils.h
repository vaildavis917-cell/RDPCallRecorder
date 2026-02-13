#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct AgentConfig;

struct FoundProcess {
    DWORD pid;
    std::wstring name;
};

std::vector<FoundProcess> FindTargetProcesses(const AgentConfig& config);
std::wstring GetProcessNameByPid(DWORD pid);
DWORD GetParentProcessId(DWORD pid);
bool IsChildOfProcess(DWORD childPid, DWORD parentPid);
