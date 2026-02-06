// OpusEncoder.h — Заглушка (stub) для сборки без Opus/Ogg
// Заменяет оригинальный заголовок, который требует <opus/opus.h> и <ogg/ogg.h>
// Определяет тот же интерфейс, но без внешних зависимостей.

#pragma once

#include <windows.h>
#include <mmreg.h>
#include <string>
#include <vector>

class OpusOggEncoder {
public:
    OpusOggEncoder();
    ~OpusOggEncoder();

    bool Open(const std::wstring& filename, const WAVEFORMATEX* format, UINT32 bitrate = 128000);
    bool WriteData(const BYTE* data, UINT32 size);
    void Close();
    bool IsOpen() const { return false; }
};
