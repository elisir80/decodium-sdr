// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/AudioRouter.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstring>

Q_LOGGING_CATEGORY(dsdrAudio, "dsdr.audio")

namespace dsdr::audio {

namespace {
/// Buffer del sink: compromesso fra la latenza obiettivo di RNF-03 (25 ms) e
/// la robustezza su Windows, dove buffer più corti fanno crepitare WASAPI.
constexpr int kTargetLatencyMs = 40;
constexpr int kPrimeIntervalMs = 5;
constexpr int kMaxPrimeAttempts = 100; // 500 ms: Soapy may open after the sink
} // namespace

/// QIODevice di sola lettura che drena il ring del DSP Engine.
class AudioRouter::RingSource : public QIODevice
{
public:
    explicit RingSource(dsp::SpscRing<float> *ring, bool useFloat, int outputChannels)
        : m_ring(ring)
        , m_useFloat(useFloat)
        , m_outputChannels(outputChannels)
    {
        m_scratch.resize(4096 * kSourceChannels);
    }

    void setGain(float gain) noexcept { m_gain.store(gain, std::memory_order_relaxed); }
    quint64 underruns() const noexcept { return m_underruns.load(std::memory_order_relaxed); }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        const std::size_t frames = m_ring
            ? m_ring->available() / kSourceChannels : 0;
        return static_cast<qint64>(frames * bytesPerFrame()) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const std::size_t perFrame = bytesPerFrame();
        std::size_t frames = static_cast<std::size_t>(maxSize) / perFrame;
        if (frames == 0)
            return 0;
        frames = std::min(frames, m_scratch.size() / kSourceChannels);

        const std::size_t requestedSamples = frames * kSourceChannels;
        const std::size_t gotSamples = m_ring
            ? m_ring->read(m_scratch.data(), requestedSamples) : 0;
        const std::size_t gotFrames = gotSamples / kSourceChannels;
        if (gotFrames < frames) {
            // Underrun: silenzio, mai campioni vecchi ripetuti (creerebbero
            // un ronzio periodico molto più fastidioso di un buco).
            std::fill(m_scratch.begin() + gotFrames * kSourceChannels,
                      m_scratch.begin() + frames * kSourceChannels, 0.0f);
            if (m_ring)
                m_underruns.fetch_add(1, std::memory_order_relaxed);
        }

        const float gain = m_gain.load(std::memory_order_relaxed);

        if (m_useFloat) {
            auto *out = reinterpret_cast<float *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                const float left = m_scratch[i * kSourceChannels] * gain;
                const float right = m_scratch[i * kSourceChannels + 1] * gain;
                if (m_outputChannels == 2) {
                    out[i * 2] = left;
                    out[i * 2 + 1] = right;
                } else {
                    out[i] = 0.5f * (left + right);
                }
            }
        } else {
            auto *out = reinterpret_cast<qint16 *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                const float left = m_scratch[i * kSourceChannels] * gain;
                const float right = m_scratch[i * kSourceChannels + 1] * gain;
                if (m_outputChannels == 2) {
                    out[i * 2] = static_cast<qint16>(
                        std::clamp(left, -1.0f, 1.0f) * 32767.0f);
                    out[i * 2 + 1] = static_cast<qint16>(
                        std::clamp(right, -1.0f, 1.0f) * 32767.0f);
                } else {
                    const float v = std::clamp(0.5f * (left + right), -1.0f, 1.0f);
                    out[i] = static_cast<qint16>(v * 32767.0f);
                }
            }
        }

        return static_cast<qint64>(frames * perFrame);
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    static constexpr std::size_t kSourceChannels = 2;

    std::size_t bytesPerFrame() const noexcept
    {
        const std::size_t sampleBytes = m_useFloat ? sizeof(float) : sizeof(qint16);
        return sampleBytes * static_cast<std::size_t>(m_outputChannels);
    }

    dsp::SpscRing<float> *m_ring = nullptr;
    std::vector<float> m_scratch;
    std::atomic<float> m_gain{1.0f};
    std::atomic<quint64> m_underruns{0};
    bool m_useFloat = true;
    int m_outputChannels = 2;
};

