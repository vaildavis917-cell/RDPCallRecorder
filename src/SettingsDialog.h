#pragma once
#include <windows.h>

void ShowSettingsDialog(HWND hParent);
LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
