// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/Agc.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {

struct ModeTiming
{
    double decaySeconds;
    double hangSeconds;
};

ModeTiming timingFor(AgcMode mode)
{
    switch (mode) {
    case AgcMode::Fast:   return {0.20, 0.10};
    case AgcMode::Medium: return {0.50, 0.25};
    case AgcMode::Slow:   return {1.20, 0.50};
    case AgcMode::Long:   return {2.50, 1.00};
    case AgcMode::Off:    break;
    }
    return {0.50, 0.25};
}

float onePoleCoeff(double timeConstantSeconds, double sampleRate)
{
    if (timeConstantSeconds <= 0.0)
        return 1.0f;
    return static_cast<float>(1.0 - std::exp(-1.0 / (timeConstantSeconds * sampleRate)));
}

} // namespace

void Agc::configure(double sampleRate)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    recomputeCoefficients();
    reset();
}

void Agc::setMode(AgcMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    recomputeCoefficients();
}

void Agc::setThresholdDb(double db)
{
    m_thresholdDb = std::clamp(db, -140.0, 0.0);
}

void Agc::setAttackMs(double ms)
{
    m_attackMs = std::clamp(ms, 0.1, 5000.0);
    recomputeCoefficients();
}

void Agc::setDecayMs(double ms)
{
    m_decayMs = std::clamp(ms, 0.0, 10000.0);
    recomputeCoefficients();
}

void Agc::setMaxGainDb(double db)
{
    m_maxGainDb = std::clamp(db, 0.0, 120.0);
}

void Agc::setManualGainDb(double db)
{
    m_manualGainDb = std::clamp(db, -40.0, 60.0);
}

void Agc::recomputeCoefficients()
{
    const ModeTiming t = timingFor(m_mode);
    const double decaySeconds = m_decayMs > 0.0 ? m_decayMs / 1000.0 : t.decaySeconds;
    m_attackCoeff = onePoleCoeff(m_attackMs / 1000.0, m_sampleRate);
    m_decayCoeff = onePoleCoeff(decaySeconds, m_sampleRate);
    m_gainSmoothCoeff = onePoleCoeff(0.005, m_sampleRate);
    m_hangSamples = static_cast<unsigned>(t.hangSeconds * m_sampleRate);
}

void Agc::reset() noexcept
{
    m_envelope = 0.0f;
    m_gain = 1.0f;
    m_hangCounter = 0;
}

void Agc::process(float *audio, std::size_t n) noexcept
{
    if (m_mode == AgcMode::Off) {
        const float g = static_cast<float>(std::pow(10.0, m_manualGainDb / 20.0));
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            audio[i] *= g;
            peak = std::max(peak, std::abs(audio[i]));
        }
        m_gain = g;
        m_envelope = peak;
        return;
    }

    const float threshold = static_cast<float>(std::pow(10.0, m_thresholdDb / 20.0));
    const float maxGain = static_cast<float>(std::pow(10.0, m_maxGainDb / 20.0));

    for (std::size_t i = 0; i < n; ++i) {
        const float x = std::abs(audio[i]);

        if (x > m_envelope) {
            // Attacco: insegue immediatamente il picco e riarma l'hang.
            m_envelope += (x - m_envelope) * m_attackCoeff;
            m_hangCounter = m_hangSamples;
        } else if (m_hangCounter > 0) {
            // Hang: il guadagno resta fermo fra una sillaba e l'altra, così
            // il rumore non "risale" nelle pause del parlato.
            --m_hangCounter;
        } else {
            m_envelope += (x - m_envelope) * m_decayCoeff;
        }

        // AGC-T: sotto soglia il guadagno non insegue più il segnale, quindi
        // il rumore di fondo resta al livello che l'operatore ha scelto.
        const float effective = std::max(m_envelope, threshold);
        float target = (effective > 1e-9f) ? (kTargetLevel / effective) : maxGain;
        target = std::clamp(target, 0.0f, maxGain);

        m_gain += (target - m_gain) * m_gainSmoothCoeff;
        audio[i] *= m_gain;
    }
}

float Agc::gainDb() const noexcept
{
    return 20.0f * std::log10(std::max(m_gain, 1e-9f));
}

float Agc::envelopeDb() const noexcept
{
    return 20.0f * std::log10(std::max(m_envelope, 1e-9f));
}

} // namespace dsdr::dsp
