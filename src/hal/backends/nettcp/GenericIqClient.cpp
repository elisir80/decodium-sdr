// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/GenericIqClient.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsdr::hal::nettcp {

namespace {
constexpr int kMaxFramesPerPass = 16'384;

IqSampleFormat formatFromInt(int value)
{
    switch (value) {
    case static_cast<int>(IqSampleFormat::Int8):    return IqSampleFormat::Int8;
    case static_cast<int>(IqSampleFormat::Int16):   return IqSampleFormat::Int16;
    case static_cast<int>(IqSampleFormat::Int32):   return IqSampleFormat::Int32;
    case static_cast<int>(IqSampleFormat::Float32): return IqSampleFormat::Float32;
    default:                                         return IqSampleFormat::Int16;
    }
}

float floatFromLittleEndian(const char *data)
{
    const quint32 bits = qFromLittleEndian<quint32>(data);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits), "float32 wire conversion expects 32 bits");
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace

GenericIqClient::GenericIqClient(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_scratch.resize(static_cast<std::size_t>(kMaxFramesPerPass) * 2);
}

GenericIqClient::~GenericIqClient()
{
    disconnectFromSource();
}

void GenericIqClient::connectToSource(const QString &host, quint16 port, bool udp, int sampleFormat)
{
    disconnectFromSource();
    m_format = formatFromInt(sampleFormat);
    m_pending.clear();
    if (m_ring)
        m_ring->clear();
    m_clock.start();

    if (udp) {
        m_udp = new QUdpSocket(this);
        if (!m_udp->bind(QHostAddress::AnyIPv4, port,
                         QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            const QString message = tr("Impossibile aprire UDP %1: %2")
                                        .arg(port).arg(m_udp->errorString());
            qCWarning(dsdrHal) << "netiq:" << message;
            emit failed(message, true);
            m_udp->deleteLater();
            m_udp = nullptr;
            return;
        }
        connect(m_udp, &QUdpSocket::readyRead, this, &GenericIqClient::onUdpReadyRead);
        connect(m_udp, &QUdpSocket::errorOccurred, this, &GenericIqClient::onSocketError);
        qCInfo(dsdrHal) << "netiq: ascolto UDP" << port << "mittente atteso" << host
                         << iqSampleFormatName(m_format);
        emit connected();
        return;
    }

    m_tcp = new QTcpSocket(this);
    m_tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_tcp->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1 << 20);
    connect(m_tcp, &QTcpSocket::connected, this, [this, host, port] {
        qCInfo(dsdrHal) << "netiq: connesso TCP" << host << port
                         << iqSampleFormatName(m_format);
        emit connected();
    });
    connect(m_tcp, &QTcpSocket::readyRead, this, &GenericIqClient::onTcpReadyRead);
    connect(m_tcp, &QTcpSocket::errorOccurred, this, &GenericIqClient::onSocketError);
    connect(m_tcp, &QTcpSocket::disconnected, this, &GenericIqClient::onDisconnected);
    qCInfo(dsdrHal) << "netiq: connessione TCP a" << host << port;
    m_tcp->connectToHost(host, port);
}

void GenericIqClient::disconnectFromSource()
{
    if (m_tcp) {
        m_tcp->disconnect(this);
        m_tcp->abort();
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
    if (m_udp) {
        m_udp->disconnect(this);
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
    m_pending.clear();
}

void GenericIqClient::onTcpReadyRead()
{
    if (m_tcp)
        appendBytes(m_tcp->readAll());
}

void GenericIqClient::onUdpReadyRead()
{
    if (!m_udp)
        return;
    while (m_udp->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(datagram.data(), datagram.size());
        appendBytes(datagram);
    }
}

void GenericIqClient::onSocketError()
{
    const QString message = m_tcp ? m_tcp->errorString()
                                  : m_udp ? m_udp->errorString() : QString();
    if (message.isEmpty())
        return;
    qCWarning(dsdrHal) << "netiq: errore di socket:" << message;
    emit failed(message, true);
}

void GenericIqClient::onDisconnected()
{
    emit disconnected();
}

void GenericIqClient::appendBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;
    m_pending.append(bytes);
    processPending();
}

void GenericIqClient::processPending()
{
    const int bytesPerFrame = bytesPerIqFrame(m_format);
    const quint64 timestampNs = static_cast<quint64>(m_clock.nsecsElapsed());
    while (m_pending.size() >= bytesPerFrame) {
        const int availableFrames = m_pending.size() / bytesPerFrame;
        const int frames = std::min(availableFrames, kMaxFramesPerPass);
        const char *raw = m_pending.constData();
        for (int frame = 0; frame < frames; ++frame) {
            const int offset = frame * bytesPerFrame;
            float i = 0.0f;
            float q = 0.0f;
            switch (m_format) {
            case IqSampleFormat::Int8:
                i = static_cast<float>(static_cast<qint8>(raw[offset])) / 128.0f;
                q = static_cast<float>(static_cast<qint8>(raw[offset + 1])) / 128.0f;
                break;
            case IqSampleFormat::Int16:
                i = static_cast<float>(qFromLittleEndian<qint16>(raw + offset)) / 32768.0f;
                q = static_cast<float>(qFromLittleEndian<qint16>(raw + offset + 2)) / 32768.0f;
                break;
            case IqSampleFormat::Int32:
                i = static_cast<float>(qFromLittleEndian<qint32>(raw + offset)
                                        / 2147483648.0);
                q = static_cast<float>(qFromLittleEndian<qint32>(raw + offset + 4)
                                        / 2147483648.0);
                break;
            case IqSampleFormat::Float32:
                i = floatFromLittleEndian(raw + offset);
                q = floatFromLittleEndian(raw + offset + 4);
                break;
            }
            m_scratch[static_cast<std::size_t>(frame) * 2] = i;
            m_scratch[static_cast<std::size_t>(frame) * 2 + 1] = q;
        }

        std::size_t written = 0;
        if (m_ring)
            written = m_ring->write(m_scratch.data(), static_cast<std::size_t>(frames) * 2);
        m_pending.remove(0, frames * bytesPerFrame);
        emit samplesProduced(static_cast<quint32>(written / 2),
                             static_cast<quint32>(frames - static_cast<int>(written / 2)),
                             timestampNs);
    }
}

} // namespace dsdr::hal::nettcp
