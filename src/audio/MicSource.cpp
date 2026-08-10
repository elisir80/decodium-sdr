// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/MicSource.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

Q_DECLARE_LOGGING_CATEGORY(dsdrAudio)

namespace dsdr::audio {

namespace {
/// Frequenza dell'audio di trasmissione. È la stessa di quella di ricezione,
/// e non è una coincidenza: tutta la catena TX è tarata qui, e i rapporti con
/// le frequenze dei device (192, 384, 768 kS/s) restano interi.
constexpr int kSampleRate = 48000;

/// Due secondi di coda. Serve larga perché il motore TX legge a blocchi e il
/// driver consegna quando gli pare: un ring stretto trasformerebbe un ritardo
/// di venti millisecondi in un buco udibile in aria.
constexpr std::size_t kRingSamples = kSampleRate * 2;

constexpr int kTargetLatencyMs = 40;
} // namespace

/// QIODevice di sola scrittura: il driver ci consegna i campioni chiamando
/// `writeData`, e noi li mettiamo nel ring convertendoli in float.
class MicSource::RingSink : public QIODevice
{
public:
    RingSink(dsp::SpscRing<float> *ring,
             bool useFloat,
             int inputChannels,
             std::atomic<float> *peak,
             std::atomic<quint64> *overruns)
        : m_ring(ring)
        , m_useFloat(useFloat)
        , m_inputChannels(std::max(1, inputChannels))
        , m_peak(peak)
        , m_overruns(overruns)
    {
        m_scratch.resize(4096);
    }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *, qint64) override { return 0; }

    qint64 writeData(const char *data, qint64 maxSize) override
    {
        const int bytesPerSample = m_useFloat ? 4 : 2;
        const std::size_t frames = static_cast<std::size_t>(maxSize)
                                 / (bytesPerSample * m_inputChannels);
        if (frames == 0)
            return maxSize;

        std::size_t done = 0;
        float peak = 0.0f;

        while (done < frames) {
            const std::size_t chunk = std::min(frames - done, m_scratch.size());

            for (std::size_t i = 0; i < chunk; ++i) {
                const std::size_t frame = done + i;
                float sample = 0.0f;
                // Più canali si sommano e si mediano: un microfono stereo
                // consegna spesso il segnale su un solo canale, e prendere
                // sempre il primo darebbe silenzio a metà degli utenti.
                if (m_useFloat) {
                    const auto *in = reinterpret_cast<const float *>(data);
                    for (int c = 0; c < m_inputChannels; ++c)
                        sample += in[frame * m_inputChannels + c];
                } else {
                    const auto *in = reinterpret_cast<const qint16 *>(data);
                    for (int c = 0; c < m_inputChannels; ++c)
                        sample += in[frame * m_inputChannels + c] / 32768.0f;
                }
                sample /= static_cast<float>(m_inputChannels);
                m_scratch[i] = sample;
                peak = std::max(peak, std::abs(sample));
            }

            const std::size_t written = m_ring ? m_ring->write(m_scratch.data(), chunk) : chunk;
            if (written < chunk && m_overruns)
                m_overruns->fetch_add(chunk - written, std::memory_order_relaxed);
            done += chunk;
        }

        if (m_peak)
            m_peak->store(peak, std::memory_order_relaxed);

        // Si dichiara sempre di aver consumato tutto: dire al driver che si è
        // preso meno lo farebbe ritentare con gli stessi campioni, e un
        // consumatore lento diventerebbe un blocco dell'audio di sistema.
        return maxSize;
    }

private:
    dsp::SpscRing<float> *m_ring = nullptr;
    std::vector<float> m_scratch;
    bool m_useFloat = true;
    int m_inputChannels = 1;
    std::atomic<float> *m_peak = nullptr;
    std::atomic<quint64> *m_overruns = nullptr;
};

MicSource::MicSource(QObject *parent)
    : QObject(parent)
    , m_ring(std::make_unique<dsp::SpscRing<float>>(kRingSamples))
{
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);
}

MicSource::~MicSource()
{
    stop();
}

QList<QAudioDevice> MicSource::inputs()
{
    return QMediaDevices::audioInputs();
}

bool MicSource::start()
{
    return start(QMediaDevices::defaultAudioInput());
}

bool MicSource::start(const QAudioDevice &device)
{
    if (isActive())
        return true;

    if (device.isNull()) {
        m_error = tr("Nessun ingresso audio disponibile.");
        return false;
    }

    QAudioFormat format = m_format;
    if (!device.isFormatSupported(format)) {
        // Ripiego su interi a 16 bit e sul numero di canali che il dispositivo
        // preferisce: la conversione la facciamo noi, ed è meno cara di un
        // microfono che non si apre.
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelCount(std::max(1, device.preferredFormat().channelCount()));
        if (!device.isFormatSupported(format)) {
            format = device.preferredFormat();
            format.setSampleRate(kSampleRate);
        }
    }
    if (!device.isFormatSupported(format)) {
        m_error = tr("Il microfono non accetta 48 kHz.");
        return false;
    }

    m_format = format;
    m_deviceName = device.description();
    m_error.clear();
    m_ring->clear();

    m_source = std::make_unique<QAudioSource>(device, format);
    m_source->setBufferSize(format.bytesForDuration(kTargetLatencyMs * 1000));

    m_sink = std::make_unique<RingSink>(m_ring.get(),
                                        format.sampleFormat() == QAudioFormat::Float,
                                        format.channelCount(),
                                        &m_peak, &m_overruns);
    m_sink->open(QIODevice::WriteOnly);
    m_source->start(m_sink.get());

    if (m_source->error() != QAudio::NoError) {
        m_error = tr("Il microfono non si è aperto.");
        stop();
        return false;
    }

    qCInfo(dsdrAudio) << "microfono:" << m_deviceName
                      << format.sampleRate() << "Hz"
                      << format.channelCount() << "canali";
    emit deviceChanged();
    emit activeChanged();
    return true;
}

void MicSource::stop()
{
    if (!m_source)
        return;
    m_source->stop();
    m_source.reset();
    m_sink.reset();
    m_peak.store(0.0f, std::memory_order_relaxed);
    emit activeChanged();
}

bool MicSource::isActive() const
{
    return m_source && m_source->state() == QAudio::ActiveState;
}

} // namespace dsdr::audio
