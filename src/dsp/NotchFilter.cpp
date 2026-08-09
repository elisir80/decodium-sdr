// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/NotchFilter.h"
#include "dsp/DspTypes.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

void NotchFilter::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;
    m_sampleRate = sampleRate;
    m_configured = true;
    redesign();
    reset();
}

void NotchFilter::reset() noexcept
{
    m_z1 = 0.0f;
    m_z2 = 0.0f;
}

void NotchFilter::setNotch(double frequencyHz, double widthHz) noexcept
{
    // La riga deve stare dentro la banda audio con un margine: portata a
    // ridosso di Nyquist la biquad degenera e il filtro si mette a suonare.
    const double top = m_configured ? m_sampleRate * 0.45 : 20000.0;
    m_frequency = std::clamp(frequencyHz, 50.0, top);
    m_width = std::clamp(widthHz, 10.0, 1000.0);
    redesign();
}

void NotchFilter::redesign() noexcept
{
    if (!m_configured)
        return;

    const double w0 = kTwoPi * m_frequency / m_sampleRate;
    const double cosW0 = std::cos(w0);
    const double sinW0 = std::sin(w0);

    // Q dalla larghezza richiesta: è il modo in cui l'operatore ragiona
    // («togli duecento hertz attorno al fischio»), non in fattori di merito.
    const double q = std::max(0.5, m_frequency / m_width);
    const double alpha = sinW0 / (2.0 * q);

    const double a0 = 1.0 + alpha;
    m_b0 = static_cast<float>(1.0 / a0);
    m_b1 = static_cast<float>(-2.0 * cosW0 / a0);
    m_b2 = static_cast<float>(1.0 / a0);
    m_a1 = static_cast<float>(-2.0 * cosW0 / a0);
    m_a2 = static_cast<float>((1.0 - alpha) / a0);
}

void NotchFilter::process(float *audio, std::size_t n) noexcept
{
    if (!m_configured || audio == nullptr || n == 0)
        return;

    for (std::size_t i = 0; i < n; ++i) {
        const float x = audio[i];
        const float y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        audio[i] = y;
    }
}

} // namespace dsdr::dsp
