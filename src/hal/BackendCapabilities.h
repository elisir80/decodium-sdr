// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — descrittore di capacità (§4.2).
//
// È il cuore dell'universalità: la UI si genera da qui. Se `tx == None` il
// pulsante PTT NON esiste (non è disabilitato: non esiste). Se `coherentRx`
// compare il pannello QuadBeam. Se `demod == Device` la tendina dei filtri
// riflette i filtri della radio.
//
// CONSTITUTION §7: sopra la HAL è vietato ogni `if (backendId == "...")`.
// Se serve un ramo, serve una capability nuova.
#pragma once

#include "common/Types.h"

#include <QList>
#include <QMetaType>
#include <QStringList>

namespace dsdr::hal {

struct BackendCapabilities
{
    // ── Topologia ────────────────────────────────────────────────────────
    int maxRxChannels = 1;
    bool coherentRx = false;      ///< true solo per SDR One (QuadBeam)
    int maxPanadapters = 1;
    TxSupport tx = TxSupport::None;

    // ── Dove vive il DSP (§3.1) ──────────────────────────────────────────
    DspLocation demod = DspLocation::Client;
    DspLocation spectrum = DspLocation::Client;
    DspLocation agc = DspLocation::Client;

    /// Chi modula, in trasmissione. Non si ricava da `demod`: una radio
    /// tradizionale demodula **e** modula a bordo, ma esistono anche device
    /// che consegnano IQ in ricezione e vogliono audio in trasmissione. Da
    /// questo campo dipende che cosa il client scrive in `txStream()`, e
    /// sbagliarlo vuol dire modulare due volte.
    DspLocation modulation = DspLocation::Client;

    // ── Segnale ──────────────────────────────────────────────────────────
    QList<double> sampleRates;    ///< rate IQ disponibili (vuoto se server-DSP)
    double defaultSampleRate = 0.0;
    qint64 minFrequencyHz = 0;
    qint64 maxFrequencyHz = 0;
    bool hasHardwareFilters = false;
    bool hasPreamp = false;
    bool hasAttenuator = false;
    int adcBits = 0;

    /// Quanto guadagno il device può togliere su richiesta della catena, in dB.
    ///
    /// Zero — il default — significa «non offerto», e allora la guardia contro
    /// la saturazione (SPEC-003 §3) resta un indicatore: avverte e non
    /// corregge. È un solo numero perché dice due cose insieme, se il comando
    /// esiste e fin dove arriva, e un `bool` separato avrebbe permesso di
    /// dichiararlo disponibile con campo nullo.
    ///
    /// Con quale mezzo lo faccia — attenuatore, preamp, guadagno del tuner — è
    /// affar suo: il core chiede dei decibel in meno, non un componente.
    double maxGainReductionDb = 0.0;

    // ── Rete / sessione ──────────────────────────────────────────────────
    bool remoteCapable = false;   ///< già dietro rete (kiwi, nettcp, flex)
    bool multiClient = false;
    bool supportsRecording = true;

    // ── Suggerimenti per la UI ───────────────────────────────────────────
    QStringList nativePanels;     ///< componenti QML extra da caricare

    bool canTransmit() const noexcept { return tx != TxSupport::None; }
    bool isRawIq() const noexcept { return demod == DspLocation::Client; }

    /// Vero se il device copre la frequenza richiesta. `min == max == 0`
    /// significa "non dichiarato": in quel caso non si vincola nulla.
    bool coversFrequency(qint64 hz) const noexcept
    {
        if (minFrequencyHz == 0 && maxFrequencyHz == 0)
            return true;
        return hz >= minFrequencyHz && hz <= maxFrequencyHz;
    }
};

} // namespace dsdr::hal

Q_DECLARE_METATYPE(dsdr::hal::BackendCapabilities)
