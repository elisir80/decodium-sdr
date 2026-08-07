// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/AudioRouter.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMediaDevices>

#include <algorithm>
#include <atomic>
#include <cstring>

Q_LOGGING_CATEGORY(dsdrAudio, "dsdr.audio")

namespace dsdr::audio {

namespace {
/// Buffer del sink: compromesso fra la latenza obiettivo di RNF-03 (25 ms) e
/// la robustezza su Windows, dove buffer più corti fanno crepitare WASAPI.
constexpr int kTargetLatencyMs = 40;
} // namespace

/// QIODevice di sola lettura che drena il ring del DSP Engine.
class AudioRouter::RingSource : public QIODevice
{
public:
    explicit RingSource(dsp::SpscRing<float> *ring, bool useFloat)
        : m_ring(ring)
        , m_useFloat(useFloat)
    {
        m_scratch.resize(4096);
    }

    void setGain(float gain) noexcept { m_gain.store(gain, std::memory_order_relaxed); }
    quint64 underruns() const noexcept { return m_underruns.load(std::memory_order_relaxed); }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        const std::size_t frames = m_ring ? m_ring->available() : 0;
        return static_cast<qint64>(frames * bytesPerFrame()) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const std::size_t perFrame = bytesPerFrame();
        std::size_t frames = static_cast<std::size_t>(maxSize) / perFrame;
        if (frames == 0)
            return 0;
        frames = std::min(frames, m_scratch.size());

        const std::size_t got = m_ring ? m_ring->read(m_scratch.data(), frames) : 0;
        if (got < frames) {
            // Underrun: silenzio, mai campioni vecchi ripetuti (creerebbero
            // un ronzio periodico molto più fastidioso di un buco).
            std::fill(m_scratch.begin() + got, m_scratch.begin() + frames, 0.0f);
            if (m_ring)
                m_underruns.fetch_add(1, std::memory_order_relaxed);
        }

        const float gain = m_gain.load(std::memory_order_relaxed);

        if (m_useFloat) {
            auto *out = reinterpret_cast<float *>(data);
            for (std::size_t i = 0; i < frames; ++i)
                out[i] = m_scratch[i] * gain;
        } else {
            auto *out = reinterpret_cast<qint16 *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                const float v = std::clamp(m_scratch[i] * gain, -1.0f, 1.0f);
                out[i] = static_cast<qint16>(v * 32767.0f);
            }
        }

        return static_cast<qint64>(frames * perFrame);
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    std::size_t bytesPerFrame() const noexcept { return m_useFloat ? sizeof(float) : sizeof(qint16); }

    dsp::SpscRing<float> *m_ring = nullptr;
    std::vector<float> m_scratch;
    std::atomic<float> m_gain{1.0f};
    std::atomic<quint64> m_underruns{0};
    bool m_useFloat = true;
};

AudioRouter::AudioRouter(QObject *parent)
    : QObject(parent)
{
}

AudioRouter::~AudioRouter()
{
    stop();
}

bool AudioRouter::start(dsp::SpscRing<float> *source)
{
    stop();
    if (!source)
        return false;

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        qCWarning(dsdrAudio) << "nessun dispositivo di uscita audio disponibile";
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(kInternalAudioRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);

    bool useFloat = true;
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        useFloat = false;
        if (!device.isFormatSupported(format)) {
            qCWarning(dsdrAudio) << "il dispositivo non accetta 48 kHz mono:" << device.description();
            return false;
        }
    }

    m_format = format;
    m_deviceName = device.description();

    m_sink = std::make_unique<QAudioSink>(device, format);
    const int bytesPerSecond = format.bytesForDuration(1'000'000);
    m_sink->setBufferSize(bytesPerSecond * kTargetLatencyMs / 1000);

    m_source = std::make_unique<RingSource>(source, useFloat);
    m_source->open(QIODevice::ReadOnly);
    applyVolume();

    m_sink->start(m_source.get());

    m_latencyMs = m_sink->bufferSize() > 0
        ? static_cast<int>(format.durationForBytes(m_sink->bufferSize()) / 1000)
        : kTargetLatencyMs;

    qCInfo(dsdrAudio) << "uscita audio:" << m_deviceName
                      << (useFloat ? "float32" : "int16") << m_latencyMs << "ms";

    emit activeChanged();
    emit deviceChanged();
    return true;
}

void AudioRouter::stop()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (m_source) {
        m_source->close();
        m_source.reset();
    }
    emit activeChanged();
}

bool AudioRouter::isActive() const
{
    return m_sink != nullptr;
}

void AudioRouter::setVolume(float volume)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (qFuzzyCompare(m_volume, volume))
        return;
    m_volume = volume;
    applyVolume();
    emit volumeChanged();
}

void AudioRouter::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    applyVolume();
    emit mutedChanged();
}

void AudioRouter::applyVolume()
{
    if (!m_source)
        return;
    // Curva percettiva: il cursore lineare diventa una variazione udibile
    // uniforme invece di concentrare tutto nell'ultimo quarto di corsa.
    const float gain = m_muted ? 0.0f : m_volume * m_volume;
    m_source->setGain(gain);
}

quint64 AudioRouter::underrunCount() const
{
    return m_source ? m_source->underruns() : 0;
}

} // namespace dsdr::audio
