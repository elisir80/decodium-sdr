// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/PeakFilter.h"
#include "dsp/DspTypes.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Guadagno della campana. Dieci decibel bastano a far emergere la nota
/// senza che il filtro diventi un oscillatore: più in alto, sul rumore, si
/// sente il filtro invece del segnale.
constexpr double kPeakGainDb = 10.0;

} // namespace

void PeakFilter::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;
    m_sampleRate = sampleRate;
    m_configured = true;
    redesign();
    reset();
}

void PeakFilter::reset() noexcept
{
    m_z1 = 0.0f;
    m_z2 = 0.0f;
}

void PeakFilter::setPeak(double frequencyHz, double q) noexcept
{
    const double top = m_configured ? m_sampleRate * 0.45 : 20000.0;
    m_frequency = std::clamp(frequencyHz, 100.0, top);
    m_q = std::clamp(q, 1.0, 50.0);
    redesign();
}

void PeakFilter::redesign() noexcept
{
    if (!m_configured)
        return;

    // Peaking EQ (Robert Bristow-Johnson): la stessa forma del notch, con il
    // guadagno che moltiplica invece di dividere.
    const double a = std::pow(10.0, kPeakGainDb / 40.0);
    const double w0 = kTwoPi * m_frequency / m_sampleRate;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * m_q);

    const double a0 = 1.0 + alpha / a;
    m_b0 = static_cast<float>((1.0 + alpha * a) / a0);
    m_b1 = static_cast<float>(-2.0 * cosW0 / a0);
    m_b2 = static_cast<float>((1.0 - alpha * a) / a0);
    m_a1 = static_cast<float>(-2.0 * cosW0 / a0);
    m_a2 = static_cast<float>((1.0 - alpha / a) / a0);
}

void PeakFilter::process(float *audio, std::size_t n) noexcept
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
