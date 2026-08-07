// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(dsdrDsp, "dsdr.dsp")

namespace dsdr::core {

using dsp::Complex;

namespace {

/// ~1.3 s di audio a 48 kHz: assorbe una pausa lunga della scheda audio senza
/// far crescere la latenza percepita, che resta governata dal buffer del sink.
constexpr std::size_t kAudioRingFloats = 1 << 16;

/// Quanti campioni IQ si elaborano per giro. Coincide con il blocco massimo
/// dei ChannelProcessor: nessuna suddivisione ulteriore, nessuna allocazione.
constexpr std::size_t kProcessBlock = dsp::kMaxBlockFrames;

/// Intervallo minimo fra due emissioni di meter. L'occhio non distingue oltre
/// ~15 aggiornamenti al secondo, mentre ogni signal costa un attraversamento
/// di thread e, a valle, un dataChanged che rilancia le animazioni del delegate.
constexpr qint64 kMeterIntervalNs = 66'000'000; // ~15 Hz

} // namespace

DspEngine::DspEngine(QObject *parent)
    : QObject(parent)
    , m_audioRing(std::make_unique<dsp::SpscRing<float>>(kAudioRingFloats))
    , m_spectrum(new SpectrumFeed(this))
{
    m_interleaved.resize(kProcessBlock * 2);
    m_iq.resize(kProcessBlock);
    m_mix.resize(kProcessBlock);
    m_uptime.start();
}

DspEngine::~DspEngine() = default;

void DspEngine::setSource(dsp::SpscRing<float> *ring, double sampleRate, qint64 centerFrequencyHz)
{
    m_sourceRate.store(sampleRate, std::memory_order_release);
    m_centerHz.store(centerFrequencyHz, std::memory_order_release);
    m_source.store(ring, std::memory_order_release);
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::clearSource()
{
    m_source.store(nullptr, std::memory_order_release);
    m_audioRing->clear();
}

void DspEngine::setCenterFrequency(qint64 hz)
{
    m_centerHz.store(hz, std::memory_order_release);
}

void DspEngine::setRecorder(IqRecorder *recorder)
{
    m_recorder.store(recorder, std::memory_order_release);
}

void DspEngine::setFftSize(int size)
{
    if (size < 256 || (size & (size - 1)) != 0) {
        qCWarning(dsdrDsp) << "dimensione FFT non valida, ignorata:" << size;
        return;
    }
    m_fftSize = size;
    m_needsReconfigure.store(true, std::memory_order_release);
}

void DspEngine::reconfigure()
{
    m_activeRate = m_sourceRate.load(std::memory_order_acquire);
    if (m_activeRate <= 0.0)
        return;

    m_analyzer.configure(m_fftSize, m_activeRate);
    m_analyzer.setAveraging(0.5f);
    m_analyzer.setOverlap(0.5f);

    m_spectrum->configure(m_fftSize, m_activeRate, m_centerHz.load(std::memory_order_acquire));

    for (auto &[id, channel] : m_channels) {
        Q_UNUSED(id)
        channel.processor->configure(m_activeRate, kInternalAudioRate);
        channel.processor->applySettings(channel.settings);
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock), 0.0f);
    }

    m_audioRing->clear();
    m_needsReconfigure.store(false, std::memory_order_release);

    qCInfo(dsdrDsp) << "DSP riconfigurato:" << m_activeRate << "Hz, FFT" << m_fftSize
                    << "canali:" << m_channels.size();
}

void DspEngine::addChannel(ChannelId id, const dsp::ChannelSettings &settings)
{
    if (id == kInvalidChannel || m_channels.find(id) != m_channels.end())
        return;

    Channel channel;
    channel.processor = std::make_unique<dsp::ChannelProcessor>();
    channel.settings = settings;

    if (m_activeRate > 0.0) {
        channel.processor->configure(m_activeRate, kInternalAudioRate);
        channel.processor->applySettings(settings);
        channel.audio.assign(channel.processor->maxAudioFrames(kProcessBlock), 0.0f);
    }

    m_channels.emplace(id, std::move(channel));
}

void DspEngine::updateChannel(ChannelId id, const dsp::ChannelSettings &settings)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        return;

    it->second.settings = settings;
    if (m_activeRate > 0.0)
        it->second.processor->applySettings(settings);
}

