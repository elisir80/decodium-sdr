// SPDX-License-Identifier: GPL-3.0-or-later
// Riduzione del rumore IF per FM/NFM, distinta dal noise blanker impulsivo.
#pragma once

#include "dsp/DspTypes.h"

#include <cstddef>

namespace dsdr::dsp {

class FmIfNoiseReducer
{
public:
    bool configure(double sampleRate);
    void setPreset(int preset);
    void reset() noexcept;

    /// Applica un passa-basso adattivo al segnale complesso IF.
    void process(Complex *samples, std::size_t count) noexcept;

    int preset() const noexcept { return m_preset; }

private:
    void updateCoefficient() noexcept;

    double m_sampleRate = 48000.0;
    int m_preset = 0;
    float m_alpha = 1.0f;
    Complex m_state{0.0f, 0.0f};
    bool m_initialized = false;
};

} // namespace dsdr::dsp
