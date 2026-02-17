# Changelog

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
