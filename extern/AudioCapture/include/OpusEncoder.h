#pragma once

#include <windows.h>
#include <mmreg.h>
#include <string>

// Stub header — Opus support disabled (no libopus/libogg dependency)
// Public API matches the full version so CaptureManager.h compiles.

class OpusOggEncoder {
public:
    OpusOggEncoder();
    ~OpusOggEncoder();

    bool Open(const std::wstring& filename, const WAVEFORMATEX* format, UINT32 bitrate = 128000);
    bool WriteData(const BYTE* data, UINT32 size);
    void Close();
    bool IsOpen() const { return false; }
};
