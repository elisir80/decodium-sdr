// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IqRecorder.h"
#include "core/IqRecorderWriter.h"

#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// ~1,3 s di IQ a 2,048 MS/s: copre abbondantemente l'intervallo di scrittura
/// e assorbe le pause del filesystem senza bucare la registrazione.
constexpr std::size_t kRecordRingFloats = 1 << 23;

constexpr int kProgressIntervalMs = 500;

} // namespace

IqRecorder::IqRecorder(QObject *parent)
    : QObject(parent)
    , m_ring(std::make_unique<dsp::SpscRing<float>>(kRecordRingFloats))
{
    qRegisterMetaType<dsdr::core::IqRecordingInfo>("dsdr::core::IqRecordingInfo");
}

IqRecorder::~IqRecorder()
{
    stop();
}

QString IqRecorder::defaultDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return (base.isEmpty() ? QDir::homePath() : base) + QStringLiteral("/DECODIUM SDR");
}

QString IqRecorder::suggestedFileName(const IqRecordingInfo &info)
{
    const QDateTime when = info.startedAt.isValid() ? info.startedAt : QDateTime::currentDateTime();
    const double mhz = info.centerFrequencyHz / 1e6;

    // Data prima della frequenza: così l'ordine alfabetico è anche cronologico.
    if (info.audio) {
        return QStringLiteral("%1_audio_%2kSps.wav")
            .arg(when.toString(QStringLiteral("yyyyMMdd_HHmmss")))
            .arg(info.sampleRate / 1000.0, 0, 'f', 0);
    }

    return QStringLiteral("%1_%2MHz_%3kSps.wav")
        .arg(when.toString(QStringLiteral("yyyyMMdd_HHmmss")))
        .arg(mhz, 0, 'f', 3)
        .arg(info.sampleRate / 1000.0, 0, 'f', 0);
}

bool IqRecorder::start(const IqRecordingInfo &info, const QString &path)
{
    if (isRecording())
        return false;
    if (info.sampleRate <= 0.0) {
        emit failed(tr("Nessuna sorgente attiva da registrare."));
        return false;
    }

    IqRecordingInfo effective = info;
    if (!effective.startedAt.isValid())
        effective.startedAt = QDateTime::currentDateTime();
    m_audio.store(effective.audio, std::memory_order_release);

    m_currentFile = path.isEmpty()
        ? defaultDirectory() + QLatin1Char('/') + suggestedFileName(effective)
        : path;

    m_ring->clear();
    m_dropped.store(0, std::memory_order_relaxed);

    m_writer = new IqRecorderWriter(m_ring.get());
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dsdr-iq-recorder"));
    m_writer->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_writer, &QObject::deleteLater);

    connect(m_writer, &IqRecorderWriter::failed, this, [this](const QString &message) {
        qCWarning(dsdrCore) << "registrazione:" << message;
        emit failed(message);
        stop();
    });

    m_thread->start();
    QMetaObject::invokeMethod(m_writer, "open", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentFile),
                              Q_ARG(dsdr::core::IqRecordingInfo, effective));

    if (!m_progressTimer) {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(kProgressIntervalMs);
        connect(m_progressTimer, &QTimer::timeout, this, &IqRecorder::progressChanged);
    }
    m_progressTimer->start();

    // L'ordine conta: il flag va alzato solo quando il writer è pronto a
    // ricevere, altrimenti il thread DSP riempirebbe il ring nel vuoto.
    m_recording.store(true, std::memory_order_release);
    emit recordingChanged();

    qCInfo(dsdrCore) << "registrazione avviata:" << m_currentFile;
    return true;
}

bool IqRecorder::startAudio(double sampleRate, qint64 centerFrequencyHz,
                            const QString &deviceName, const QString &path)
{
    IqRecordingInfo info;
    info.centerFrequencyHz = centerFrequencyHz;
    info.sampleRate = sampleRate;
    info.backendId = QStringLiteral("audio");
    info.deviceName = deviceName;
    info.startedAt = QDateTime::currentDateTime();
    info.audio = true;
    return start(info, path);
}

void IqRecorder::stop()
{
    if (!isRecording() && !m_thread)
        return;

    m_recording.store(false, std::memory_order_release);
    m_audio.store(false, std::memory_order_release);

    if (m_progressTimer)
        m_progressTimer->stop();

    if (m_thread) {
        if (m_writer && m_thread->isRunning())
            QMetaObject::invokeMethod(m_writer, "close", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_writer = nullptr;
    }

    emit recordingChanged();
    emit progressChanged();
}

void IqRecorder::feed(const float *interleaved, std::size_t floatCount) noexcept
{
    if (!m_recording.load(std::memory_order_acquire) || floatCount == 0)
        return;

    const std::size_t written = m_ring->write(interleaved, floatCount);
    if (written < floatCount) {
        // Il disco non tiene il passo. Si conta e si prosegue: interrompere la
        // registrazione per un buco sarebbe peggio del buco.
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

qint64 IqRecorder::bytesWritten() const
{
    return m_writer ? m_writer->bytesWritten() : 0;
}

qint64 IqRecorder::durationMs() const
{
    return m_writer ? m_writer->durationMs() : 0;
}

int IqRecorder::droppedBlocks() const
{
    return m_dropped.load(std::memory_order_relaxed);
}

} // namespace dsdr::core
