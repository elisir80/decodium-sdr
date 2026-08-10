// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/CwKeyer.h"
#include "dsp/DspTypes.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

bool CwKeyer::configure(double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    setRiseMs(m_riseMs);
    reset();
    return true;
}

void CwKeyer::setRiseMs(double ms)
{
    m_riseMs = std::clamp(ms, 1.0, 20.0);
    m_edgeSamples = static_cast<std::size_t>(
        std::max(1.0, std::round(m_riseMs * 0.001 * m_sampleRate)));
    m_position = std::min(m_position, m_edgeSamples);
}

void CwKeyer::reset() noexcept
{
    m_position = 0;
    m_keyDown = false;
}

void CwKeyer::process(float *envelope, std::size_t n) noexcept
{
    for (std::size_t i = 0; i < n; ++i) {
        if (m_keyDown && m_position < m_edgeSamples)
            ++m_position;
        else if (!m_keyDown && m_position > 0)
            --m_position;

        // Coseno rialzato: 0 a tasto alzato, 1 a tasto premuto, e in mezzo
        // mezzo periodo di coseno. Contare la posizione lungo il fronte invece
        // di tenere un flag «sto salendo» fa sì che un'inversione a metà
        // salita torni indietro dallo stesso punto, senza discontinuità.
        const double phase = static_cast<double>(m_position)
                           / static_cast<double>(m_edgeSamples);
        envelope[i] = static_cast<float>(0.5 - 0.5 * std::cos(kPi * phase));
    }
}

} // namespace dsdr::dsp
