; ============================================================
; RDP Call Recorder - NSIS Installer Script
; Version: 2.6.3
; User-level installation (no admin rights required)
; ============================================================
; Build instructions:
;   1. Install NSIS: https://nsis.sourceforge.io/Download
;   2. Place RDPCallRecorder.exe and config.ini in installer\files\
;   3. Run: makensis installer.nsi
;   4. Result: RDPCallRecorder_Setup.exe
; ============================================================

!include "MUI2.nsh"

; --- Main parameters ---
Name "RDP Call Recorder"
OutFile "RDPCallRecorder_Setup.exe"
InstallDir "$LOCALAPPDATA\RDPCallRecorder"
InstallDirRegKey HKCU "Software\RDPCallRecorder" "InstallDir"
RequestExecutionLevel user
Unicode true

; --- Metadata ---
VIProductVersion "2.6.3.0"
VIAddVersionKey "ProductName" "RDP Call Recorder"
VIAddVersionKey "CompanyName" "QC Department"
VIAddVersionKey "FileDescription" "Call Recording Agent for RDP Sessions"
VIAddVersionKey "FileVersion" "2.6.3"
VIAddVersionKey "LegalCopyright" "Internal Use Only"

; --- Interface ---
!define MUI_ABORTWARNING
!define MUI_ICON "app.ico"
!define MUI_UNICON "app.ico"

; --- Finish page: launch app after install ---
!define MUI_FINISHPAGE_RUN "$INSTDIR\RDPCallRecorder.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Запустить RDP Call Recorder (откроет настройки)"
!define MUI_FINISHPAGE_RUN_CHECKED

; --- Install pages ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; --- Uninstall pages ---
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; --- Language ---
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

; ============================================================
; Install section
; ============================================================
Section "Install"
    SetOutPath "$INSTDIR"

    ; Kill running instance before overwriting
    nsExec::ExecToLog 'taskkill /F /IM RDPCallRecorder.exe'
    Sleep 1500

    ; Copy files
    File "files\RDPCallRecorder.exe"
    File "files\config.ini"
    File "app.ico"

    ; Save install path to registry (HKCU - user level)
    WriteRegStr HKCU "Software\RDPCallRecorder" "InstallDir" "$INSTDIR"

    ; Autostart for current user (HKCU)
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
        "RDPCallRecorder" '"$INSTDIR\RDPCallRecorder.exe"'

    ; Create desktop shortcut (current user only)
    SetShellVarContext current
    CreateShortCut "$DESKTOP\RDP Call Recorder.lnk" \
        "$INSTDIR\RDPCallRecorder.exe" "" \
        "$INSTDIR\app.ico" 0 \
        SW_SHOWNORMAL "" "RDP Call Recorder"

    ; Create Start Menu shortcut (current user only)
    CreateDirectory "$SMPROGRAMS\RDP Call Recorder"
    CreateShortCut "$SMPROGRAMS\RDP Call Recorder\RDP Call Recorder.lnk" \
        "$INSTDIR\RDPCallRecorder.exe" "" \
        "$INSTDIR\app.ico" 0
    CreateShortCut "$SMPROGRAMS\RDP Call Recorder\Uninstall.lnk" \
        "$INSTDIR\Uninstall.exe"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Register in "Programs and Features" (HKCU - user level)
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "DisplayName" "RDP Call Recorder"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "DisplayVersion" "2.6.3"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "Publisher" "QC Department"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "DisplayIcon" "$INSTDIR\app.ico"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder" \
        "NoRepair" 1

SectionEnd

; ============================================================
; Uninstall section
; ============================================================
Section "Uninstall"

    ; Kill all running instances
    nsExec::ExecToLog 'taskkill /F /IM RDPCallRecorder.exe'

    ; Remove autostart (HKCU)
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RDPCallRecorder"

    ; Remove desktop shortcut (current user)
    SetShellVarContext current
    Delete "$DESKTOP\RDP Call Recorder.lnk"

    ; Remove Start Menu
    Delete "$SMPROGRAMS\RDP Call Recorder\RDP Call Recorder.lnk"
    Delete "$SMPROGRAMS\RDP Call Recorder\Uninstall.lnk"
    RMDir "$SMPROGRAMS\RDP Call Recorder"

    ; Remove files
    Delete "$INSTDIR\RDPCallRecorder.exe"
    Delete "$INSTDIR\config.ini"
    Delete "$INSTDIR\app.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove registry keys (HKCU)
    DeleteRegKey HKCU "Software\RDPCallRecorder"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RDPCallRecorder"

    ; NOTE: Recordings folder is NOT deleted - recordings are preserved!

SectionEnd
