// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — vocabolario comune delle sorgenti IQ di rete.
//
// `rtl_tcp` e SpyServer sono protocolli che si riconoscono interrogando la
// radio. Un flusso IQ grezzo no: deve dichiarare esplicitamente trasporto,
// formato e frequenza di campionamento. Tenerli in un tipo separato evita di
// trasformare il backend in un insieme di stringhe magiche.
#pragma once

#include <QString>

namespace dsdr::hal::nettcp {

enum class NetProtocol {
    None,
    RtlTcp,
    SpyServer,
    RawTcp,
    RawUdp,
    SdrppServer,
};

enum class IqSampleFormat {
    Int8,
    Int16,
    Int32,
    Float32,
};

inline QString iqSampleFormatName(IqSampleFormat format)
{
    switch (format) {
    case IqSampleFormat::Int8:    return QStringLiteral("Int8");
    case IqSampleFormat::Int16:   return QStringLiteral("Int16");
    case IqSampleFormat::Int32:   return QStringLiteral("Int32");
    case IqSampleFormat::Float32: return QStringLiteral("Float32");
    }
    return QStringLiteral("Int16");
}

inline int bytesPerIqFrame(IqSampleFormat format)
{
    switch (format) {
    case IqSampleFormat::Int8:    return 2;
    case IqSampleFormat::Int16:   return 4;
    case IqSampleFormat::Int32:   return 8;
    case IqSampleFormat::Float32: return 8;
    }
    return 4;
}

} // namespace dsdr::hal::nettcp
