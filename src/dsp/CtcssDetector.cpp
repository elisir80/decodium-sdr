// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/CtcssDetector.h"

#include "dsp/DspTypes.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

bool CtcssDetector::configure(double sampleRate, double toneHz)
{
    if (sampleRate <= 0.0 || toneHz <= 0.0 || toneHz >= sampleRate * 0.5)
        return false;
    m_sampleRate = sampleRate;
    setTone(toneHz);
    return true;
}

void CtcssDetector::setTone(double toneHz)
{
    m_toneHz = std::clamp(toneHz, 50.0, 300.0);
    const double omega = kTwoPi * m_toneHz / m_sampleRate;
    m_coefficient = static_cast<float>(2.0 * std::cos(omega));
    reset();
}

void CtcssDetector::reset() noexcept
{
    m_s1 = 0.0f;
    m_s2 = 0.0f;
    m_totalPower = 0.0f;
    m_samples = 0;
    m_detected = false;
    m_levelDb = -160.0f;
}

void CtcssDetector::updateWindow() noexcept
{
    constexpr float kMinimumToneDb = -90.0f;
    // Keep adjacent standard tones separated even when the NFM voice/audio
    // component is strong.  The detector still accepts a CTCSS tone about
    // 30 dB below the total demodulated power.
    constexpr float kDetectionRatioDb = -30.0f;

    if (m_samples == 0)
        return;

    const float tonePower = std::max(
        0.0f, m_s1 * m_s1 + m_s2 * m_s2 - m_coefficient * m_s1 * m_s2);
    const float normalizer = static_cast<float>(m_samples) * m_samples;
    const float normalizedTone = 2.0f * tonePower / std::max(normalizer, 1.0f);
    const float normalizedTotal = m_totalPower / static_cast<float>(m_samples);
    const float toneDb = powerToDb(normalizedTone);
    const float ratioDb = powerToDb(normalizedTone
                                    / std::max(normalizedTotal, 1e-12f));

    m_levelDb = ratioDb;
    m_detected = toneDb > kMinimumToneDb && ratioDb > kDetectionRatioDb;

    m_s1 = 0.0f;
    m_s2 = 0.0f;
    m_totalPower = 0.0f;
    m_samples = 0;
}

void CtcssDetector::process(const float *samples, std::size_t count) noexcept
{
    if (!samples)
        return;

    // A 2 ksample window has only 23.4 Hz bin spacing at 48 kHz and can
    // confuse adjacent standard CTCSS tones.  This window gives about 5.9 Hz
    // resolution while keeping the mute/decode decision below 200 ms.
    constexpr std::size_t kWindowSamples = 8192;
    for (std::size_t i = 0; i < count; ++i) {
        const float sample = samples[i];
        const float s0 = sample + m_coefficient * m_s1 - m_s2;
        m_s2 = m_s1;
        m_s1 = s0;
        m_totalPower += sample * sample;
        ++m_samples;
        if (m_samples >= kWindowSamples)
            updateWindow();
    }
}

} // namespace dsdr::dsp
