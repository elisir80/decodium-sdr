// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — demodulatori.
//
// Il demodulatore riceve il canale già portato a banda base dal DDC e già
// filtrato dal passa-banda complesso. Per SSB e le modalità digitali questo
// rende la demodulazione una semplice proiezione sulla parte reale: la
// selezione della banda laterale è avvenuta nel filtro (vedi ComplexFir).
#pragma once

#include "common/Types.h"
#include "dsp/DspTypes.h"

namespace dsdr::dsp {

/// Blocco DC a un polo: y[n] = x[n] − x[n−1] + a·y[n−1].
class DcBlocker
{
public:
    void configure(double sampleRate, double cornerHz = 20.0);
    void reset() noexcept;
    float process(float x) noexcept;

private:
    float m_a = 0.999f;
    float m_prevIn = 0.0f;
    float m_prevOut = 0.0f;
};

class Demodulator
{
public:
    void configure(double sampleRate);
    void setMode(DemodMode mode);
    DemodMode mode() const noexcept { return m_mode; }

    /// Deviazione usata per normalizzare l'uscita FM.
    void setFmDeviation(double hz);
    /// AGC lento della portante AM, distinto dall'AGC audio del canale.
    void setAmCarrierAgc(bool enabled);

    void reset() noexcept;

    /// Restituisce il numero di campioni audio prodotti (uno per campione IQ).
    std::size_t process(const Complex *in, std::size_t n, float *out) noexcept;

    /// Quanto lontano può andare a cercare la portante il PLL della AM
    /// sincrona, in hertz. Largo aggancia anche con la radio scentrata, ma
    /// può agganciarsi a un'interferenza vicina invece che alla portante.
    void setSamCaptureRangeHz(double hz) noexcept;

    /// Il PLL sta seguendo la portante: l'errore residuo è piccolo e stabile.
    /// Serve come spia — una AM sincrona sganciata suona peggio di una AM
    /// normale, e senza indicazione non si capisce perché.
    bool samLocked() const noexcept { return m_samLocked; }

    /// Errore di aggancio del PLL SAM, in Hz (diagnostica UI).
    float samLockErrorHz() const noexcept;

private:
    std::size_t demodSsb(const Complex *in, std::size_t n, float *out) noexcept;
    std::size_t demodAm(const Complex *in, std::size_t n, float *out) noexcept;
    std::size_t demodSam(const Complex *in, std::size_t n, float *out) noexcept;
    std::size_t demodFm(const Complex *in, std::size_t n, float *out) noexcept;
    std::size_t demodIq(const Complex *in, std::size_t n, float *out) noexcept;

    DemodMode m_mode = DemodMode::Usb;
    double m_sampleRate = 48000.0;
    double m_fmDeviation = 5000.0;

    DcBlocker m_dcBlocker;
    Complex m_previous{0.0f, 0.0f};
    float m_fmScale = 1.0f;
    bool m_amCarrierAgc = false;
    float m_amCarrierLevel = 0.0f;

    // PLL della AM sincrona.
    double m_samPhase = 0.0;
    double m_samFrequency = 0.0;
    double m_samAlpha = 0.0;
    double m_samBeta = 0.0;
    double m_samCaptureRange = 0.05;   ///< in radianti per campione
    double m_samErrorAverage = 0.0;
    bool m_samLocked = false;
};

} // namespace dsdr::dsp
