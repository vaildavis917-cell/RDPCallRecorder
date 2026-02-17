#pragma once
#include <windows.h>

void CreateTrayIcon(HWND hWnd);
void UpdateTrayTooltip();
void RemoveTrayIcon();
void ShowTrayBalloon(const std::wstring& title, const std::wstring& msg);
void ShowTrayMenu(HWND hWnd);
