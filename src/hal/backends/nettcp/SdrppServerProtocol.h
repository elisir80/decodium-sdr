// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — descrizione indipendente del protocollo SDR++ Server.
//
// Il server espone IQ CF32/PCM su TCP: tutti gli interi del framing sono
// little-endian e `size` include sempre l'header di otto byte.
#pragma once

#include <QtGlobal>

namespace dsdr::hal::nettcp::sdrpp {

constexpr quint32 kPacketHeaderSize = 8;
constexpr quint32 kCommandHeaderSize = 4;
constexpr quint32 kMaxPacketSize = 16u << 20;

enum class PacketType : quint32 {
    Command = 0,
    CommandAck = 1,
    Baseband = 2,
    BasebandCompressed = 3,
    Vfo = 4,
    Fft = 5,
    Error = 6,
};

enum class Command : quint32 {
    GetUi = 0,
    UiAction = 1,
    Start = 2,
    Stop = 3,
    SetFrequency = 4,
    GetSampleRate = 5,
    SetSampleType = 6,
    SetCompression = 7,
    SetSampleRate = 0x80,
    Disconnect = 0x81,
};

enum class PcmType : quint8 {
    Int8 = 0,
    Int16 = 1,
    Float32 = 2,
};

} // namespace dsdr::hal::nettcp::sdrpp
