#pragma once

#include <windows.h>
#include <mmreg.h>
#include <string>

// Stub header — FLAC support disabled (no libFLAC dependency)
// Public API matches the full version so CaptureManager.h compiles.

class FlacEncoder {
public:
    FlacEncoder();
    ~FlacEncoder();

    bool Open(const std::wstring& filename, const WAVEFORMATEX* format, UINT32 compressionLevel = 5);
    bool WriteData(const BYTE* data, UINT32 size);
    void Close();
    bool IsOpen() const { return false; }
};
