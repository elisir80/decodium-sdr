// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/FirInterpolator.h"

#include <algorithm>

namespace dsdr::dsp {

void FirInterpolator::configure(std::vector<float> taps, int interpolation)
{
    if (taps.empty())
        taps.assign(1, 1.0f);
    m_interpolation = std::max(1, interpolation);

    const std::size_t L = static_cast<std::size_t>(m_interpolation);
    // Le fasi devono essere tutte lunghe uguale, altrimenti l'indicizzazione
    // diventa un caso particolare per fase: si arrotonda in su e le fasi corte
    // finiscono con qualche zero, che non cambia il filtro.
    m_tapsPerPhase = (taps.size() + L - 1) / L;

    m_phases.assign(m_tapsPerPhase * L, 0.0f);
    for (std::size_t i = 0; i < taps.size(); ++i) {
        const std::size_t phase = i % L;
        const std::size_t k = i / L;
        // Il guadagno L compensa gli L-1 zeri che l'interpolazione inserisce
        // fra un campione e l'altro: senza, il segnale uscirebbe attenuato di
        // 20·log10(L) dB, che a L=16 sono ventiquattro decibel di potenza
        // buttati via — e nessuno li cercherebbe qui.
        m_phases[phase * m_tapsPerPhase + k] = taps[i] * static_cast<float>(L);
    }

    m_delay.assign(m_tapsPerPhase * 2, Complex(0.0f, 0.0f));
    m_position = m_tapsPerPhase;
}

void FirInterpolator::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), Complex(0.0f, 0.0f));
    m_position = m_tapsPerPhase;
}

std::size_t FirInterpolator::process(const Complex *in, std::size_t n, Complex *out) noexcept
{
    const std::size_t taps = m_tapsPerPhase;
    if (taps == 0)
        return 0;

    const std::size_t L = static_cast<std::size_t>(m_interpolation);
    const float *phases = m_phases.data();
    Complex *delay = m_delay.data();
    std::size_t produced = 0;

    for (std::size_t i = 0; i < n; ++i) {
        // Scrittura doppia: la finestra [m_position, m_position+taps) resta contigua.
        if (m_position == 0)
            m_position = taps;
        --m_position;
        delay[m_position] = in[i];
        delay[m_position + taps] = in[i];

        const Complex *window = delay + m_position;
        for (std::size_t p = 0; p < L; ++p) {
            const float *h = phases + p * taps;
            float accRe = 0.0f;
            float accIm = 0.0f;
            for (std::size_t k = 0; k < taps; ++k) {
                accRe += h[k] * window[k].real();
                accIm += h[k] * window[k].imag();
            }
            out[produced++] = Complex(accRe, accIm);
        }
    }

    return produced;
}

} // namespace dsdr::dsp
