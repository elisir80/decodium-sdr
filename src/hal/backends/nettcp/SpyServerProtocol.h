// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — protocollo SpyServer (Airspy).
//
// Implementato da documentazione pubblica e osservazione del protocollo
// (CONSTITUTION §3): nessuna riga deriva da spyserver_client o da altri
// client esistenti.
//
// A differenza di rtl_tcp, che manda campioni appena connesso, SpyServer è un
// protocollo a messaggi: il client si presenta, il server descrive il device,
// e i campioni arrivano solo dopo che si è chiesto esplicitamente lo streaming.
// È più cerimonioso ma dice molto di più su cosa c'è dall'altra parte.
#pragma once

#include <QtGlobal>

namespace dsdr::hal::nettcp::spyserver {

/// Versione di protocollo dichiarata nell'handshake.
inline constexpr quint32 kProtocolVersion = (2u << 24) | (0u << 16) | 1700u;

inline constexpr quint16 kDefaultPort = 5555;

/// Intestazione comune a ogni messaggio del server.
struct MessageHeader
{
    quint32 protocolId;
    quint32 messageType;
    quint32 streamType;
    quint32 sequenceNumber;
    quint32 bodySize;
};

inline constexpr int kMessageHeaderSize = 20;

/// Comandi che il client può inviare.
enum class Command : quint32 {
    Hello = 0,
    SetSetting = 2,
    Ping = 3,
};

/// Impostazioni richiedibili con `SetSetting`.
enum class Setting : quint32 {
    StreamingMode = 0,
    StreamingEnabled = 1,
    GainMode = 2,
    IqFormat = 100,
    IqFrequency = 101,
    IqDecimation = 102,
    IqDigitalGain = 103,
    FftFormat = 200,
};

/// Tipi di messaggio inviati dal server.
enum class MessageType : quint32 {
    DeviceInfo = 0,
    ClientSync = 1,
    PongData = 2,
    ReadSetting = 3,
    Uint8Iq = 100,
    Int16Iq = 101,
    Int24Iq = 102,
    Float32Iq = 103,
};

/// Modalità di streaming.
enum class StreamingMode : quint32 {
    Iq = 1,
    Fft = 2,
    IqAndFft = 3,
};

/// Formato dei campioni richiesto al server.
enum class SampleFormat : quint32 {
    Invalid = 0,
    Uint8 = 1,
    Int16 = 2,
    Int24 = 3,
    Float32 = 4,
};

/// Descrizione del device, primo messaggio utile del server.
struct DeviceInfo
{
    quint32 deviceType = 0;
    quint32 deviceSerial = 0;
    quint32 maximumSampleRate = 0;
    quint32 maximumBandwidth = 0;
    quint32 decimationStageCount = 0;
    quint32 gainStageCount = 0;
    quint32 maximumGainIndex = 0;
    quint32 minimumFrequency = 0;
    quint32 maximumFrequency = 0;
    quint32 resolution = 0;
    quint32 minimumIqDecimation = 0;
    quint32 forcedIqFormat = 0;
};

inline constexpr int kDeviceInfoSize = 48;

/// Tipi di device noti, per dare un nome all'hardware remoto.
inline const char *deviceTypeName(quint32 type)
{
    switch (type) {
    case 1:  return "Airspy One";
    case 2:  return "Airspy HF+";
    case 3:  return "RTL-SDR";
    case 4:  return "Airspy Mini";
    default: return "SpyServer";
    }
}

} // namespace dsdr::hal::nettcp::spyserver
