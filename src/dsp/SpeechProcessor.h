// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — catena audio di trasmissione: microfono, compressione, ALC.
//
// IMPLEMENTAZIONE ORIGINALE (§11). Il rivelatore d'inviluppo con attacco
// rapido e rilascio lento, la compressione sopra una soglia e il limitatore di
// picco a valle sono tecniche di dominio pubblico; questo codice è scritto da
// zero e non deriva da altri programmi SDR.
//
// L'ordine degli stadi non è arbitrario:
//
//   passa-alto → guadagno → compressore → limitatore
//
// Il passa-alto viene per primo perché il rumore di stanza sotto i 200 Hz non
// arriva mai all'altro capo ma pilota il compressore, che abbassa il guadagno
// per un segnale che nessuno sentirà. Il limitatore viene per ultimo perché è
// l'unico che deve poter promettere qualcosa di assoluto: sopra di lui non
// esce nulla oltre il fondo scala, e il modulatore può contarci.
#pragma once

#include "dsp/AudioHighPass.h"

#include <cstddef>

namespace dsdr::dsp {

class SpeechProcessor
{
public:
    bool configure(double sampleRate);

    /// Guadagno del microfono in dB. È il comando che l'operatore ha in mano.
    void setMicGainDb(double db);
    double micGainDb() const noexcept { return m_micGainDb; }

    /// Compressione in dB: quanti decibel di dinamica il compressore toglie
    /// al segnale forte. A zero lo stadio è trasparente.
    void setCompressionDb(double db);
    double compressionDb() const noexcept { return m_compressionDb; }

    /// Taglio del passa-alto d'ingresso. 200 Hz lascia intatta la voce e
    /// toglie il rimbombo della stanza.
    void setHighPassHz(double hz);

    void reset() noexcept;

    /// Elabora in loco un blocco mono normalizzato in ±1.
    void process(float *audio, std::size_t n) noexcept;

    /// Riduzione applicata dal compressore nell'ultimo blocco, in dB
    /// (positiva). È il numero che va sull'indicatore COMP.
    float lastCompressionDb() const noexcept { return m_lastCompressionDb; }

    /// Picco d'ingresso dell'ultimo blocco, prima di ogni guadagno: serve
    /// all'indicatore del microfono, che deve mostrare quanto parla
    /// l'operatore, non quanto lavora il compressore.
    float lastInputPeak() const noexcept { return m_lastInputPeak; }

    /// Vero se il limitatore è intervenuto nell'ultimo blocco: significa che
    /// il guadagno del microfono è troppo alto, e va detto.
    bool lastLimited() const noexcept { return m_lastLimited; }

private:
    void recomputeCoefficients();

    AudioHighPass m_highPass;
    double m_sampleRate = 48000.0;
    double m_micGainDb = 0.0;
    double m_compressionDb = 0.0;
    double m_highPassHz = 200.0;

    float m_linearGain = 1.0f;
    float m_threshold = 1.0f;   ///< inviluppo oltre il quale si comprime
    float m_slope = 0.0f;       ///< 1 - 1/ratio, la pendenza sopra la soglia
    float m_attack = 0.0f;
    float m_release = 0.0f;

    float m_envelope = 0.0f;
    float m_lastCompressionDb = 0.0f;
    float m_lastInputPeak = 0.0f;
    bool m_lastLimited = false;
};

} // namespace dsdr::dsp
