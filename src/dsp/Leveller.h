// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — leveller: un AGC lento sulla voce (SPEC-005 §4.1).
//
// Corregge la distanza dal microfono. Chi parla si avvicina e si allontana di
// continuo — si gira a guardare il computer, si appoggia allo schienale — e il
// livello cambia di dieci o quindici decibel senza che se ne accorga.
//
// Sta **prima** del compressore, e la ragione e' precisa: senza, il compressore
// deve fare due lavori diversi con la stessa manopola. Deve inseguire la
// distanza, che cambia in secondi, e domare le punte delle consonanti, che
// cambiano in millisecondi. Una costante di tempo sola non puo' fare bene
// entrambe: o pompa sulla voce o non insegue la distanza. Diviso in due, il
// leveller prende la lentezza e il compressore la velocita'.
//
// Il guadagno ha un tetto. Un AGC senza tetto, nelle pause, alza fino a
// portare il rumore di fondo al livello della voce — ed e' esattamente la cosa
// che il gate davanti serviva a togliere.
#pragma once

#include <atomic>
#include <cstddef>

namespace dsdr::dsp {

class Leveller
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    /// Il livello a cui portare la voce, in dBFS. Meno diciotto lascia dodici
    /// decibel di margine alle punte, che e' quanto ne ha una voce.
    void setTargetDb(double db) noexcept;
    double targetDb() const noexcept { return m_targetDb.load(std::memory_order_relaxed); }

    /// Quanto sta guadagnando adesso, in decibel. Un leveller che sta sempre
    /// al massimo dice che il microfono e' troppo lontano o troppo basso, ed e'
    /// una cosa che si risolve prima del DSP.
    double gainDb() const noexcept { return m_gainDb.load(std::memory_order_relaxed); }

    void process(float *audio, std::size_t frames) noexcept;

private:
    double m_sampleRate = 0.0;
    bool m_configured = false;

    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;

    double m_envelope = 0.0;
    double m_currentGainDb = 0.0;

    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_targetDb{-18.0};
    std::atomic<double> m_gainDb{0.0};
};

} // namespace dsdr::dsp
