#pragma once

#include <string>
#include <vector>
#include <map>
#include <windows.h>

struct AgentConfig;

struct FoundProcess {
    DWORD pid;
    std::wstring name;
};

// Cached process snapshot - one snapshot per poll cycle
struct ProcessSnapshot {
    std::map<DWORD, DWORD> parentMap;     // pid -> parent pid
    std::map<DWORD, std::wstring> nameMap; // pid -> exe name
    void Refresh();
};

std::vector<FoundProcess> FindTargetProcesses(const AgentConfig& config);

// Original functions (create snapshot each call) - kept for backward compatibility
std::wstring GetProcessNameByPid(DWORD pid);
DWORD GetParentProcessId(DWORD pid);
bool IsChildOfProcess(DWORD childPid, DWORD parentPid);

// Snapshot-based functions (use cached snapshot, no extra CreateToolhelp32Snapshot)
std::wstring GetProcessNameByPid(DWORD pid, const ProcessSnapshot& snap);
DWORD GetParentProcessId(DWORD pid, const ProcessSnapshot& snap);
bool IsChildOfProcess(DWORD childPid, DWORD parentPid, const ProcessSnapshot& snap);
