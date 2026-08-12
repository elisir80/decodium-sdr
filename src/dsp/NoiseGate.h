// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — gate sulla voce, in testa alla catena TX (SPEC-005 §4.1).
//
// Toglie la stanza fra una frase e l'altra. Su una SSB stretta non si nota; su
// una eSSB larga, con il compressore che alza tutto quello che trova nelle
// pause, il respiro e il ronzio del computer diventano la parte più costante
// del segnale — e chi ascolta li sente per tutto il tempo in cui non si parla.
//
// Sta **prima** di tutto il resto, e non è una scelta di comodo: un gate messo
// dopo la compressione arriva quando il rumore è già stato alzato al livello
// della voce, e a quel punto non c'è più una soglia che li separi.
//
// Tre tempi, non uno. L'attacco deve essere immediato, o si perde l'attacco
// della prima consonante — che è la parte che rende una parola riconoscibile.
// La tenuta serve a non chiudersi dentro una frase, fra due sillabe. Il
// rilascio deve essere lento, o la coda di ogni parola viene tagliata di netto
// e si sente più del rumore che si voleva togliere.
#pragma once

#include <atomic>
#include <cstddef>

namespace dsdr::dsp {

class NoiseGate
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    /// La soglia in dBFS: sotto, il gate chiude. Si regola sul rumore della
    /// propria stanza, non su un valore di libro — è l'unico parametro che
    /// dipende da dove si sta.
    void setThresholdDb(double db) noexcept;
    double thresholdDb() const noexcept { return m_thresholdDb.load(std::memory_order_relaxed); }

    /// Quanto è aperto adesso, da 0 a 1. È la misura che dice se la soglia è
    /// troppo alta: un gate che sta chiuso mentre si parla si vede qui prima
    /// che si senta.
    double opening() const noexcept { return m_opening.load(std::memory_order_relaxed); }

    void process(float *audio, std::size_t frames) noexcept;

private:
    double m_sampleRate = 0.0;
    bool m_configured = false;

    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;
    std::size_t m_holdFrames = 0;

    double m_envelope = 0.0;
    double m_gain = 0.0;
    std::size_t m_held = 0;

    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_thresholdDb{-45.0};
    std::atomic<double> m_opening{0.0};
};

} // namespace dsdr::dsp
