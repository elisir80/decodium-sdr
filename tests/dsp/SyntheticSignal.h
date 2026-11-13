// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only, repeatable baseband fixture. No hardware or real-time guarantees.
#pragma once

#include "dsp/DspTypes.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

namespace dsdr::test {

struct SyntheticSignalConfig {
    double sampleRate = 48000.0;
    double carrierHz = 1000.0;       // signed baseband frequency
    double amplitude = 0.5;
    double phaseRadians = 0.0;
    double modulationHz = 0.0;
    double amDepth = 0.0;            // envelope = 1 + depth * sin(...)
    double fmDeviationHz = 0.0;
    double noiseRms = 0.0;           // RMS per I or Q component
    std::uint32_t seed = 1;
    std::size_t impulsePeriod = 0;   // zero disables impulses
    std::size_t impulseOffset = 0;
    dsp::Complex impulse = {};
};

// Successive calls continue the same stream; reset() replays it. Noise uses a
// specified mt19937 mapping and a 12-uniform approximation, not the
// implementation-defined std::normal_distribution. Floating-point trig can
// still differ slightly between platforms. Frequencies may alias deliberately.
class SyntheticSignal {
public:
    explicit SyntheticSignal(SyntheticSignalConfig config) : m_config(config), m_rng(config.seed)
    {
        if (!std::isfinite(config.sampleRate) || config.sampleRate <= 0.0
            || !std::isfinite(config.carrierHz) || !std::isfinite(config.amplitude)
            || config.amplitude < 0.0 || !std::isfinite(config.phaseRadians)
            || !std::isfinite(config.modulationHz) || config.modulationHz < 0.0
            || !std::isfinite(config.amDepth) || config.amDepth < 0.0
            || !std::isfinite(config.fmDeviationHz) || config.fmDeviationHz < 0.0
            || !std::isfinite(config.noiseRms) || config.noiseRms < 0.0
            || !std::isfinite(config.impulse.real()) || !std::isfinite(config.impulse.imag())
            || (config.fmDeviationHz != 0.0 && config.modulationHz == 0.0))
            throw std::invalid_argument("invalid synthetic signal parameters");
    }

    void reset() { m_index = 0; m_rng.seed(m_config.seed); }

    std::vector<dsp::Complex> generate(std::size_t count)
    {
        std::vector<dsp::Complex> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i, ++m_index) {
            const double t = static_cast<double>(m_index) / m_config.sampleRate;
            const double modPhase = dsp::kTwoPi * m_config.modulationHz * t;
            const double envelope = m_config.amplitude * (1.0 + m_config.amDepth * std::sin(modPhase));
            double phase = m_config.phaseRadians + dsp::kTwoPi * m_config.carrierHz * t;
            if (m_config.fmDeviationHz != 0.0)
                phase += (m_config.fmDeviationHz / m_config.modulationHz) * (1.0 - std::cos(modPhase));
            dsp::Complex sample(static_cast<float>(envelope * std::cos(phase)),
                                static_cast<float>(envelope * std::sin(phase)));
            if (m_config.noiseRms != 0.0) {
                const float noiseI = noise();
                const float noiseQ = noise();
                sample += dsp::Complex(noiseI, noiseQ);
            }
            if (m_config.impulsePeriod != 0 && m_index >= m_config.impulseOffset
                && (m_index - m_config.impulseOffset) % m_config.impulsePeriod == 0)
                sample += m_config.impulse;
            result.push_back(sample);
        }
        return result;
    }

private:
    float noise()
    {
        double sum = 0.0;
        for (int i = 0; i < 12; ++i)
            sum += static_cast<double>(m_rng() >> 8) / 16777216.0;
        return static_cast<float>((sum - 6.0) * m_config.noiseRms);
    }
    SyntheticSignalConfig m_config;
    std::mt19937 m_rng;
    std::uint64_t m_index = 0;
};

} // namespace dsdr::test
