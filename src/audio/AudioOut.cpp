// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/AudioOut.h"

#include <QAudioSink>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(dsdrAudio)

namespace dsdr::audio {

namespace {
constexpr int kSampleRate = 48000;

/// Mezzo secondo di coda. Il motore TX scrive a blocchi di ventun
/// millisecondi e il driver chiede quando gli pare: una coda stretta
/// trasformerebbe un ritardo di venti millisecondi in un buco in aria.
constexpr std::size_t kRingSamples = kSampleRate / 2;

/// Buffer del dispositivo. Più corto di quello dell'ascolto: in trasmissione
/// la latenza si somma a quella del PTT, e mezzo secondo di ritardo fra
/// «parlo» e «esce» rende impossibile stare in un pile-up.
constexpr int kTargetLatencyMs = 30;
} // namespace

/// QIODevice di sola lettura che svuota il ring verso il dispositivo.
class AudioOut::RingSource : public QIODevice
{
public:
    RingSource(dsp::SpscRing<float> *ring,
               bool useFloat,
               int outputChannels,
               std::atomic<quint64> *underruns)
        : m_ring(ring)
        , m_useFloat(useFloat)
        , m_outputChannels(std::max(1, outputChannels))
        , m_underruns(underruns)
    {
        m_scratch.resize(4096);
    }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override
    {
        // **Sempre** un blocco, anche a ring vuoto: `readData` non resta mai
        // senza niente da dare, perché quando il ring è vuoto produce
        // silenzio. Dichiarare zero manderebbe il sink in stato di attesa a
        // ring vuoto — cioè subito, perché fuori trasmissione il ring **è**
        // vuoto — e da lì non riprenderebbe più: il PTT manderebbe la radio in
        // portante e l'audio resterebbe fermo dentro un anello che nessuno
        // legge più.
        return static_cast<qint64>(m_scratch.size() * bytesPerFrame())
             + QIODevice::bytesAvailable();
    }

protected:
    qint64 writeData(const char *, qint64) override { return 0; }

    qint64 readData(char *data, qint64 maxSize) override
    {
        const std::size_t perFrame = bytesPerFrame();
        std::size_t frames = static_cast<std::size_t>(maxSize) / perFrame;
        if (frames == 0)
            return 0;
        frames = std::min(frames, m_scratch.size());

        const std::size_t got = m_ring ? m_ring->read(m_scratch.data(), frames) : 0;
        if (got < frames) {
            std::fill(m_scratch.begin() + static_cast<std::ptrdiff_t>(got),
                      m_scratch.begin() + static_cast<std::ptrdiff_t>(frames), 0.0f);
            if (m_underruns)
                m_underruns->fetch_add(1, std::memory_order_relaxed);
        }

        // Il segnale è mono e il dispositivo può volerne due: si ripete sullo
        // stesso valore. Una radio prende quello che le serve, e mandare
        // silenzio su un canale la lascerebbe con metà del livello.
        if (m_useFloat) {
            auto *out = reinterpret_cast<float *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                for (int c = 0; c < m_outputChannels; ++c)
                    out[i * m_outputChannels + c] = m_scratch[i];
            }
        } else {
            auto *out = reinterpret_cast<qint16 *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                const float clamped = std::clamp(m_scratch[i], -1.0f, 1.0f);
                const auto value = static_cast<qint16>(clamped * 32767.0f);
                for (int c = 0; c < m_outputChannels; ++c)
                    out[i * m_outputChannels + c] = value;
            }
        }

        return static_cast<qint64>(frames * perFrame);
    }

private:
    std::size_t bytesPerFrame() const
    {
        return static_cast<std::size_t>(m_useFloat ? 4 : 2) * m_outputChannels;
    }

    dsp::SpscRing<float> *m_ring = nullptr;
    std::vector<float> m_scratch;
    bool m_useFloat = true;
    int m_outputChannels = 1;
    std::atomic<quint64> *m_underruns = nullptr;
};

AudioOut::AudioOut(QObject *parent)
    : QObject(parent)
    , m_ring(std::make_unique<dsp::SpscRing<float>>(kRingSamples))
{
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);
}

AudioOut::~AudioOut()
{
    stop();
}

QList<QAudioDevice> AudioOut::outputs()
{
    return QMediaDevices::audioOutputs();
}

bool AudioOut::start(const QAudioDevice &device)
{
    if (isActive())
        return true;

    if (device.isNull()) {
        m_error = tr("Nessuna uscita audio disponibile.");
        return false;
    }

    QAudioFormat format = m_format;
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelCount(std::max(1, device.preferredFormat().channelCount()));
        if (!device.isFormatSupported(format)) {
            format = device.preferredFormat();
            format.setSampleRate(kSampleRate);
        }
    }
    if (!device.isFormatSupported(format)) {
        m_error = tr("L'uscita audio non accetta 48 kHz.");
        return false;
    }

    m_format = format;
    m_deviceName = device.description();
    m_error.clear();
    m_ring->clear();

    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferSize(format.bytesForDuration(kTargetLatencyMs * 1000));

    m_source = std::make_unique<RingSource>(m_ring.get(),
                                            format.sampleFormat() == QAudioFormat::Float,
                                            format.channelCount(),
                                            &m_underruns);
    m_source->open(QIODevice::ReadOnly);
    m_sink->start(m_source.get());

    // Lo stato del sink è l'unico posto da cui si vede che l'uscita ha smesso
    // di tirare: senza questa riga, un'uscita ferma e un'uscita che suona
    // silenzio hanno lo stesso aspetto.
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        qCInfo(dsdrAudio) << "uscita audio" << m_deviceName << "stato" << state;
    });

    if (m_sink->error() != QAudio::NoError) {
        m_error = tr("L'uscita audio non si è aperta.");
        stop();
        return false;
    }

    qCInfo(dsdrAudio) << "uscita audio:" << m_deviceName
                      << format.sampleRate() << "Hz"
                      << format.channelCount() << "canali";
    emit activeChanged();
    return true;
}

void AudioOut::stop()
{
    if (!m_sink)
        return;
    m_sink->stop();
    m_sink.reset();
    m_source.reset();
    emit activeChanged();
}

bool AudioOut::isActive() const
{
    // IdleState conta come attivo: significa che il dispositivo è aperto e ha
    // finito i campioni, che fuori trasmissione è la condizione normale.
    return m_sink && m_sink->state() != QAudio::StoppedState;
}

} // namespace dsdr::audio
