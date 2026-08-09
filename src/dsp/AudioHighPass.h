// SPDX-License-Identifier: GPL-3.0-or-later
// Filtro passa-alto audio post-demodulazione.
#pragma once

#include <cstddef>
#include <vector>

namespace dsdr::dsp {

/// FIR lineare progettato per la catena AF. Il filtro resta allocation-free
/// durante process(): i coefficienti e il delay vengono preparati in
/// configure(), come per gli altri stadi DSP.
class AudioHighPass
{
public:
    bool configure(double sampleRate, double cutoffHz = 300.0,
                   double transitionHz = 100.0);
    void reset() noexcept;
    float process(float sample) noexcept;

    double cutoffHz() const noexcept { return m_cutoffHz; }

private:
    std::vector<float> m_taps;
    std::vector<float> m_delay;
    std::size_t m_position = 0;
    double m_cutoffHz = 300.0;
};

} // namespace dsdr::dsp
