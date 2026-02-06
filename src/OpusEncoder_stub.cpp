// OpusEncoder_stub.cpp — Заглушка для OpusOggEncoder
// Все методы возвращают false/ничего не делают.
// Используется вместо оригинального OpusEncoder.cpp (который требует libopus/libogg)

#include "OpusEncoder.h"

OpusOggEncoder::OpusOggEncoder() {}
OpusOggEncoder::~OpusOggEncoder() { Close(); }

bool OpusOggEncoder::Open(const std::wstring& /*filename*/, const WAVEFORMATEX* /*format*/, UINT32 /*bitrate*/) {
    return false; // Opus не поддерживается в этой сборке
}

bool OpusOggEncoder::WriteData(const BYTE* /*data*/, UINT32 /*size*/) {
    return false;
}

void OpusOggEncoder::Close() {
    // Ничего не делаем
}
