# Changelog

## v2.7.2 (2026-02-24)

### Minor fix
- Fixed data race on `g_logLevel`: changed from plain `LogLevel` to `std::atomic<LogLevel>` to prevent undefined behavior when log level is read from multiple threads.

## v2.7.1 (2026-02-24)

### Fixed audio detection on non-default devices
- Fixed `GetProcessPeakLevel()` scanning only the default render device — now enumerates all active render devices. This fixes missed recordings in RDP sessions where the app audio plays on a non-default endpoint.
- Added periodic WASAPI enumerator reset (~30 seconds) to handle device changes during RDP reconnections.
- Removed stale cached device/session manager that could go stale after device changes.

### Fixed Telegram false positives
- Improved `IsTelegramInCall()` to filter out media viewer windows (`.jpg`, `.mp4`, `.webm`, etc.) that were incorrectly detected as call windows.

### Fixed process snapshot spam
- Added `ProcessSnapshot` struct — one `CreateToolhelp32Snapshot` per monitoring cycle instead of dozens. Snapshot is passed through `GetProcessPeakLevel`, `IsSessionActive`, and `DumpAudioSessions`.

### Fixed COM threading mismatch
- Changed main thread COM initialization from `COINIT_APARTMENTTHREADED` to `COINIT_MULTITHREADED` to match the monitor thread.

### Fixed duplicate recordings for parent/child processes
- Added deduplication: if both `WhatsApp.exe` and `WhatsApp.Root.exe` are found, only the child process (the one actually playing audio) is recorded.

### Fixed Logger performance
- Cached `enableLogging` flag via `std::atomic` — removed `GetConfigSnapshot()` mutex lock on every `Log()` call.
- Added periodic flush every 10 log lines (previously only flushed on WARN/ERROR, losing DEBUG/INFO on crash).

### Fixed UI performance
- Changed `StatusData::m_logRing` from `std::vector` to `std::deque` — `pop_front()` is now O(1) instead of O(n).
- Changed `RefreshStatusTab` log view from full rewrite to append-only — eliminates flicker and reduces CPU usage.
- Added `EM_SETLIMITTEXT` (100KB) to log edit control.

### Fixed auto-update JSON parser
- `ExtractJsonString` now verifies the matched key is followed by `:` — prevents false matches when the key appears as a value in the JSON body.

### Fixed tiny/empty recording files
- Recordings smaller than 10KB are automatically deleted after stop (likely false triggers).

### Fixed double config read in MonitorThread
- Removed redundant `GetConfigSnapshot()` call before the sleep loop — reuses the config from the beginning of the cycle.

### Fixed peakHistory memory growth
- Peak history for non-recording processes is now cleared when it exceeds 2x the configured window size.

### Improved Globals.h
- Changed all `static const` declarations to `inline constexpr` — eliminates symbol duplication across translation units.

### Bundled AudioCapture
- AudioCapture library is now included in `extern/AudioCapture/` inside the repository. No need to clone it separately.

### Hardened auto-update system
- Added `Accept: application/vnd.github+json` and `User-Agent` headers to GitHub API requests — prevents HTML responses on rate-limited or misconfigured endpoints.
- Added HTTP status code check in `WinHttpGet` — returns empty on non-200 (e.g. 403 rate limit) instead of trying to parse HTML as JSON.
- Reused `hSession` across redirects in `WinHttpDownloadFile` — eliminates redundant session creation on GitHub's 3-hop redirects.
- Fixed PID substring false match in update bat script — switched to CSV output with exact match.
- Added `ERRORLEVEL` check after silent installer — aborts and rolls back on failure.
- Added exe backup before install — old exe is copied to `.bak`, restored if install fails or exe is missing after install.
- Removed 2-minute startup delay — update check runs immediately on launch, then periodically.

### Configuration
- Added explicit `MaxRecordingSeconds=7200` to `config.ini` for transparency (2-hour safety limit).

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
