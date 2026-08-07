// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/ChannelProcessor.h"
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

bool ChannelSettings::operator==(const ChannelSettings &o) const noexcept
{
    return offsetHz == o.offsetHz && mode == o.mode && filterLowHz == o.filterLowHz
        && filterHighHz == o.filterHighHz && agcMode == o.agcMode
        && agcThresholdDb == o.agcThresholdDb && agcMaxGainDb == o.agcMaxGainDb
        && cwPitchHz == o.cwPitchHz && volume == o.volume && muted == o.muted;
}

ChannelProcessor::ChannelProcessor()
{
    m_filterTaps.reserve(kMaxFirTaps);
}

bool ChannelProcessor::configure(double deviceSampleRate, double audioSampleRate)
{
    if (deviceSampleRate <= 0.0 || audioSampleRate <= 0.0)
        return false;

    m_deviceRate = deviceSampleRate;
    m_audioRate = audioSampleRate;

    int decimation = static_cast<int>(std::lround(deviceSampleRate / audioSampleRate));
    decimation = std::max(1, decimation);

    // Banda utile conservata dalla decimazione: copre la FM larga, che è il
    // canale più largo che il demodulatore possa chiedere.
    if (!m_chain.configure(deviceSampleRate, decimation, 8000.0))
        return false;

    m_channelRate = m_chain.outputRate();

    m_nco.configure(deviceSampleRate, 0.0);
    m_demod.configure(m_channelRate);
    m_agc.configure(m_channelRate);

    const std::size_t block = kMaxBlockFrames;
    m_mixed.assign(block, Complex(0.0f, 0.0f));
    m_decimated.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));
    m_filtered.assign(m_chain.maxOutput(block) + 8, Complex(0.0f, 0.0f));

    m_configured = true;
    redesignFilter();
    reset();
    return true;
}

double ChannelProcessor::tuningOffsetHz() const
{
    // Per la CW la portante non va portata a DC ma al tono di battimento
    // scelto dall'operatore: è il BFO, realizzato spostando il DDC.
    switch (m_settings.mode) {
    case DemodMode::Cw:
        return m_settings.offsetHz - m_settings.cwPitchHz;
    case DemodMode::Cwr:
        return m_settings.offsetHz + m_settings.cwPitchHz;
    default:
        return m_settings.offsetHz;
    }
}

void ChannelProcessor::computeFilterEdges(double &loHz, double &hiHz) const
{
    const double nyquist = m_channelRate * 0.5;
    const double lo = static_cast<double>(m_settings.filterLowHz);
    const double hi = static_cast<double>(m_settings.filterHighHz);

    switch (m_settings.mode) {
    case DemodMode::Usb:
    case DemodMode::DigU:
        loHz = lo;
        hiHz = hi;
        break;
    case DemodMode::Lsb:
    case DemodMode::DigL:
        loHz = -hi;
        hiHz = -lo;
        break;
    case DemodMode::Cw: {
        const double half = std::max(50.0, (hi - lo) * 0.5);
        loHz = m_settings.cwPitchHz - half;
        hiHz = m_settings.cwPitchHz + half;
        break;
    }
    case DemodMode::Cwr: {
        const double half = std::max(50.0, (hi - lo) * 0.5);
        loHz = -m_settings.cwPitchHz - half;
        hiHz = -m_settings.cwPitchHz + half;
        break;
    }
    case DemodMode::Am:
    case DemodMode::Sam:
    case DemodMode::Fm:
    case DemodMode::Nfm:
        // Modalità simmetriche: la portante sta a DC, serve tutta la banda.
        loHz = -std::abs(hi);
        hiHz = std::abs(hi);
        break;
    case DemodMode::Iq:
        loHz = -nyquist * 0.9;
        hiHz = nyquist * 0.9;
        break;
    }

    loHz = std::clamp(loHz, -nyquist * 0.95, nyquist * 0.95);
    hiHz = std::clamp(hiHz, -nyquist * 0.95, nyquist * 0.95);
    if (hiHz - loHz < 50.0)
        hiHz = loHz + 50.0;
}

void ChannelProcessor::redesignFilter()
{
    if (!m_configured)
        return;

    double lo = 0.0;
    double hi = 0.0;
    computeFilterEdges(lo, hi);

    const double width = hi - lo;
    const double transition = std::clamp(width * 0.15, 60.0, 800.0);
    int taps = estimateTaps(transition, m_channelRate, 70.0);
    taps = std::min<int>(taps, 511);

    designBandpassInto(m_filterTaps, lo, hi, m_channelRate, taps, kaiserBeta(70.0));
    m_filter.setTaps(m_filterTaps);
}

void ChannelProcessor::applySettings(const ChannelSettings &settings)
{
    const bool filterChanged = settings.mode != m_settings.mode
        || settings.filterLowHz != m_settings.filterLowHz
        || settings.filterHighHz != m_settings.filterHighHz
        || settings.cwPitchHz != m_settings.cwPitchHz;
    const bool tuningChanged = settings.offsetHz != m_settings.offsetHz
        || settings.cwPitchHz != m_settings.cwPitchHz || settings.mode != m_settings.mode;

    m_settings = settings;

    if (tuningChanged)
        m_nco.setFrequency(tuningOffsetHz());
    if (filterChanged) {
        m_demod.setMode(m_settings.mode);
        m_demod.setFmDeviation(m_settings.mode == DemodMode::Nfm ? 2500.0 : 5000.0);
        redesignFilter();
    }

    m_agc.setMode(m_settings.agcMode);
    m_agc.setThresholdDb(m_settings.agcThresholdDb);
    m_agc.setMaxGainDb(m_settings.agcMaxGainDb);
}

void ChannelProcessor::reset() noexcept
{
    m_nco.reset();
    m_chain.reset();
    m_filter.reset();
    m_demod.reset();
    m_agc.reset();
    m_signalLevelDb = -160.0f;
    m_lastBasebandFrames = 0;
}

std::size_t ChannelProcessor::process(const Complex *iq, std::size_t n, float *out) noexcept
{
    if (!m_configured || n == 0)
        return 0;

    std::size_t produced = 0;
    std::size_t offset = 0;

    while (offset < n) {
        const std::size_t chunk = std::min(kMaxBlockFrames, n - offset);

        m_nco.mixDown(iq + offset, m_mixed.data(), chunk);
        const std::size_t decimated = m_chain.process(m_mixed.data(), chunk, m_decimated.data());
        if (decimated == 0) {
            offset += chunk;
            continue;
        }

        m_filter.process(m_decimated.data(), m_filtered.data(), decimated);
        m_lastBasebandFrames = decimated;

        // S-meter: potenza media del canale filtrato, prima dell'AGC.
        float power = 0.0f;
        for (std::size_t i = 0; i < decimated; ++i)
            power += magnitudeSquared(m_filtered[i]);
        power /= static_cast<float>(decimated);
        const float instantDb = powerToDb(power);
        m_signalLevelDb += (instantDb - m_signalLevelDb) * 0.2f;

        float *audio = out + produced;
        m_demod.process(m_filtered.data(), decimated, audio);
        m_agc.process(audio, decimated);

        const float gain = m_settings.muted ? 0.0f : m_settings.volume;
        for (std::size_t i = 0; i < decimated; ++i)
            audio[i] = std::clamp(audio[i] * gain, -1.0f, 1.0f);

        produced += decimated;
        offset += chunk;
    }

    return produced;
}

} // namespace dsdr::dsp