void DspEngine::removeChannel(ChannelId id)
{
    m_channels.erase(id);
}

void DspEngine::onIqFrameReady(const hal::IqFrame &frame)
{
    if (frame.droppedFrames > 0) {
        m_totalDropped += frame.droppedFrames;
        // In overrun sostenuto il segnale arriverebbe a ogni frame: la UI ne
        // ricaverebbe solo un flusso di re-layout della barra di stato.
        const qint64 now = m_uptime.nsecsElapsed();
        if (now - m_lastOverrunReportNs >= 500'000'000) {
            m_lastOverrunReportNs = now;
            emit overrunDetected(m_totalDropped);
        }
    }
    processAvailable();
}

void DspEngine::processAvailable()
{
    dsp::SpscRing<float> *source = m_source.load(std::memory_order_acquire);
    if (!source)
        return;

    if (m_needsReconfigure.load(std::memory_order_acquire))
        reconfigure();
    if (m_activeRate <= 0.0)
        return;

    const int decimation =
        std::max(1, static_cast<int>(std::lround(m_activeRate / kInternalAudioRate)));

    while (true) {
        const std::size_t availableFrames = source->available() / 2;
        if (availableFrames == 0)
            break;

        const std::size_t frames = std::min(availableFrames, kProcessBlock);
        const std::size_t got = source->read(m_interleaved.data(), frames * 2);
        const std::size_t count = got / 2;
        if (count == 0)
            break;

        // Tap di registrazione prima di qualunque elaborazione: su disco
        // finisce ciò che la radio ha consegnato, non ciò che il DSP ne ha
        // fatto. `feed()` non blocca e non alloca.
        if (IqRecorder *recorder = m_recorder.load(std::memory_order_acquire))
            recorder->feed(m_interleaved.data(), got);

        for (std::size_t i = 0; i < count; ++i)
            m_iq[i] = Complex(m_interleaved[i * 2], m_interleaved[i * 2 + 1]);

        // ── Ramo spettro: tap in parallelo alla demodulazione (§5.1) ─────
        if (m_analyzer.push(m_iq.data(), count))
            m_spectrum->publish(m_analyzer.magnitudesDb().data());

        // ── Ramo audio ──────────────────────────────────────────────────
        const std::size_t audioFrames = count / static_cast<std::size_t>(decimation);
        if (audioFrames == 0)
            continue;

        std::fill_n(m_mix.begin(), audioFrames, 0.0f);
        bool hasAudio = false;

        for (auto &[channelId, channel] : m_channels) {
            if (!channel.processor)
                continue;

            const std::size_t produced =
                channel.processor->process(m_iq.data(), count, channel.audio.data());
            const std::size_t usable = std::min(produced, audioFrames);
            for (std::size_t i = 0; i < usable; ++i)
                m_mix[i] += channel.audio[i];
            hasAudio = hasAudio || usable > 0;

            const qint64 now = m_uptime.nsecsElapsed();
            if (now - channel.lastMeterNs >= kMeterIntervalNs) {
                channel.lastMeterNs = now;
                emit metersUpdated(channelId,
                                   channel.processor->signalLevelDb(),
                                   channel.processor->agcGainDb());
            }
        }

        if (hasAudio) {
            for (std::size_t i = 0; i < audioFrames; ++i)
                m_mix[i] = std::clamp(m_mix[i], -1.0f, 1.0f);
        }

        // Se il consumatore audio è in ritardo scartiamo il campione più
        // vecchio: meglio un micro-salto che una latenza che cresce senza fine.
        if (m_audioRing->space() < audioFrames)
            m_audioRing->discard(audioFrames - m_audioRing->space());
        m_audioRing->write(m_mix.data(), audioFrames);
    }

    // Il centro può essere cambiato mentre eravamo dentro il ciclo: la
    // geometria dello spettro va allineata prima del prossimo frame.
    const qint64 center = m_centerHz.load(std::memory_order_acquire);
    if (center != m_spectrum->centerFrequency())
        m_spectrum->configure(m_fftSize, m_activeRate, center);
}

} // namespace dsdr::core
