// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/NetworkAudioSink.h"

#include "audio/AudioRouter.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(dsdrAudio)

namespace dsdr::audio {

namespace {
constexpr int kSampleRate = 48'000;
constexpr int kPacketMilliseconds = 20;
constexpr int kFramesPerPacket = kSampleRate * kPacketMilliseconds / 1000;
constexpr std::size_t kRingSamples = static_cast<std::size_t>(kSampleRate) * 4 * 2;
constexpr qint64 kMaxTcpBacklogBytes = 512 * 1024;
} // namespace

class NetworkAudioSink::Worker final : public QObject
{
    Q_OBJECT

public:
    Worker(dsp::SpscRing<float> *ring, const Config &config,
           std::atomic<quint64> *framesSent, std::atomic<quint64> *framesDropped)
        : m_ring(ring)
        , m_config(config)
        , m_framesSent(framesSent)
        , m_framesDropped(framesDropped)
    {
        m_samples.resize(static_cast<std::size_t>(kFramesPerPacket) * 2);
    }

public slots:
    void start()
    {
        if (m_running)
            return;

        if (m_ring)
            m_ring->clear();

        if (m_config.protocol == Protocol::Udp) {
            m_udp = new QUdpSocket(this);
            // `connectToHost` su UDP non apre una connessione affidabile:
            // fissa soltanto il destinatario e lascia a Qt la risoluzione di
            // un eventuale hostname, senza bloccare il thread DSP.
            m_udp->connectToHost(m_config.host, m_config.port);
            m_detail = tr("UDP verso %1:%2").arg(m_config.host).arg(m_config.port);
        } else {
            QHostAddress address;
            if (m_config.host.isEmpty() || m_config.host == QStringLiteral("*")
                || m_config.host == QStringLiteral("0.0.0.0")) {
                address = QHostAddress::AnyIPv4;
            } else if (!address.setAddress(m_config.host)) {
                fail(tr("L'indirizzo TCP deve essere un IP locale, '*' oppure 0.0.0.0."));
                return;
            }

            m_server = new QTcpServer(this);
            if (!m_server->listen(address, m_config.port)) {
                fail(tr("Impossibile ascoltare TCP %1:%2: %3")
                         .arg(m_config.host).arg(m_config.port).arg(m_server->errorString()));
                return;
            }
            connect(m_server, &QTcpServer::newConnection, this, &Worker::acceptClient);
            m_detail = tr("TCP in ascolto su %1:%2").arg(m_config.host).arg(m_config.port);
        }

        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(kPacketMilliseconds);
        connect(m_timer, &QTimer::timeout, this, &Worker::sendPacket);
        m_timer->start();
        m_running = true;
        emit started(m_detail);
    }

    void stop()
    {
        if (m_timer)
            m_timer->stop();
        if (m_client) {
            m_client->disconnect(this);
            m_client->disconnectFromHost();
            m_client = nullptr;
        }
        if (m_server)
            m_server->close();
        if (m_udp)
            m_udp->close();
        if (m_running)
            emit stopped();
        m_running = false;
    }

private slots:
    void acceptClient()
    {
        while (QTcpSocket *next = m_server->nextPendingConnection()) {
            if (m_client && m_client != next) {
                m_client->disconnect(this);
                m_client->disconnectFromHost();
            }
            m_client = next;
            connect(m_client, &QTcpSocket::disconnected, this, [this] {
                m_client = nullptr;
                emit detailChanged(tr("TCP in ascolto: client disconnesso"));
            });
            emit detailChanged(tr("TCP: client %1 collegato")
                                   .arg(m_client->peerAddress().toString()));
        }
    }

    void sendPacket()
    {
        if (!m_running)
            return;

        const std::size_t sampleCount = m_samples.size();
        std::size_t got = m_ring ? m_ring->read(m_samples.data(), sampleCount) : 0;
        got -= got % 2;
        if (got < sampleCount) {
            std::fill(m_samples.begin() + static_cast<std::ptrdiff_t>(got),
                      m_samples.end(), 0.0f);
            if (m_framesDropped)
                m_framesDropped->fetch_add((sampleCount - got) / 2, std::memory_order_relaxed);
        }

        const int channels = m_config.stereo ? 2 : 1;
        m_payload.resize(kFramesPerPacket * channels * static_cast<int>(sizeof(qint16)));
        for (int frame = 0; frame < kFramesPerPacket; ++frame) {
            const float left = m_samples[static_cast<std::size_t>(frame) * 2];
            const float right = m_samples[static_cast<std::size_t>(frame) * 2 + 1];
            const float mono = (left + right) * 0.5f;
            const auto encode = [](float value) {
                const float limited = std::clamp(value, -1.0f, 1.0f);
                return static_cast<qint16>(std::lround(limited * 32767.0f));
            };
            const int offset = frame * channels * static_cast<int>(sizeof(qint16));
            qToLittleEndian(encode(m_config.stereo ? left : mono), m_payload.data() + offset);
            if (m_config.stereo)
                qToLittleEndian(encode(right), m_payload.data() + offset + sizeof(qint16));
        }

        bool sent = false;
        if (m_udp) {
            sent = m_udp->write(m_payload) == m_payload.size();
        } else if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            if (m_client->bytesToWrite() <= kMaxTcpBacklogBytes)
                sent = m_client->write(m_payload) == m_payload.size();
            else if (m_framesDropped) {
                m_framesDropped->fetch_add(kFramesPerPacket, std::memory_order_relaxed);
            }
        }

