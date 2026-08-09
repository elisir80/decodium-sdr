// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — decoder MPX stereo per FM broadcast.
//
// Il discriminatore FM produce il multiplex: L+R (0–15 kHz), pilota a
// 19 kHz e L-R modulato in DSB-SC a 38 kHz. Questa classe separa i tre rami
// senza dipendenze esterne e restituisce campioni interleaved L/R.
#pragma once

#include "dsp/DspTypes.h"

#include <cstddef>
#include <vector>

namespace dsdr::dsp {

class BroadcastFmStereo
{
public:
    bool configure(double sampleRate);
    void reset() noexcept;
    void setLowPass(bool enabled) noexcept;
    bool lowPass() const noexcept { return m_lowPass; }

    /// `out` contiene 2 * `n` campioni: sinistro, destro, sinistro, destro.
    void process(const float *mpx, std::size_t n, float *out) noexcept;

    double sampleRate() const noexcept { return m_sampleRate; }

private:
    class RealFir
    {
    public:
        void setTaps(const std::vector<float> &taps);
        void reset() noexcept;
        float process(float sample) noexcept;

    private:
        std::vector<float> m_taps;
        std::vector<float> m_delay;
        std::size_t m_position = 0;
    };

    double m_sampleRate = 0.0;
    double m_pilotOmega = 0.0;
    double m_pilotFrequency = 0.0;
    double m_pilotPhase = 0.0;
    double m_pilotAlpha = 0.0;
    double m_pilotBeta = 0.0;
    std::size_t m_groupDelay = 0;
    bool m_lowPass = true;

    RealFir m_pilotFilter;
    RealFir m_sumFilter;
    RealFir m_differenceFilter;
    std::vector<float> m_pilotTaps;
    std::vector<float> m_audioTaps;
    std::vector<float> m_mpxDelay;
    std::size_t m_delayPosition = 0;
};

} // namespace dsdr::dsp
