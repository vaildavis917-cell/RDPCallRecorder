@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: RDPCallRecorder Auto-Build Script
:: Usage: build.bat [clean]
::   clean - full rebuild (deletes build directory)
:: ============================================================

:: --- Configuration (edit these paths if needed) ---
set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "INSTALLER_DIR=%PROJECT_DIR%installer"
set "AUDIOCAPTURE_DIR=C:\Users\User\Documents\Projects\AudioCapture"
set "NSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe"

:: --- Colors ---
set "STEP=[36m"
set "OK=[32m"
set "ERR=[31m"
set "RESET=[0m"

echo.
echo %STEP%========================================%RESET%
echo %STEP% RDPCallRecorder Auto-Build%RESET%
echo %STEP%========================================%RESET%
echo.

:: --- Check AudioCapture ---
if not exist "%AUDIOCAPTURE_DIR%\include\AudioCapture.h" (
    echo %ERR%ERROR: AudioCapture not found at: %AUDIOCAPTURE_DIR%%RESET%
    echo.
    echo Clone it next to the project directory:
    echo   git clone https://github.com/masonasons/AudioCapture.git
    echo.
    echo Or edit AUDIOCAPTURE_DIR in this script.
    exit /b 1
)
echo %OK%[OK]%RESET% AudioCapture found

:: --- Check NSIS ---
if not exist "%NSIS_PATH%" (
    :: Try alternative path
    set "NSIS_PATH=C:\Program Files\NSIS\makensis.exe"
)
if not exist "!NSIS_PATH!" (
    echo %ERR%WARNING: NSIS not found. Installer will not be built.%RESET%
    echo Download from: https://nsis.sourceforge.io/Download
    set "HAS_NSIS=0"
) else (
    echo %OK%[OK]%RESET% NSIS found
    set "HAS_NSIS=1"
)

:: --- Git pull ---
echo.
echo %STEP%[1/5] Git pull...%RESET%
cd /d "%PROJECT_DIR%"
git pull
if %ERRORLEVEL% neq 0 (
    echo %ERR%WARNING: git pull failed, building from local files%RESET%
)

:: --- Clean build if requested ---
if /i "%~1"=="clean" (
    echo.
    echo %STEP%[2/5] Cleaning build directory...%RESET%
    if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
    echo %OK%[OK]%RESET% Build directory cleaned
) else (
    echo.
    echo %STEP%[2/5] Incremental build (use "build.bat clean" for full rebuild)%RESET%
)

:: --- CMake configure ---
if not exist "%BUILD_DIR%" md "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

if not exist "build.ninja" (
    echo.
    echo %STEP%[3/5] CMake configure...%RESET%
    cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR="%AUDIOCAPTURE_DIR%"
    if !ERRORLEVEL! neq 0 (
        echo %ERR%ERROR: CMake configure failed%RESET%
        exit /b 1
    )
    echo %OK%[OK]%RESET% CMake configured
) else (
    echo.
    echo %STEP%[3/5] CMake already configured (skipping)%RESET%
)

:: --- Build ---
echo.
echo %STEP%[4/5] Building with Ninja...%RESET%
ninja
if %ERRORLEVEL% neq 0 (
    echo.
    echo %ERR%ERROR: Build failed!%RESET%
    exit /b 1
)
echo %OK%[OK]%RESET% Build successful

:: --- Verify exe exists ---
if not exist "%BUILD_DIR%\bin\RDPCallRecorder.exe" (
    echo %ERR%ERROR: RDPCallRecorder.exe not found in bin\%RESET%
    exit /b 1
)

:: --- Copy exe to installer ---
echo.
echo %STEP%[5/5] Building installer...%RESET%
if not exist "%INSTALLER_DIR%\files" md "%INSTALLER_DIR%\files"
xcopy "%BUILD_DIR%\bin\RDPCallRecorder.exe" "%INSTALLER_DIR%\files\" /Y >NUL
echo %OK%[OK]%RESET% Copied RDPCallRecorder.exe to installer\files\

:: --- Build NSIS installer ---
if "%HAS_NSIS%"=="1" (
    "!NSIS_PATH!" "%INSTALLER_DIR%\installer.nsi"
    if !ERRORLEVEL! neq 0 (
        echo %ERR%ERROR: NSIS build failed%RESET%
        exit /b 1
    )
    echo %OK%[OK]%RESET% Installer built: %INSTALLER_DIR%\RDPCallRecorder_Setup.exe
) else (
    echo %ERR%[SKIP]%RESET% NSIS not installed, skipping installer build
)

:: --- Done ---
echo.
echo %STEP%========================================%RESET%
echo %OK% BUILD COMPLETE!%RESET%
echo %STEP%========================================%RESET%
echo.
echo   EXE:       %BUILD_DIR%\bin\RDPCallRecorder.exe
if "%HAS_NSIS%"=="1" (
    echo   Installer: %INSTALLER_DIR%\RDPCallRecorder_Setup.exe
)
echo.

endlocal
