// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/iqfile/IqFileWorker.h"

#include "dsp/SpscRing.h"

#include <QTimer>

#include <algorithm>

namespace dsdr::hal::iqfile {

namespace {

/// Blocco di lettura: ~21 ms a 192 kHz, come il generatore del demo.
constexpr std::size_t kBlockFrames = 4096;

/// Ogni quanto si guarda l'orologio. Più corto dell'intervallo di un blocco,
/// così la consegna resta regolare anche se il timer scivola.
constexpr int kTickMs = 5;

/// Quanto si può recuperare dopo una pausa del consumatore. Oltre, i campioni
/// si dichiarano persi: rincorrere all'infinito farebbe scorrere la
/// registrazione più in fretta del vero.
constexpr quint64 kMaxCatchUpFrames = kBlockFrames * 8;

} // namespace

IqFileWorker::IqFileWorker(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_block.resize(kBlockFrames * 2);
}

IqFileWorker::~IqFileWorker() = default;

void IqFileWorker::openFile(const QString &path)
{
    stop();

    QString error;
    if (!m_reader.open(path, &error)) {
        emit failed(error.isEmpty() ? tr("Impossibile aprire %1.").arg(path) : error);
        return;
    }

    m_lastReportedMs = -1;
    emit opened(m_reader.info());
    emit positionChanged(0, m_reader.info().durationMs);
}

void IqFileWorker::resetClock()
{
    m_framesDelivered = 0;
    m_clock.start();
}

void IqFileWorker::start()
{
    if (m_running || !m_reader.isOpen())
        return;

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &IqFileWorker::tick);
    }

    if (m_ring)
        m_ring->clear();

    resetClock();
    m_running = true;
    m_timer->start(kTickMs);
}

void IqFileWorker::stop()
{
    m_running = false;
    if (m_timer)
        m_timer->stop();
    m_reader.close();
}

void IqFileWorker::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    // Riprendendo si riparte da adesso: il tempo passato in pausa non è
    // ritardo da recuperare.
    if (!m_paused)
        resetClock();
}

void IqFileWorker::setLoop(bool loop)
{
    m_loop = loop;
}

void IqFileWorker::setSpeed(double factor)
{
    factor = std::clamp(factor, 0.1, 8.0);
    if (qFuzzyCompare(m_speed, factor))
        return;
    m_speed = factor;
    resetClock();
}

void IqFileWorker::seekMs(qint64 ms)
{
    if (!m_reader.isOpen())
        return;

    const double rate = m_reader.info().sampleRate;
    m_reader.seek(static_cast<qint64>(ms / 1000.0 * rate));
    if (m_ring)
        m_ring->clear();
    resetClock();

    m_lastReportedMs = -1;
    emit positionChanged(static_cast<qint64>(m_reader.position() * 1000.0 / rate),
                         m_reader.info().durationMs);
}

void IqFileWorker::tick()
{
    if (!m_running || m_paused || !m_ring || !m_reader.isOpen())
        return;

    const double rate = m_reader.info().sampleRate * m_speed;
    if (rate <= 0.0)
        return;

    const qint64 elapsedNs = m_clock.nsecsElapsed();
    const auto due = static_cast<quint64>(static_cast<double>(elapsedNs) * 1e-9 * rate);
    if (due <= m_framesDelivered)
        return;

    quint64 pending = due - m_framesDelivered;
    if (pending > kMaxCatchUpFrames) {
        m_framesDelivered += pending - kMaxCatchUpFrames;
        pending = kMaxCatchUpFrames;
    }

    while (pending > 0) {
        const auto want = static_cast<std::size_t>(std::min<quint64>(pending, kBlockFrames));
        const std::size_t got = m_reader.read(m_block.data(), want);

        if (got == 0) {
            // Fine della registrazione.
            if (!m_loop) {
                m_running = false;
                if (m_timer)
                    m_timer->stop();
                emit finished();
                return;
            }
            m_reader.seek(0);
            resetClock();
            return;
        }

        const std::size_t written = m_ring->write(m_block.data(), got * 2);
        const std::size_t writtenFrames = written / 2;

        m_framesDelivered += got;
        pending -= std::min<quint64>(pending, got);

        emit framesProduced(static_cast<quint32>(writtenFrames),
                            static_cast<quint32>(got - writtenFrames),
                            static_cast<quint64>(elapsedNs));

        if (got < want)
            break;   // il file è finito a metà blocco: si riparte dal prossimo tick
    }

    // La posizione si annuncia al decimo di secondo: la barra di avanzamento
    // non ha bisogno di più, e ogni segnale attraversa un thread.
    const qint64 positionMs = static_cast<qint64>(m_reader.position() * 1000.0
                                                  / m_reader.info().sampleRate);
    if (m_lastReportedMs < 0 || std::abs(positionMs - m_lastReportedMs) >= 100) {
        m_lastReportedMs = positionMs;
        emit positionChanged(positionMs, m_reader.info().durationMs);
    }
}

} // namespace dsdr::hal::iqfile
