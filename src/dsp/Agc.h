// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — AGC multi-modalità con soglia AGC-T.
//
// IMPLEMENTAZIONE ORIGINALE (§11). L'algoritmo — rivelatore di inviluppo con
// attacco rapido, tempo di hang e decadimento dipendente dalla modalità, più
// una soglia che impedisce al guadagno di inseguire il rumore di fondo — è
// pubblico e non copyrightabile; il codice qui sotto è scritto da zero e non
// deriva da WDSP né da wcpAGC.c, che restano materiale di sola lettura finché
// la questione GPLv2/v3 non è risolta.
//
// AGC-T: la soglia è il livello sotto il quale il guadagno smette di salire.
// Alzandola l'operatore "abbassa" il rumore di banda senza toccare il volume,
// che è il comportamento atteso da chi viene da PowerSDR/piHPSDR.
#pragma once

#include "common/Types.h"
#include "dsp/DspTypes.h"

namespace dsdr::dsp {

class Agc
{
public:
    void configure(double sampleRate);

    void setMode(AgcMode mode);
    AgcMode mode() const noexcept { return m_mode; }

    /// Soglia AGC-T in dBFS (tipicamente fra -120 e -20).
    void setThresholdDb(double db);
    double thresholdDb() const noexcept { return m_thresholdDb; }

    /// Tempi esposti come millisecondi. Un decadimento pari a zero conserva
    /// il preset automatico Fast/Medium/Slow/Long.
    void setAttackMs(double ms);
    double attackMs() const noexcept { return m_attackMs; }
    void setDecayMs(double ms);
    double decayMs() const noexcept { return m_decayMs; }

    /// Guadagno massimo consentito in dB (limita l'amplificazione del rumore).
    void setMaxGainDb(double db);

    /// Guadagno fisso usato quando la modalità è Off.
    void setManualGainDb(double db);

    void reset() noexcept;

    void process(float *audio, std::size_t n) noexcept;

    /// Guadagno attualmente applicato, in dB (per il meter della UI).
    float gainDb() const noexcept;

    /// Inviluppo del segnale in dBFS (per il meter S).
    float envelopeDb() const noexcept;

private:
    void recomputeCoefficients();

    double m_sampleRate = 48000.0;
    AgcMode m_mode = AgcMode::Medium;
    double m_thresholdDb = -100.0;
    double m_maxGainDb = 90.0;
    double m_manualGainDb = 0.0;
    double m_attackMs = 2.0;
    double m_decayMs = 0.0;

    float m_attackCoeff = 0.0f;
    float m_decayCoeff = 0.0f;
    float m_gainSmoothCoeff = 0.0f;
    unsigned m_hangSamples = 0;

    float m_envelope = 0.0f;
    float m_gain = 1.0f;
    unsigned m_hangCounter = 0;

    static constexpr float kTargetLevel = 0.35f; ///< headroom verso il fondo scala
};

} // namespace dsdr::dsp
