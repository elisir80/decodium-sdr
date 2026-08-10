// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NeuralNrWorker.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// Ogni quanto il worker guarda se c'è lavoro. Cinque millisecondi sono più
/// corti del blocco della rete (dieci) e molto più corti del buffer della
/// scheda audio: nessuno se ne accorge, e il thread resta quasi sempre fermo.
constexpr int kPumpIntervalMs = 5;

/// Capienza del ring d'uscita: come quello del DSP, circa un secondo.
constexpr std::size_t kOutputFloats = 1 << 16;

/// Quanto lavoro si prende in un giro. Blocchi troppo grandi allungano la
/// latenza, troppo piccoli moltiplicano le chiamate.
constexpr std::size_t kChunkFrames = 960;   // 20 ms a 48 kHz

/// Sopra questa quota di tempo reale, per questo numero di giri consecutivi,
/// lo stadio si arrende. La striscia serve a non spegnersi al primo
/// singhiozzo del sistema operativo, che non è colpa dell'inferenza.
constexpr double kOverloadThreshold = 0.9;
constexpr int kOverloadStreak = 25;

} // namespace

NeuralNrWorker::NeuralNrWorker(QObject *parent)
    : QObject(parent)
    , m_output(std::make_unique<dsp::SpscRing<float>>(kOutputFloats))
{
    m_interleaved.resize(kChunkFrames * 2);
    m_mono.resize(kChunkFrames);
    m_clock.start();
}

NeuralNrWorker::~NeuralNrWorker() = default;

void NeuralNrWorker::setSource(dsp::SpscRing<float> *ring, double sampleRate, int channels)
{
    m_sourceRate.store(sampleRate, std::memory_order_release);
    m_channels.store(std::clamp(channels, 1, 2), std::memory_order_release);
    m_needsReset.store(true, std::memory_order_release);
    m_source.store(ring, std::memory_order_release);
}

void NeuralNrWorker::setEnabled(bool enabled)
{
    if (enabled && !dsp::NeuralDenoiser::isAvailable())
        return;
    if (m_enabled.exchange(enabled, std::memory_order_acq_rel) == enabled)
        return;

    m_needsReset.store(true, std::memory_order_release);
    if (!enabled)
        m_load.store(0.0, std::memory_order_release);
    emit enabledChanged(enabled);
}

void NeuralNrWorker::start()
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    m_timer->setInterval(kPumpIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &NeuralNrWorker::pump);
    m_timer->start();
}

void NeuralNrWorker::stop()
{
    if (m_timer)
        m_timer->stop();
}

void NeuralNrWorker::pump()
{
    dsp::SpscRing<float> *source = m_source.load(std::memory_order_acquire);
    if (!source)
        return;

    const int channels = m_channels.load(std::memory_order_acquire);
    const bool enabled = m_enabled.load(std::memory_order_acquire);
    const double rate = m_sourceRate.load(std::memory_order_acquire);

    if (m_needsReset.exchange(false, std::memory_order_acq_rel)) {
        m_left.configure(rate);
        m_right.configure(rate);
        m_left.reset();
        m_right.reset();
        m_overrunStreak = 0;
    }

    const auto chunkFloats = kChunkFrames * static_cast<std::size_t>(channels);

    while (true) {
        const std::size_t available = source->available();
        if (available < chunkFloats)
            break;

        // Se a valle nessuno consuma, si smette di produrre: riempire il ring
        // d'uscita a forza vorrebbe dire scartare audio già elaborato, cioè
        // spendere l'inferenza per buttarla via.
        if (m_output->space() < chunkFloats)
            break;

        const std::size_t got = source->read(m_interleaved.data(), chunkFloats);
        if (got == 0)
            break;

        const std::size_t frames = got / static_cast<std::size_t>(channels);
        const qint64 startNs = m_clock.nsecsElapsed();

        if (enabled && m_left.isConfigured()) {
            if (channels == 1) {
                m_left.process(m_interleaved.data(), frames);
            } else {
                // Un canale per volta: la rete è monofonica, e sommare i due
                // in uno cancellerebbe la spazializzazione del CW binaurale —
                // che è esattamente la ragione per cui esiste.
                for (int ch = 0; ch < 2; ++ch) {
                    dsp::NeuralDenoiser &engine = (ch == 0) ? m_left : m_right;
                    for (std::size_t i = 0; i < frames; ++i)
                        m_mono[i] = m_interleaved[i * 2 + static_cast<std::size_t>(ch)];
                    engine.process(m_mono.data(), frames);
                    for (std::size_t i = 0; i < frames; ++i)
                        m_interleaved[i * 2 + static_cast<std::size_t>(ch)] = m_mono[i];
                }
            }
            m_speech.store(m_left.speechProbability(), std::memory_order_release);

            // Costo, come quota del tempo reale che quel blocco rappresenta.
            const double elapsed = static_cast<double>(m_clock.nsecsElapsed() - startNs) / 1e9;
            const double realTime = static_cast<double>(frames) / std::max(rate, 1.0);
            const double load = elapsed / std::max(realTime, 1e-9);
            m_load.store(load, std::memory_order_release);

            if (load > kOverloadThreshold) {
                if (++m_overrunStreak >= kOverloadStreak) {
                    // Meglio dirlo e spegnersi che consegnare audio a scatti:
                    // un difetto che si sente ma non si spiega è il peggiore.
                    m_enabled.store(false, std::memory_order_release);
                    m_overrunStreak = 0;
                    emit overrun(load);
                    emit enabledChanged(false);
                }
            } else {
                m_overrunStreak = 0;
            }
        }

        m_output->write(m_interleaved.data(), got);
    }
}

} // namespace dsdr::core
