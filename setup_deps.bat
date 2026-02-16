@echo off
REM ============================================================
REM RDP Call Recorder - Dependency Setup Script
REM Run this ONCE before building to patch AudioCapture stubs
REM ============================================================

set SCRIPT_DIR=%~dp0
set AC_DIR=%SCRIPT_DIR%..\AudioCapture

if not exist "%AC_DIR%\include\AudioCapture.h" (
    echo ERROR: AudioCapture not found at %AC_DIR%
    echo Clone it: git clone https://github.com/masonasons/AudioCapture.git
    echo Make sure it is next to the RDPCallRecorder folder.
    exit /b 1
)

echo Patching AudioCapture with stub headers...

copy /Y "%SCRIPT_DIR%include\OpusEncoder.h" "%AC_DIR%\include\OpusEncoder.h"
copy /Y "%SCRIPT_DIR%include\FlacEncoder.h" "%AC_DIR%\include\FlacEncoder.h"

echo Done. AudioCapture patched for build without Opus/FLAC dependencies.
echo.
echo Now you can build:
echo   mkdir build ^& cd build
echo   cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
echo   ninja
