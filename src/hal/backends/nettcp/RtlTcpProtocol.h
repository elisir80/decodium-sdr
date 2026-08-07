// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — protocollo rtl_tcp.
//
// Implementato da documentazione pubblica e osservazione del protocollo
// (CONSTITUTION §3): nessun codice deriva da librosdr/rtl-sdr.
//
// Il protocollo è volutamente minimale. Dopo la connessione il server invia
// un saluto di 12 byte e poi, senza altre cerimonie, un flusso continuo di
// campioni I/Q a 8 bit non segnati. Il client comanda scrivendo pacchetti di
// 5 byte: un opcode e un intero a 32 bit big-endian.
#pragma once

#include <QtGlobal>

namespace dsdr::hal::nettcp {

/// Saluto iniziale del server: "RTL0", tipo di tuner, numero di passi di
/// guadagno. Tutti i campi interi sono big-endian.
struct RtlTcpGreeting
{
    char magic[4];          ///< "RTL0"
    quint32 tunerType;
    quint32 tunerGainCount;
};

static_assert(sizeof(RtlTcpGreeting) == 12, "il saluto rtl_tcp è di 12 byte");

inline constexpr int kGreetingSize = 12;
inline constexpr quint16 kDefaultPort = 1234;

/// Tipi di tuner dichiarati da rtl_tcp. Determinano la copertura in frequenza,
/// che è ciò che il core mostra all'utente attraverso le capability.
enum class TunerType : quint32 {
    Unknown = 0,
    E4000 = 1,
    FC0012 = 2,
    FC0013 = 3,
    FC2580 = 4,
    R820T = 5,
    R828D = 6,
};

/// Comandi. I valori sono quelli del protocollo, non vanno rinumerati.
enum class RtlTcpCommand : quint8 {
    SetFrequency = 0x01,
    SetSampleRate = 0x02,
    SetGainMode = 0x03,       ///< 0 = automatico, 1 = manuale
    SetGain = 0x04,           ///< in decimi di dB
    SetFrequencyCorrection = 0x05, ///< ppm
    SetIfGain = 0x06,
    SetTestMode = 0x07,
    SetAgcMode = 0x08,
    SetDirectSampling = 0x09,
    SetOffsetTuning = 0x0a,
    SetRtlXtal = 0x0b,
    SetTunerXtal = 0x0c,
    SetTunerGainByIndex = 0x0d,
    SetBiasTee = 0x0e,
};

struct TunerCoverage
{
    qint64 minHz;
    qint64 maxHz;
    const char *name;
};

/// Copertura dichiarata per tipo di tuner. Sono i limiti nominali dei
/// datasheet: meglio dichiarare meno e funzionare che promettere e fallire.
inline TunerCoverage coverageFor(TunerType tuner)
{
    switch (tuner) {
    case TunerType::E4000:  return {52'000'000, 2'200'000'000, "Elonics E4000"};
    case TunerType::FC0012: return {22'000'000, 948'600'000, "Fitipower FC0012"};
    case TunerType::FC0013: return {22'000'000, 1'100'000'000, "Fitipower FC0013"};
    case TunerType::FC2580: return {146'000'000, 924'000'000, "FCI FC2580"};
    case TunerType::R820T:  return {24'000'000, 1'766'000'000, "Rafael Micro R820T"};
    case TunerType::R828D:  return {24'000'000, 1'766'000'000, "Rafael Micro R828D"};
    case TunerType::Unknown:
        break;
    }
    return {24'000'000, 1'766'000'000, "sconosciuto"};
}

/// Frequenze di campionamento accettate da rtl_tcp. Fra 300 kS/s e 900 kS/s
/// il chip non è affidabile: quell'intervallo è escluso di proposito.
inline const double *supportedSampleRates(int &count)
{
    static const double rates[] = {
        250000.0, 1024000.0, 1536000.0, 1792000.0, 1920000.0,
        2048000.0, 2160000.0, 2560000.0, 2880000.0, 3200000.0,
    };
    count = static_cast<int>(sizeof(rates) / sizeof(rates[0]));
    return rates;
}

} // namespace dsdr::hal::nettcp
