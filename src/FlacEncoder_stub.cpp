// FlacEncoder_stub.cpp — Заглушка для FlacEncoder
// Все методы возвращают false/ничего не делают.
// Используется вместо оригинального FlacEncoder.cpp (который требует libFLAC)

#include "FlacEncoder.h"

FlacEncoder::FlacEncoder() {}
FlacEncoder::~FlacEncoder() { Close(); }

bool FlacEncoder::Open(const std::wstring& /*filename*/, const WAVEFORMATEX* /*format*/, UINT32 /*compressionLevel*/) {
    return false; // FLAC не поддерживается в этой сборке
}

bool FlacEncoder::WriteData(const BYTE* /*data*/, UINT32 /*size*/) {
    return false;
}

void FlacEncoder::Close() {
    // Ничего не делаем
}
