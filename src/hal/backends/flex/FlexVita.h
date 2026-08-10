// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — i pacchetti VITA-49 di un FlexRadio.
//
// Il flusso dati di SmartSDR viaggia in VITA-49 su UDP. L'intestazione è
// quella dello standard; quello che serve sapere di FlexRadio sono tre numeri,
// e stanno nell'SDK che il costruttore pubblica:
//
//   OUI                 0x001C2D
//   information class   0x534C
//   packet class        un **campo di bit** che descrive il formato
//
// Il terzo è la parte che conta, ed è anche quella che evita di indovinare: il
// codice di classe non è un identificativo opaco da confrontare con una
// tabella, è la descrizione del carico. Dice quanti bit per campione, quanti
// canali, e se sono in virgola mobile IEEE-754. Un decodificatore che lo legge
// non ha bisogno di sapere in anticipo che cosa gli arriverà: se il pacchetto
// dichiara un formato che non sappiamo leggere, lo salta e lo dice.
//
// È la differenza fra un decodificatore che indovina — e quando sbaglia
// consegna campioni plausibili, cioè rumore che sembra una banda — e uno che
// verifica.
#pragma once

#include <QByteArray>

#include <cstddef>

namespace dsdr::hal::flex {

/// Costanti del costruttore, dal suo SDK.
inline constexpr quint32 kFlexOui = 0x001C2D;
inline constexpr quint16 kFlexInformationClass = 0x534C;

/// Il campo di bit del codice di classe.
inline constexpr quint16 kClassBitsPerSampleMask = 0x3 << 5;
inline constexpr quint16 kClassBitsPerSample32 = 0x3 << 5;
inline constexpr quint16 kClassChannelsMask = 0x3 << 7;
inline constexpr quint16 kClassChannelsPair = 0x3 << 7;   ///< due canali: I e Q
inline constexpr quint16 kClassIeee754 = 0x1 << 9;

/// Quel che si ricava dall'intestazione di un pacchetto.
struct VitaPacket
{
    bool valid = false;
    quint32 streamId = 0;
    quint16 packetClass = 0;
    quint8 packetCount = 0;    ///< contatore a 4 bit: serve a vedere i buchi
    int payloadOffset = 0;     ///< dove cominciano i dati
    int payloadBytes = 0;

    /// Coppie di valori a 32 bit: due canali, trentadue bit ciascuno. È la
    /// forma di un flusso IQ, qualunque sia il modo di scrivere i numeri.
    bool isPair32() const noexcept
    {
        return (packetClass & kClassBitsPerSampleMask) == kClassBitsPerSample32
            && (packetClass & kClassChannelsMask) == kClassChannelsPair;
    }

    /// Virgola mobile IEEE-754, invece che interi in virgola fissa.
    ///
    /// Non è una curiosità: **FlexLib stessa ha avuto questo difetto**,
    /// trattando come float un flusso che la radio mandava in virgola fissa.
    /// Il risultato di quello scambio non è silenzio — sono numeri assurdi che
    /// il DSP elabora diligentemente, cioè rumore che sembra una banda. Il bit
    /// c'è apposta: si legge, non si presume.
    bool isFloat() const noexcept { return (packetClass & kClassIeee754) != 0; }

    bool isFloatPair() const noexcept { return isPair32() && isFloat(); }
};

/// Legge l'intestazione. `valid` resta falso se il pacchetto non è di una
/// radio FlexRadio o non porta dati con identificativo di flusso.
VitaPacket parseVita(const QByteArray &datagram);

/// Estrae le coppie IQ di un pacchetto in coppie di float normalizzate,
/// interleaved I,Q. Restituisce il numero di coppie scritte, zero se il
/// pacchetto non porta un formato che sappiamo leggere.
///
/// `out` deve avere spazio per `packet.payloadBytes / 4` float.
std::size_t decodeIq(const QByteArray &datagram, const VitaPacket &packet, float *out);

} // namespace dsdr::hal::flex