        if (sent && m_framesSent)
            m_framesSent->fetch_add(kFramesPerPacket, std::memory_order_relaxed);
    }

private:
    void fail(const QString &message)
    {
        qCWarning(dsdrAudio) << "audio rete:" << message;
        emit failed(message);
        stop();
    }

    dsp::SpscRing<float> *m_ring = nullptr;
    Config m_config;
    std::atomic<quint64> *m_framesSent = nullptr;
    std::atomic<quint64> *m_framesDropped = nullptr;
    QTimer *m_timer = nullptr;
    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    std::vector<float> m_samples;
    QByteArray m_payload;
    QString m_detail;
    bool m_running = false;

signals:
    void started(const QString &detail);
    void stopped();
    void detailChanged(const QString &detail);
    void failed(const QString &message);
};

NetworkAudioSink::NetworkAudioSink(QObject *parent)
    : QObject(parent)
    , m_ring(std::make_unique<dsp::SpscRing<float>>(kRingSamples))
{
}

NetworkAudioSink::~NetworkAudioSink()
{
    stop();
}

QString NetworkAudioSink::protocolName(Protocol protocol)
{
    return protocol == Protocol::Udp ? QStringLiteral("UDP") : QStringLiteral("TCP server");
}

bool NetworkAudioSink::start(const Config &config)
{
    if (config.port == 0 || config.host.trimmed().isEmpty()) {
        m_error = tr("Indirizzo e porta audio di rete sono obbligatori.");
        emit failed(m_error);
        emit stateChanged();
        return false;
    }

    stop();
    const quint64 generation = ++m_generation;
    m_config = config;
    m_error.clear();
    m_detail = tr("Avvio %1…").arg(protocolName(config.protocol));
    m_framesSent.store(0, std::memory_order_release);
    m_framesDropped.store(0, std::memory_order_release);

    m_thread = new QThread(this);
    m_worker = new Worker(m_ring.get(), m_config, &m_framesSent, &m_framesDropped);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Worker::started, this, [this, generation](const QString &detail) {
        if (generation != m_generation)
            return;
        m_detail = detail;
        m_active.store(true, std::memory_order_release);
        m_accepting.store(true, std::memory_order_release);
        qCInfo(dsdrAudio) << "audio rete avviato:" << detail
                           << (m_config.stereo ? "stereo" : "mono") << "PCM16 48 kHz";
        emit stateChanged();
    });
    connect(m_worker, &Worker::detailChanged, this, [this, generation](const QString &detail) {
        if (generation != m_generation)
            return;
        m_detail = detail;
        emit stateChanged();
    });
    connect(m_worker, &Worker::failed, this, [this, generation](const QString &message) {
        if (generation != m_generation)
            return;
        m_error = message;
        m_detail = message;
        m_active.store(false, std::memory_order_release);
        m_accepting.store(false, std::memory_order_release);
        emit failed(message);
        emit stateChanged();
    });
    connect(m_worker, &Worker::stopped, this, [this, generation] {
        if (generation != m_generation)
            return;
        m_active.store(false, std::memory_order_release);
        m_accepting.store(false, std::memory_order_release);
        emit stateChanged();
    });

    m_thread->start();
    QMetaObject::invokeMethod(m_worker, &Worker::start, Qt::QueuedConnection);
    emit stateChanged();
    return true;
}

void NetworkAudioSink::stop()
{
    ++m_generation;
    m_accepting.store(false, std::memory_order_release);
    m_active.store(false, std::memory_order_release);
    if (m_worker && m_thread && m_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &Worker::stop, Qt::BlockingQueuedConnection);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
    }
    m_thread = nullptr;
    m_worker = nullptr;
    if (!m_detail.isEmpty())
        m_detail = tr("Audio di rete fermo");
    emit stateChanged();
}

QVariantMap NetworkAudioSink::status() const
{
    QVariantMap result;
    result.insert(QStringLiteral("active"), isActive());
    result.insert(QStringLiteral("protocol"), protocolName(m_config.protocol));
    result.insert(QStringLiteral("host"), m_config.host);
    result.insert(QStringLiteral("port"), m_config.port);
    result.insert(QStringLiteral("stereo"), m_config.stereo);
    result.insert(QStringLiteral("sampleRate"), kSampleRate);
    result.insert(QStringLiteral("framesSent"),
                  QVariant::fromValue(m_framesSent.load(std::memory_order_acquire)));
    result.insert(QStringLiteral("framesDropped"),
                  QVariant::fromValue(m_framesDropped.load(std::memory_order_acquire)));
    result.insert(QStringLiteral("detail"), m_detail);
    result.insert(QStringLiteral("error"), m_error);
    return result;
}

void NetworkAudioSink::feed(const float *stereo, std::size_t frames) noexcept
{
    if (!stereo || frames == 0 || !m_accepting.load(std::memory_order_acquire))
        return;
    const std::size_t samples = frames * 2;
    if (m_ring->space() < samples)
        m_ring->discard(samples - m_ring->space());
    const std::size_t written = m_ring->write(stereo, samples);
    if (written < samples)
        m_framesDropped.fetch_add((samples - written) / 2, std::memory_order_relaxed);
}

} // namespace dsdr::audio

#include "NetworkAudioSink.moc"
