// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/demo/DemoWorker.h"

#include "dsp/SpscRing.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::demo {

namespace {
/// Blocco di generazione: ~21 ms a 192 kHz. Abbastanza corto da non aggiungere
/// latenza percepibile, abbastanza lungo da non far costare nulla il timer.
constexpr std::size_t kBlockFrames = 4096;
} // namespace

DemoWorker::DemoWorker(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_block.resize(kBlockFrames);
    m_interleaved.resize(kBlockFrames * 2);
}

DemoWorker::~DemoWorker() = default;

void DemoWorker::configure(double sampleRate,
                           qint64 centerHz,
                           std::vector<StationSpec> stations)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 192000.0;
    m_centerHz = centerHz;
    m_band.configure(m_sampleRate, centerHz, stations);
    m_framesGenerated = 0;
    if (m_clock.isValid())
        m_clock.restart();
}

void DemoWorker::start()
{
    if (m_running)
        return;

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &DemoWorker::tick);
    }

    m_framesGenerated = 0;
    m_band.reset();
    if (m_ring)
        m_ring->clear();
    m_clock.start();
    m_running = true;
    m_timer->start(5);
}

void DemoWorker::stop()
{
    m_running = false;
    if (m_timer)
        m_timer->stop();
}

void DemoWorker::setCenterFrequency(qint64 hz)
{
    m_centerHz = hz;
    m_band.setCenterFrequency(hz);
}

void DemoWorker::setTransmitting(bool transmitting)
{
    m_transmitting = transmitting;
}

void DemoWorker::setGainReductionDb(double db)
{
    m_gainScale = static_cast<float>(std::pow(10.0, -std::max(0.0, db) / 20.0));
}

void DemoWorker::tick()
{
    if (!m_running || !m_ring)
        return;

    // Quanti campioni avremmo dovuto produrre da `start()` a ora.
    const qint64 elapsedNs = m_clock.nsecsElapsed();
    const quint64 due = static_cast<quint64>(
        static_cast<double>(elapsedNs) * 1e-9 * m_sampleRate);

    if (due <= m_framesGenerated)
        return;

    quint64 pending = due - m_framesGenerated;

    // Se il consumatore si è fermato a lungo non recuperiamo all'infinito:
    // meglio dichiarare i campioni persi che generare un burst inutile.
    constexpr quint64 kMaxCatchUp = kBlockFrames * 8;
    if (pending > kMaxCatchUp) {
        m_framesGenerated += pending - kMaxCatchUp;
        pending = kMaxCatchUp;
    }

    while (pending > 0) {
        const std::size_t count = static_cast<std::size_t>(
            std::min<quint64>(pending, kBlockFrames));

        if (m_transmitting) {
            // Half-duplex simulato: in TX il ricevitore è muto, non fermo.
            std::fill_n(m_block.begin(), count, dsp::Complex(0.0f, 0.0f));
        } else {
            m_band.generate(m_block.data(), count);
        }

        // L'attenuazione chiesta dalla catena agisce qui, dove agirebbe su una
        // radio vera: prima che i campioni entrino nel ring, non a valle.
        if (m_gainScale < 1.0f) {
            for (std::size_t i = 0; i < count; ++i)
                m_block[i] *= m_gainScale;
        }

        for (std::size_t i = 0; i < count; ++i) {
            m_interleaved[i * 2] = m_block[i].real();
            m_interleaved[i * 2 + 1] = m_block[i].imag();
        }

        const std::size_t written = m_ring->write(m_interleaved.data(), count * 2);
        const std::size_t writtenFrames = written / 2;
        const std::size_t dropped = count - writtenFrames;

        m_framesGenerated += count;
        pending -= count;

        emit framesProduced(static_cast<quint32>(writtenFrames),
                            static_cast<quint32>(dropped),
                            static_cast<quint64>(elapsedNs));
    }
}

} // namespace dsdr::hal::demo
