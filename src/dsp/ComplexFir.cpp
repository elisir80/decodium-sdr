// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/ComplexFir.h"

#include <algorithm>

namespace dsdr::dsp {

ComplexFir::ComplexFir()
{
    m_taps.reserve(kMaxFirTaps);
    m_delay.reserve(kMaxFirTaps * 2);
}

void ComplexFir::setTaps(const std::vector<Complex> &taps)
{
    const bool sameLength = (taps.size() == m_taps.size());
    m_taps = taps;
    if (m_taps.empty())
        m_taps.assign(1, Complex(1.0f, 0.0f));

    if (!sameLength) {
        m_delay.assign(m_taps.size() * 2, Complex(0.0f, 0.0f));
        m_position = m_taps.size();
    }
}

void ComplexFir::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), Complex(0.0f, 0.0f));
    m_position = m_taps.size();
}

void ComplexFir::process(const Complex *in, Complex *out, std::size_t n) noexcept
{
    const std::size_t taps = m_taps.size();
    if (taps == 0)
        return;

    const Complex *h = m_taps.data();
    Complex *delay = m_delay.data();

    for (std::size_t i = 0; i < n; ++i) {
        if (m_position == 0)
            m_position = taps;
        --m_position;
        delay[m_position] = in[i];
        delay[m_position + taps] = in[i];

        const Complex *window = delay + m_position;
        float accRe = 0.0f;
        float accIm = 0.0f;
        for (std::size_t k = 0; k < taps; ++k) {
            const float xr = window[k].real();
            const float xi = window[k].imag();
            const float hr = h[k].real();
            const float hi = h[k].imag();
            accRe += xr * hr - xi * hi;
            accIm += xr * hi + xi * hr;
        }
        out[i] = Complex(accRe, accIm);
    }
}

} // namespace dsdr::dsp
