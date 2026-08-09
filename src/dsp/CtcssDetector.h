// SPDX-License-Identifier: GPL-3.0-or-later
// Rilevatore CTCSS a finestra continua per il canale NFM.
#pragma once

#include <cstddef>

namespace dsdr::dsp {

class CtcssDetector
{
public:
    bool configure(double sampleRate, double toneHz);
    void setTone(double toneHz);
    void reset() noexcept;
    void process(const float *samples, std::size_t count) noexcept;

    bool detected() const noexcept { return m_detected; }
    float levelDb() const noexcept { return m_levelDb; }
    double toneHz() const noexcept { return m_toneHz; }

private:
    void updateWindow() noexcept;

    double m_sampleRate = 48000.0;
    double m_toneHz = 100.0;
    float m_coefficient = 0.0f;
    float m_s1 = 0.0f;
    float m_s2 = 0.0f;
    float m_totalPower = 0.0f;
    std::size_t m_samples = 0;
    bool m_detected = false;
    float m_levelDb = -160.0f;
};

} // namespace dsdr::dsp
