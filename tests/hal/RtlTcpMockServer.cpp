// SPDX-License-Identifier: GPL-3.0-or-later
#include "RtlTcpMockServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace dsdr::test {

namespace {
constexpr int kBlockIntervalMs = 20;
constexpr double kTwoPi = 6.283185307179586;
} // namespace

RtlTcpMockServer::RtlTcpMockServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &RtlTcpMockServer::onNewConnection);

    m_timer = new QTimer(this);
    m_timer->setInterval(kBlockIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &RtlTcpMockServer::sendBlock);
}

RtlTcpMockServer::~RtlTcpMockServer()
{
    stop();
}

bool RtlTcpMockServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

void RtlTcpMockServer::stop()
{
    if (m_timer)
        m_timer->stop();

    // Il membro va azzerato *prima* di chiudere: `abort()` emette
    // `disconnected` in modo sincrono e il nostro stesso gestore azzera
    // m_client, lasciando dangling qualunque uso successivo.
    if (QTcpSocket *socket = m_client) {
        m_client = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    if (m_server && m_server->isListening())
        m_server->close();
}

quint16 RtlTcpMockServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString RtlTcpMockServer::endpoint() const
{
    return QStringLiteral("127.0.0.1:%1").arg(port());
}

void RtlTcpMockServer::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket)
        return;

    // Un client alla volta, come il vero rtl_tcp.
    if (m_client) {
        socket->abort();
        socket->deleteLater();
        return;
    }

    m_client = socket;
    m_incoming.clear();
    connect(m_client, &QTcpSocket::readyRead, this, &RtlTcpMockServer::onClientData);
    connect(m_client, &QTcpSocket::disconnected, this, [this] {
        m_timer->stop();
        if (QTcpSocket *socket = m_client) {
            m_client = nullptr;
            socket->deleteLater();
        }
    });

    if (m_sendGreeting) {
        QByteArray greeting(12, char(0));
        std::memcpy(greeting.data(), "RTL0", 4);
        qToBigEndian<quint32>(m_tunerType, greeting.data() + 4);
        qToBigEndian<quint32>(29u, greeting.data() + 8); // passi di guadagno R820T
        m_client->write(greeting);
        m_client->flush();
        m_timer->start();
    }

    emit clientConnected();
}

void RtlTcpMockServer::onClientData()
{
    if (!m_client)
        return;

    m_incoming.append(m_client->readAll());

    // I comandi sono pacchetti fissi di 5 byte: opcode e intero big-endian.
    while (m_incoming.size() >= 5) {
        const auto command = static_cast<quint8>(m_incoming.at(0));
        const auto value = static_cast<qint32>(
            qFromBigEndian<quint32>(m_incoming.constData() + 1));
        m_incoming.remove(0, 5);

        switch (command) {
        case 0x01: m_lastFrequency = value; break;
        case 0x02: m_lastSampleRate = value; m_sampleRate = value; break;
        case 0x03: m_lastGainMode = value; break;
        default: break;
        }

        ++m_commandCount;
        emit commandReceived(command, value);
    }
}

void RtlTcpMockServer::sendBlock()
{
    if (!m_client || m_client->state() != QAbstractSocket::ConnectedState)
        return;

    // Non accumulare all'infinito se il client non legge: un server vero
    // scarterebbe, e un test non deve consumare memoria senza limite.
    if (m_client->bytesToWrite() > (1 << 22))
        return;

    const int frames = static_cast<int>(m_sampleRate * kBlockIntervalMs / 1000.0);
    QByteArray block(frames * 2, char(0));
    auto *out = reinterpret_cast<quint8 *>(block.data());

    const double step = kTwoPi * m_toneOffsetHz / m_sampleRate;
    for (int i = 0; i < frames; ++i) {
        // Tono complesso al 40% del fondo scala, centrato su 127,5 come fa
        // davvero un RTL-SDR.
        const double re = std::cos(m_phase) * 100.0 + 127.5;
        const double im = std::sin(m_phase) * 100.0 + 127.5;
        out[i * 2] = static_cast<quint8>(std::lround(re));
        out[i * 2 + 1] = static_cast<quint8>(std::lround(im));

        m_phase += step;
        if (m_phase > kTwoPi)
            m_phase -= kTwoPi;
    }

    m_client->write(block);
    m_bytesSent += static_cast<quint64>(block.size());
}

} // namespace dsdr::test
