// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — oscillatore numerico e mixer DDC.
//
// Il fasore avanza per moltiplicazione complessa (nessuna sin/cos nel percorso
// caldo) e viene rinormalizzato periodicamente per contenere la deriva del
// modulo dovuta all'accumulo in singola precisione.
#pragma once

#include "dsp/DspTypes.h"

#include <cmath>

namespace dsdr::dsp {

class Nco
{
public:
    void configure(double sampleRate, double frequencyHz = 0.0)
    {
        m_sampleRate = sampleRate > 0.0 ? sampleRate : 1.0;
        setFrequency(frequencyHz);
        reset();
    }

    void setFrequency(double frequencyHz)
    {
        m_frequency = frequencyHz;
        const double w = kTwoPi * frequencyHz / m_sampleRate;
        m_step = Complex(static_cast<float>(std::cos(w)), static_cast<float>(std::sin(w)));
    }

    double frequency() const noexcept { return m_frequency; }

    void reset() noexcept
    {
        m_phasor = Complex(1.0f, 0.0f);
        m_sinceNormalize = 0;
    }

    /// Traslazione in frequenza: out[i] = in[i] · e^{-j2πft}. Con `f` pari
    /// all'offset del canale porta il canale a banda base (DDC).
    void mixDown(const Complex *in, Complex *out, std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = in[i] * std::conj(m_phasor);
            advance();
        }
    }

    /// Traslazione opposta, usata per riportare in banda un canale (TX, BFO).
    void mixUp(const Complex *in, Complex *out, std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = in[i] * m_phasor;
            advance();
        }
    }

    Complex next() noexcept
    {
        const Complex v = m_phasor;
        advance();
        return v;
    }

private:
    void advance() noexcept
    {
        m_phasor *= m_step;
        if (++m_sinceNormalize >= 2048) {
            m_sinceNormalize = 0;
            const float mag = std::sqrt(magnitudeSquared(m_phasor));
            if (mag > 1e-6f)
                m_phasor /= mag;
            else
                m_phasor = Complex(1.0f, 0.0f);
        }
    }

    Complex m_phasor{1.0f, 0.0f};
    Complex m_step{1.0f, 0.0f};
    double m_sampleRate = 1.0;
    double m_frequency = 0.0;
    unsigned m_sinceNormalize = 0;
};

} // namespace dsdr::dsp
