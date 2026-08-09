// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/NoiseBlanker.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

bool NoiseBlanker::configure(double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    reset();
    return true;
}

void NoiseBlanker::setThresholdDb(double thresholdDb)
{
    m_thresholdDb = std::clamp(static_cast<float>(thresholdDb), 3.0f, 30.0f);
}

void NoiseBlanker::reset() noexcept
{
    m_averageMagnitude = 0.0f;
    m_previous = Complex(0.0f, 0.0f);
    m_blankedSamples = 0;
}

void NoiseBlanker::process(Complex *samples, std::size_t count) noexcept
{
    if (!samples)
        return;

    const float ratio = std::pow(10.0f, m_thresholdDb / 20.0f);
    for (std::size_t i = 0; i < count; ++i) {
        const float magnitude = std::sqrt(magnitudeSquared(samples[i]));
        if (m_averageMagnitude > 1e-4f
            && magnitude > m_averageMagnitude * ratio) {
            samples[i] = m_previous;
            ++m_blankedSamples;
            continue;
        }

        m_previous = samples[i];
        const float coefficient = magnitude > m_averageMagnitude ? 0.05f : 0.002f;
        m_averageMagnitude += coefficient * (magnitude - m_averageMagnitude);
    }
}

} // namespace dsdr::dsp
