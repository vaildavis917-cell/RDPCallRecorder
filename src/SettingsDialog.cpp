#include "SettingsDialog.h"
#include "MainPanel.h"

// ShowSettingsDialog now opens the MainPanel on the Settings tab (tab index 1)
void ShowSettingsDialog(HWND hParent) {
    (void)hParent;
    ShowMainPanelOnTab(1);  // 1 = Settings tab
}
