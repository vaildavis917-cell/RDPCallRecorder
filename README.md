# RDP Call Recorder

> **[Русская версия / Russian version](README_RU.md)**

**Automatic call recording agent for RDP sessions.** Records WhatsApp, Telegram, and Viber calls with both caller and callee voices in a single audio file.

![RDP Call Recorder](src/app.ico)

## Features

- **Automatic call detection** — monitors WhatsApp, Telegram, and Viber processes for active audio sessions
- **Records both voices** — captures microphone (your voice) and application audio (callee voice) using mixed recording
- **Background operation** — runs silently in the system tray, no user interaction required
- **Auto-start** — registers itself in Windows startup on first launch
- **Settings GUI** — change recording folder, audio format, target processes, and more at any time
- **Smart file naming** — `{Date}_{RDP-User}_{App}_{Time}.mp3` (e.g., `2026-02-06_Ivanov_WhatsApp_14-30-25.mp3`)
- **RDP-aware** — only monitors processes in the current RDP session
- **NSIS installer** — one-click setup with desktop shortcut and uninstaller
- **Configurable** — all settings stored in `config.ini`

## How It Works

1. Agent starts and runs in the background (system tray icon)
2. Every 2 seconds, checks if WhatsApp/Telegram/Viber have an active audio session (call in progress)
3. When a call is detected:
   - Starts capturing the application's audio output (callee voice)
   - Starts capturing the default microphone (your voice)
   - Mixes both streams into a single MP3 file
4. When the call ends (silence detected for 6 seconds), stops recording and saves the file
5. Waits for the next call

## Installation

### From Installer (Recommended)

1. Download `RDPCallRecorder_Setup.exe` from the [Releases](../../releases) page
2. Run the installer as Administrator on the RDP server
3. A desktop shortcut will be created — click it to open settings
4. Select your recording folder and click Save
5. Done! The agent will auto-start on every RDP login

### From Source

See [Build Instructions](#building-from-source) below.

## Settings

Double-click the tray icon or the desktop shortcut to open settings:

| Setting | Description | Default |
|---------|-------------|---------|
| Recording folder | Where to save recordings | `D:\CallRecordings` |
| Audio format | MP3 or WAV | MP3 |
| MP3 Bitrate | Audio quality (kbps) | 128 |
| Target processes | Comma-separated list | `WhatsApp.exe, Telegram.exe, Viber.exe` |
| Poll interval | How often to check for calls (seconds) | 2 |
| Silence threshold | How many silent polls before stopping | 3 |
| Enable logging | Write log file | Yes |
| Auto-start | Register in Windows startup | Yes |

## File Structure

Recordings are organized as:
```
{RecordingFolder}/
  {Username}/
    {Date}/
      2026-02-06_Ivanov_WhatsApp_14-30-25.mp3
      2026-02-06_Ivanov_Telegram_15-45-10.mp3
    logs/
      agent.log
```

## Building from Source

### Prerequisites

- **Windows 10/11 or Windows Server 2019+**
- **Visual Studio 2022/2026** with C++ Desktop Development workload
- **CMake 3.20+**
- **NSIS** (optional, for building the installer)

### Dependencies

This project uses the [AudioCapture](https://github.com/masonasons/AudioCapture) library. Clone it alongside this project:

```cmd
cd C:\Projects
git clone https://github.com/masonasons/AudioCapture.git
git clone https://github.com/YOUR_USERNAME/RDPCallRecorder.git
```

### Build Steps

1. **Copy stub headers** (to avoid Opus/FLAC dependencies):
```cmd
copy RDPCallRecorder\include\OpusEncoder.h AudioCapture\include\OpusEncoder.h
copy RDPCallRecorder\include\FlacEncoder.h AudioCapture\include\FlacEncoder.h
```

2. **Open x64 Native Tools Command Prompt for VS** and build:
```cmd
cd RDPCallRecorder
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUDIOCAPTURE_DIR=C:\Projects\AudioCapture
cmake --build .
```

3. **Build the installer** (optional):
```cmd
mkdir ..\installer\files
copy bin\RDPCallRecorder.exe ..\installer\files\
copy ..\config.ini ..\installer\files\
cd ..\installer
"C:\Program Files (x86)\NSIS\makensis.exe" installer.nsi
```

## Configuration File

The `config.ini` file is located next to the executable:

```ini
[Recording]
RecordingPath=D:\CallRecordings
AudioFormat=mp3
MP3Bitrate=128000

[Monitoring]
PollInterval=2
SilenceThreshold=3

[Processes]
TargetProcesses=WhatsApp.exe,Telegram.exe,Viber.exe

[Logging]
EnableLogging=true
LogLevel=INFO
MaxLogSizeMB=10

[Advanced]
HideConsole=true
AutoRegisterStartup=true
ProcessPriority=BelowNormal
```

## System Requirements

- Windows 10 Build 20348+ / Windows 11 / Windows Server 2019+
- WASAPI-compatible audio device
- Microphone (for recording your voice)

## Antivirus Note

Windows Defender may flag the application as `Behavior:Win32/Persistence.Alml` because it registers itself in Windows startup. This is a **false positive**. Add an exclusion for the installation folder:

```powershell
Add-MpPreference -ExclusionPath "C:\Program Files\RDPCallRecorder\"
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments

- [AudioCapture](https://github.com/masonasons/AudioCapture) library by masonasons — WASAPI audio capture, mixing, and encoding
