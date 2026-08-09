// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/BroadcastFmStereo.h"
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

void BroadcastFmStereo::RealFir::setTaps(const std::vector<float> &taps)
{
    m_taps = taps;
    if (m_taps.empty())
        m_taps.assign(1, 1.0f);
    m_delay.assign(m_taps.size(), 0.0f);
    m_position = 0;
}

void BroadcastFmStereo::RealFir::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
    m_position = 0;
}

float BroadcastFmStereo::RealFir::process(float sample) noexcept
{
    if (m_taps.empty())
        return sample;

    m_delay[m_position] = sample;
    float result = 0.0f;
    for (std::size_t k = 0; k < m_taps.size(); ++k) {
        const std::size_t index = (m_position + m_taps.size() - k) % m_taps.size();
        result += m_taps[k] * m_delay[index];
    }
    m_position = (m_position + 1) % m_taps.size();
    return result;
}

bool BroadcastFmStereo::configure(double sampleRate)
{
    if (sampleRate < 100000.0)
        return false;

    m_sampleRate = sampleRate;

    // Differenza di due passa-basso: banda reale 18.75–19.25 kHz. Il filtro
    // non deve essere stretto come il filtro audio: serve soprattutto a
    // impedire che l'audio e RDS disturbino il PLL del pilota.
    constexpr int kPilotTaps = 127;
    const std::vector<float> pilotHigh = designLowpass(19250.0, sampleRate,
                                                        kPilotTaps, kaiserBeta(60.0));
    const std::vector<float> pilotLow = designLowpass(18750.0, sampleRate,
                                                       kPilotTaps, kaiserBeta(60.0));
    m_pilotTaps.resize(pilotHigh.size());
    for (std::size_t i = 0; i < m_pilotTaps.size(); ++i)
        m_pilotTaps[i] = pilotHigh[i] - pilotLow[i];

    constexpr int kAudioTaps = 127;
    m_audioTaps = designLowpass(15000.0, sampleRate, kAudioTaps, kaiserBeta(70.0));
    m_pilotFilter.setTaps(m_pilotTaps);
    m_sumFilter.setTaps(m_audioTaps);
    m_differenceFilter.setTaps(m_audioTaps);

    m_groupDelay = m_pilotTaps.size() > 1 ? (m_pilotTaps.size() - 1) / 2 : 0;
    m_mpxDelay.assign(m_groupDelay + 1, 0.0f);
    m_delayPosition = 0;

    const double pilotHz = 19000.0;
    m_pilotOmega = kTwoPi * pilotHz / sampleRate;
    m_pilotFrequency = m_pilotOmega;

    // Second-order PLL. The loop is wide enough to lock quickly on a normal
    // broadcast pilot, while the frequency clamp rejects audio/RDS leakage.
    const double normalizedBandwidth = kTwoPi * 250.0 / sampleRate;
    m_pilotAlpha = 2.0 * 0.707 * normalizedBandwidth;
    m_pilotBeta = normalizedBandwidth * normalizedBandwidth;
    reset();
    return true;
}

void BroadcastFmStereo::reset() noexcept
{
    m_pilotFrequency = m_pilotOmega;
    m_pilotPhase = 0.0;
    m_pilotFilter.reset();
    m_sumFilter.reset();
    m_differenceFilter.reset();
    std::fill(m_mpxDelay.begin(), m_mpxDelay.end(), 0.0f);
    m_delayPosition = 0;
}

void BroadcastFmStereo::setLowPass(bool enabled) noexcept
{
    if (m_lowPass == enabled)
        return;
    m_lowPass = enabled;
    m_sumFilter.reset();
    m_differenceFilter.reset();
}

void BroadcastFmStereo::process(const float *mpx, std::size_t n, float *out) noexcept
{
    if (!mpx || !out || n == 0)
        return;

    for (std::size_t i = 0; i < n; ++i) {
        const float pilot = m_pilotFilter.process(mpx[i]);

        // Il filtro del pilota introduce il ritardo di gruppo. Confrontiamo il
        // pilota con il VCO riportato allo stesso istante temporale.
        const double delayedPhase = m_pilotPhase
            - static_cast<double>(m_groupDelay) * m_pilotFrequency;
        const double error = std::clamp(
            static_cast<double>(pilot) * std::sin(delayedPhase) * 20.0,
            -1.0, 1.0);
        m_pilotFrequency += m_pilotBeta * error;
        const double minOmega = kTwoPi * 18750.0 / m_sampleRate;
        const double maxOmega = kTwoPi * 19250.0 / m_sampleRate;
        m_pilotFrequency = std::clamp(m_pilotFrequency, minOmega, maxOmega);
        m_pilotPhase += m_pilotFrequency + m_pilotAlpha * error;
        if (m_pilotPhase > kPi)
            m_pilotPhase -= kTwoPi;
        else if (m_pilotPhase < -kPi)
            m_pilotPhase += kTwoPi;

        // Allinea il multiplex al ramo pilota. Il ramo L-R viene traslato con
        // 2× il VCO (38 kHz), poi filtrato nella stessa banda audio di L+R.
        const float delayed = m_mpxDelay[m_delayPosition];
        m_mpxDelay[m_delayPosition] = mpx[i];
        m_delayPosition = (m_delayPosition + 1) % m_mpxDelay.size();

        const double mixPhase = 2.0 * delayedPhase;
        const float sum = m_lowPass ? m_sumFilter.process(delayed) : delayed;
        const float differenceInput = delayed * static_cast<float>(2.0 * std::cos(mixPhase));
        const float difference = m_lowPass ? m_differenceFilter.process(differenceInput)
                                           : differenceInput;

        out[i * 2] = std::clamp(sum + difference, -1.0f, 1.0f);
        out[i * 2 + 1] = std::clamp(sum - difference, -1.0f, 1.0f);
    }
}

} // namespace dsdr::dsp
