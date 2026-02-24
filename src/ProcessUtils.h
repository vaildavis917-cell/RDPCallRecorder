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

// Snapshot-based process lookup (one CreateToolhelp32Snapshot per cycle)
struct ProcessSnapshot {
    std::map<DWORD, DWORD> parentMap;      // pid -> parent pid
    std::map<DWORD, std::wstring> nameMap;  // pid -> exe name
    void Refresh();
};

std::vector<FoundProcess> FindTargetProcesses(const AgentConfig& config);

// Legacy functions (each creates its own snapshot — avoid in hot path)
std::wstring GetProcessNameByPid(DWORD pid);
DWORD GetParentProcessId(DWORD pid);
bool IsChildOfProcess(DWORD childPid, DWORD parentPid);

// Snapshot-based functions (use these in the hot path)
std::wstring GetProcessNameByPid(DWORD pid, const ProcessSnapshot& snap);
DWORD GetParentProcessId(DWORD pid, const ProcessSnapshot& snap);
bool IsChildOfProcess(DWORD childPid, DWORD parentPid, const ProcessSnapshot& snap);
