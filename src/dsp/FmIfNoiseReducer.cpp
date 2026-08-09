// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/FmIfNoiseReducer.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

bool FmIfNoiseReducer::configure(double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    updateCoefficient();
    reset();
    return true;
}

void FmIfNoiseReducer::setPreset(int preset)
{
    m_preset = std::clamp(preset, 0, 2);
    updateCoefficient();
}

void FmIfNoiseReducer::updateCoefficient() noexcept
{
    // Tagli conservativi: Voice e Narrow riducono il rumore fuori dalla
    // banda vocale senza comprimere la deviazione; Broadcast resta largo
    // abbastanza da non intaccare il pilot stereo o l'RDS a 57 kHz.
    const double cutoffHz[] = {8'000.0, 18'000.0, 95'000.0};
    const double cutoff = std::min(cutoffHz[m_preset], m_sampleRate * 0.45);
    m_alpha = static_cast<float>(1.0 - std::exp(-kTwoPi * cutoff / m_sampleRate));
}

void FmIfNoiseReducer::reset() noexcept
{
    m_state = Complex(0.0f, 0.0f);
    m_initialized = false;
}

void FmIfNoiseReducer::process(Complex *samples, std::size_t count) noexcept
{
    if (!samples || count == 0)
        return;

    for (std::size_t i = 0; i < count; ++i) {
        if (!m_initialized) {
            m_state = samples[i];
            m_initialized = true;
        } else {
            m_state += m_alpha * (samples[i] - m_state);
        }
        samples[i] = m_state;
    }
}

} // namespace dsdr::dsp
