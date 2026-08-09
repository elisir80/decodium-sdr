// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AudioHighPass.h"

#include "dsp/DspTypes.h"
#include "dsp/FirDesign.h"

#include <algorithm>

namespace dsdr::dsp {

bool AudioHighPass::configure(double sampleRate, double cutoffHz,
                              double transitionHz)
{
    if (sampleRate <= 0.0 || transitionHz <= 0.0)
        return false;

    cutoffHz = std::clamp(cutoffHz, 1.0, sampleRate * 0.45);
    const int taps = std::min(511, estimateTaps(transitionHz, sampleRate, 70.0));
    const std::vector<float> lowpass = designLowpass(
        cutoffHz, sampleRate, taps, kaiserBeta(70.0));

    m_taps.resize(lowpass.size());
    const std::size_t centre = lowpass.size() / 2;
    for (std::size_t i = 0; i < lowpass.size(); ++i)
        m_taps[i] = -lowpass[i];
    // Spectral inversion: delta[n] - LP[n]. Il guadagno DC è nullo e la
    // banda sopra il cutoff resta a guadagno unitario.
    m_taps[centre] += 1.0f;

    m_delay.assign(m_taps.size() * 2, 0.0f);
    m_position = m_taps.size();
    m_cutoffHz = cutoffHz;
    return true;
}

void AudioHighPass::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
    m_position = m_taps.size();
}

float AudioHighPass::process(float sample) noexcept
{
    const std::size_t taps = m_taps.size();
    if (taps == 0)
        return sample;

    if (m_position == 0)
        m_position = taps;
    --m_position;
    m_delay[m_position] = sample;
    m_delay[m_position + taps] = sample;

    float output = 0.0f;
    for (std::size_t i = 0; i < taps; ++i)
        output += m_taps[i] * m_delay[m_position + i];
    return output;
}

} // namespace dsdr::dsp
