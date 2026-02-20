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

echo.
echo ========================================
echo  RDPCallRecorder Auto-Build
echo ========================================
echo.

:: --- Check AudioCapture ---
if not exist "%AUDIOCAPTURE_DIR%\include\AudioCapture.h" (
    echo [ERROR] AudioCapture not found at: %AUDIOCAPTURE_DIR%
    echo.
    echo Clone it next to the project directory:
    echo   git clone https://github.com/masonasons/AudioCapture.git
    echo.
    echo Or edit AUDIOCAPTURE_DIR in this script.
    exit /b 1
)
echo [OK] AudioCapture found

:: --- Check NSIS ---
set "HAS_NSIS=0"
if exist "%NSIS_PATH%" (
    set "HAS_NSIS=1"
    echo [OK] NSIS found
) else (
    set "NSIS_PATH=C:\Program Files\NSIS\makensis.exe"
    if exist "!NSIS_PATH!" (
        set "HAS_NSIS=1"
        echo [OK] NSIS found
    ) else (
        echo [WARN] NSIS not found. Installer will not be built.
        echo Download from: https://nsis.sourceforge.io/Download
    )
)

:: --- Git pull ---
echo.
echo [1/5] Git pull...
cd /d "%PROJECT_DIR%"
git pull
if %ERRORLEVEL% neq 0 (
    echo [WARN] git pull failed, building from local files
)

:: --- Clean build if requested ---
if /i "%~1"=="clean" (
    echo.
    echo [2/5] Cleaning build directory...
    if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
    echo [OK] Build directory cleaned
) else (
    echo.
    echo [2/5] Incremental build (use "build.bat clean" for full rebuild)
)

:: --- CMake configure ---
if not exist "%BUILD_DIR%" md "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

if not exist "build.ninja" (
    echo.
    echo [3/5] CMake configure...
    cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR="%AUDIOCAPTURE_DIR%"
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] CMake configure failed
        exit /b 1
    )
    echo [OK] CMake configured
) else (
    echo.
    echo [3/5] CMake already configured (skipping)
)

:: --- Build ---
echo.
echo [4/5] Building with Ninja...
ninja
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)
echo [OK] Build successful

:: --- Verify exe exists ---
if not exist "%BUILD_DIR%\bin\RDPCallRecorder.exe" (
    echo [ERROR] RDPCallRecorder.exe not found in bin\
    exit /b 1
)

:: --- Copy exe to installer ---
echo.
echo [5/5] Building installer...
if not exist "%INSTALLER_DIR%\files" md "%INSTALLER_DIR%\files"
xcopy "%BUILD_DIR%\bin\RDPCallRecorder.exe" "%INSTALLER_DIR%\files\" /Y >NUL
echo [OK] Copied RDPCallRecorder.exe to installer\files\

:: --- Build NSIS installer ---
if "!HAS_NSIS!"=="1" (
    "!NSIS_PATH!" "%INSTALLER_DIR%\installer.nsi"
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] NSIS build failed
        exit /b 1
    )
    echo [OK] Installer built successfully
) else (
    echo [SKIP] NSIS not installed, skipping installer build
)

:: --- Done ---
echo.
echo ========================================
echo  BUILD COMPLETE!
echo ========================================
echo.
echo   EXE:       %BUILD_DIR%\bin\RDPCallRecorder.exe
if "!HAS_NSIS!"=="1" (
    echo   Installer: %INSTALLER_DIR%\RDPCallRecorder_Setup.exe
)
echo.

endlocal
