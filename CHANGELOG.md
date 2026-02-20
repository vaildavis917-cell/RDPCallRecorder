# Changelog

## v2.6.5 (2026-02-20)

### Fixed recording fragmentation (short clips instead of continuous recording)
- **Switched to AudioSessionState-based stop detection** — recording now stops only when Windows reports the audio session as Inactive (call truly ended), not when there's a pause in conversation. This is the same approach used by the original DesktopCallRecorder.
- Previously, silence in conversation (6 seconds with no audio peaks) would stop recording, creating multiple short clips. Now the session stays Active for the entire call duration regardless of pauses.
- Added `IsSessionActive()` method using `IAudioSessionControl::GetState()` to check `AudioSessionStateActive/Inactive`
- Fallback silence detection still available for edge cases (app bugs keeping session active)

### Fixed WhatsApp Desktop not being detected
- Added `WhatsApp.Root.exe` to default `TargetProcesses` — WhatsApp Desktop from Microsoft Store runs as `WhatsApp.Root.exe`, not `WhatsApp.exe`

### Telegram call detection via window title
- Completely redesigned Telegram call detection using window title check instead of audio peaks
- During a call, Telegram Desktop creates a separate window titled with the contact's name (not "Telegram")
- This 100% filters out notification sounds, voice message playback, and video/music in chats
- START: recording starts only when BOTH audio peak AND Telegram call window are detected
- STOP: recording stops when call window disappears (call ended)
- Added `WindowUtils.h/cpp` with `IsTelegramInCall()` using `EnumWindows` + `GetWindowText` WinAPI

### Added MinRecordingSeconds protection
- New config parameter `MinRecordingSeconds=60` — recording cannot be stopped during the first 60 seconds
- Prevents premature stop from brief audio glitches at the start of a call

### Logging improvements
- Moved log files to install directory (`%LOCALAPPDATA%\RDPCallRecorder\logs\agent.log`)
- Added `InitLogger()` — log file is created immediately at startup
- Installer now creates `logs\` folder during installation
- Added session state logging for all stop decisions

## v2.6.4 (2026-02-17)

- **Fixed Telegram recording stop:** Telegram keeps audio session active with micro-peaks even after call ends. Implemented rolling average peak detection (window of 5 cycles) with Telegram-specific silence threshold (avg peak < 0.03). Recording now stops ~10 seconds after Telegram call ends without needing to close the app.
- Unified silence detection logic: merged `hasRealAudio && isRecording` and `!hasRealAudio && isRecording` branches into a single `isRecording` block with per-app silence strategy
- Added detailed Telegram debug logging (`[TG]` prefix) for peak levels and silence counter
- Updated auto-update version to v2.6.4

## v2.6.3 (2026-02-17)

- Added **Start Recording** button on Status tab for manual recording control
- Added **Stop Recording** button on Status tab to force-stop active recordings
- Added **balloon notifications** in system tray when recording starts and stops
- Fixed audio detection — changed startCounter from hard reset to gradual decay, preventing missed recordings during real calls with brief pauses
- Fixed auto-update — installer now kills running process before overwriting exe
- Updated README with new features and build instructions

## v2.6.2 (2026-02-17)

- Added **Stop Recording** button on Status tab
- Added `taskkill` to NSIS installer to prevent "file in use" errors during updates
- Improved auto-update bat script with forced process termination as safety net

## v2.6.1 (2026-02-17)

- **Refactored** monolithic `main.cpp` (2079 lines) into 14 modular source files
- Added **Status panel** with tabbed UI (Status + Settings tabs)
- Added **live log viewer** with last 100 lines and auto-scroll
- Added **active recordings list** showing App, PID, Duration, Mode, File
- Added **auto-update** via GitHub Releases API with silent install
- Fixed `shellapi.h` missing include for `NOTIFYICONDATAW`
- Fixed `objbase.h` missing include for `CoInitializeEx`

## v2.6 (2026-02-06)

- Initial release with automatic call detection and recording
- Mixed recording (microphone + application audio) in a single file
- Support for WhatsApp, Telegram, and Viber
- System tray icon with context menu
- Settings dialog for configuration
- NSIS installer with user-level installation
- Smart file naming with user's Full Name
- RDP session awareness
- Log rotation