AudioRouter::AudioRouter(QObject *parent)
    : QObject(parent)
{
    m_diagnosticTimer = new QTimer(this);
    m_diagnosticTimer->setInterval(1000);
    connect(m_diagnosticTimer, &QTimer::timeout, this, [this] {
        if (!m_sink || !m_source)
            return;
        qCDebug(dsdrAudio) << "audio: device" << m_deviceName
                           << "state" << static_cast<int>(m_sink->state())
                           << "error" << static_cast<int>(m_sink->error())
                           << "ring bytes" << m_source->bytesAvailable()
                           << "underrun" << m_source->underruns();
    });

    m_primeTimer = new QTimer(this);
    m_primeTimer->setInterval(kPrimeIntervalMs);
    connect(m_primeTimer, &QTimer::timeout, this, &AudioRouter::startSinkWhenPrimed);
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
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);

    bool useFloat = true;
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        useFloat = false;
        if (!device.isFormatSupported(format)) {
            format.setChannelCount(1);
            format.setSampleFormat(QAudioFormat::Float);
            useFloat = true;
            if (!device.isFormatSupported(format)) {
                format.setSampleFormat(QAudioFormat::Int16);
                useFloat = false;
            }
            if (!device.isFormatSupported(format)) {
                qCWarning(dsdrAudio) << "il dispositivo non accetta 48 kHz mono/stereo:"
                                     << device.description();
                return false;
            }
        }
    }

    m_format = format;
    m_deviceName = device.description();

    m_sink = std::make_unique<QAudioSink>(device, format);
    const int bytesPerSecond = format.bytesForDuration(1'000'000);
    m_sink->setBufferSize(bytesPerSecond * kTargetLatencyMs / 1000);

    m_source = std::make_unique<RingSource>(source, useFloat, format.channelCount());
    m_source->open(QIODevice::ReadOnly);
    applyVolume();

    m_latencyMs = m_sink->bufferSize() > 0
        ? static_cast<int>(format.durationForBytes(m_sink->bufferSize()) / 1000)
        : kTargetLatencyMs;

    // Il DSP parte su un thread separato: avviare immediatamente QAudioSink
    // lo costringe a leggere un ring ancora vuoto e registra underrun che sono
    // solo startup. Aspettiamo un buffer, con un limite di 500 ms per non
    // nascondere un vero problema di sorgente.
    m_primeAttempts = 0;
    m_primeTimer->start();

    qCInfo(dsdrAudio) << "uscita audio:" << m_deviceName
                      << (useFloat ? "float32" : "int16")
                      << format.channelCount() << "canali" << m_latencyMs << "ms";

    emit activeChanged();
    emit deviceChanged();
    return true;
}

void AudioRouter::stop()
{
    if (m_primeTimer)
        m_primeTimer->stop();
    m_primeAttempts = 0;
    if (m_diagnosticTimer)
        m_diagnosticTimer->stop();
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

void AudioRouter::startSinkWhenPrimed()
{
    if (!m_sink || !m_source || !m_primeTimer)
        return;

    const qint64 formatBufferBytes = m_format.bytesForDuration(
        static_cast<qint64>(kTargetLatencyMs) * 2 * 1000);
    const qint64 requiredBytes = std::max<qint64>(m_sink->bufferSize(),
                                                   formatBufferBytes);
    const bool primed = requiredBytes <= 0
        || m_source->bytesAvailable() >= requiredBytes;
    if (!primed && m_primeAttempts++ < kMaxPrimeAttempts)
        return;

    m_primeTimer->stop();
    m_sink->start(m_source.get());
    m_diagnosticTimer->start();
    qCInfo(dsdrAudio) << "sink audio avviato: ring iniziale"
                      << m_source->bytesAvailable() << "bytes"
                      << (primed ? "primed" : "timeout");
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
