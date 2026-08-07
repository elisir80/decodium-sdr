// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/FirDecimator.h"

#include <algorithm>

namespace dsdr::dsp {

void FirDecimator::configure(std::vector<float> taps, int decimation)
{
    if (taps.empty())
        taps.assign(1, 1.0f);
    m_taps = std::move(taps);
    m_decimation = std::max(1, decimation);
    m_delay.assign(m_taps.size() * 2, Complex(0.0f, 0.0f));
    m_position = m_taps.size();
    m_phase = 0;
}

void FirDecimator::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), Complex(0.0f, 0.0f));
    m_position = m_taps.size();
    m_phase = 0;
}

std::size_t FirDecimator::process(const Complex *in, std::size_t n, Complex *out) noexcept
{
    const std::size_t taps = m_taps.size();
    if (taps == 0)
        return 0;

    const float *h = m_taps.data();
    Complex *delay = m_delay.data();
    std::size_t produced = 0;

    for (std::size_t i = 0; i < n; ++i) {
        // Scrittura doppia: la finestra [m_position, m_position+taps) resta contigua.
        if (m_position == 0)
            m_position = taps;
        --m_position;
        delay[m_position] = in[i];
        delay[m_position + taps] = in[i];

        if (++m_phase < m_decimation)
            continue;
        m_phase = 0;

        const Complex *window = delay + m_position;
        float accRe = 0.0f;
        float accIm = 0.0f;
        for (std::size_t k = 0; k < taps; ++k) {
            accRe += h[k] * window[k].real();
            accIm += h[k] * window[k].imag();
        }
        out[produced++] = Complex(accRe, accIm);
    }

    return produced;
}

} // namespace dsdr::dsp
