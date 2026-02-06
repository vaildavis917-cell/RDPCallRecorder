// FlacEncoder.h — Заглушка (stub) для сборки без FLAC
// Заменяет оригинальный заголовок, который требует <FLAC/stream_encoder.h>
// Определяет тот же интерфейс, но без внешних зависимостей.

#pragma once

#include <windows.h>
#include <mmreg.h>
#include <string>
#include <vector>

class FlacEncoder {
public:
    FlacEncoder();
    ~FlacEncoder();

    bool Open(const std::wstring& filename, const WAVEFORMATEX* format, UINT32 compressionLevel = 5);
    bool WriteData(const BYTE* data, UINT32 size);
    void Close();
    bool IsOpen() const { return false; }
};
