// SPDX-License-Identifier: GPL-3.0-or-later
// Soppressore di impulsi sul canale complesso prima della demodulazione.
#pragma once

#include "dsp/DspTypes.h"

#include <cstddef>
#include <cstdint>

namespace dsdr::dsp {

class NoiseBlanker
{
public:
    bool configure(double sampleRate);
    void setThresholdDb(double thresholdDb);
    void reset() noexcept;

    /// Sostituisce un impulso di ampiezza anomala con l'ultimo campione valido.
    void process(Complex *samples, std::size_t count) noexcept;

    float thresholdDb() const noexcept { return m_thresholdDb; }
    std::uint64_t blankedSamples() const noexcept { return m_blankedSamples; }

private:
    double m_sampleRate = 48000.0;
    float m_thresholdDb = 12.0f;
    float m_averageMagnitude = 0.0f;
    Complex m_previous{0.0f, 0.0f};
    std::uint64_t m_blankedSamples = 0;
};

} // namespace dsdr::dsp
