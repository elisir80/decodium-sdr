// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpyServerMockServer.h"

#include "hal/backends/nettcp/SpyServerProtocol.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cmath>

namespace dsdr::test {

using namespace dsdr::hal::nettcp::spyserver;

namespace {

constexpr int kBlockIntervalMs = 25;
constexpr int kFramesPerBlock = 4096;
constexpr double kTwoPi = 6.283185307179586;

void appendU32(QByteArray &out, quint32 value)
{
    char buffer[4];
    qToLittleEndian<quint32>(value, buffer);
    out.append(buffer, 4);
}

/// Intestazione comune dei messaggi del server.
QByteArray makeHeader(MessageType type, quint32 sequence, quint32 bodySize)
{
    QByteArray header;
    appendU32(header, 0);                                  // protocolId
    appendU32(header, static_cast<quint32>(type));
    appendU32(header, 0);                                  // streamType
    appendU32(header, sequence);
    appendU32(header, bodySize);
    return header;
}

} // namespace

SpyServerMockServer::SpyServerMockServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &SpyServerMockServer::onNewConnection);

    m_timer = new QTimer(this);
    m_timer->setInterval(kBlockIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &SpyServerMockServer::sendIqBlock);
}

SpyServerMockServer::~SpyServerMockServer()
{
    stop();
}

bool SpyServerMockServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

void SpyServerMockServer::stop()
{
    if (m_timer)
        m_timer->stop();

    // Azzerare prima di chiudere: abort() emette disconnected in modo
    // sincrono e il gestore tocca lo stesso puntatore.
    if (QTcpSocket *socket = m_client) {
        m_client = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    if (m_server && m_server->isListening())
        m_server->close();
}

quint16 SpyServerMockServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString SpyServerMockServer::endpoint() const
{
    return QStringLiteral("127.0.0.1:%1").arg(port());
}

void SpyServerMockServer::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket)
        return;

    if (m_client) {
        socket->abort();
        socket->deleteLater();
        return;
    }

    m_client = socket;
    m_incoming.clear();
    m_helloReceived = false;
    m_streaming = false;

    connect(m_client, &QTcpSocket::readyRead, this, &SpyServerMockServer::onClientData);
    connect(m_client, &QTcpSocket::disconnected, this, [this] {
        m_timer->stop();
        if (QTcpSocket *socket = m_client) {
            m_client = nullptr;
            socket->deleteLater();
        }
    });

    // Deliberatamente nessun saluto: un SpyServer aspetta di essere salutato.
}

void SpyServerMockServer::onClientData()
{
    if (!m_client)
        return;

    m_incoming.append(m_client->readAll());

    // Comandi: tipo (u32), dimensione del corpo (u32), corpo.
    while (m_incoming.size() >= 8) {
        const char *data = m_incoming.constData();
        const quint32 command = qFromLittleEndian<quint32>(data);
        const quint32 bodySize = qFromLittleEndian<quint32>(data + 4);

        if (bodySize > (1u << 20))
            return;   // flusso incoerente
        if (static_cast<quint32>(m_incoming.size()) < 8 + bodySize)
            return;   // corpo incompleto

        const QByteArray body = m_incoming.mid(8, static_cast<int>(bodySize));
        m_incoming.remove(0, 8 + static_cast<int>(bodySize));

        if (command == static_cast<quint32>(Command::Hello)) {
            m_helloReceived = true;
            sendDeviceInfo();
        } else if (command == static_cast<quint32>(Command::SetSetting) && body.size() >= 8) {
            const quint32 setting = qFromLittleEndian<quint32>(body.constData());
            const quint32 value = qFromLittleEndian<quint32>(body.constData() + 4);
            ++m_settingCount;

            if (setting == static_cast<quint32>(Setting::IqFrequency))
                m_lastFrequency = value;
            else if (setting == static_cast<quint32>(Setting::StreamingEnabled)) {
                m_streaming = (value != 0);
                if (m_streaming)
                    m_timer->start();
                else
                    m_timer->stop();
            }
        }
    }
}

void SpyServerMockServer::sendDeviceInfo()
{
    if (!m_client)
        return;

    QByteArray body;
    appendU32(body, m_deviceType);
    appendU32(body, 0x1234);                 // seriale
    appendU32(body, m_maximumSampleRate);
    appendU32(body, m_maximumSampleRate);    // banda massima
    appendU32(body, 8);                      // stadi di decimazione
    appendU32(body, 3);                      // stadi di guadagno
    appendU32(body, 21);                     // indice massimo di guadagno
    appendU32(body, 24'000'000);             // frequenza minima
    appendU32(body, 1'800'000'000);          // frequenza massima
    appendU32(body, 12);                     // risoluzione in bit
    appendU32(body, 0);                      // decimazione minima
    appendU32(body, 0);                      // formato forzato

    const QByteArray header = makeHeader(MessageType::DeviceInfo, m_sequence++,
                                         static_cast<quint32>(body.size()));
    m_client->write(header + body);
    m_client->flush();
}

void SpyServerMockServer::sendIqBlock()
{
    if (!m_client || m_client->state() != QAbstractSocket::ConnectedState)
        return;
    if (m_client->bytesToWrite() > (1 << 22))
        return;

    // Tono complesso a int16, il formato che il client richiede.
    QByteArray body;
    body.resize(kFramesPerBlock * 4);
    char *out = body.data();

    const double step = kTwoPi * 0.05;
    for (int i = 0; i < kFramesPerBlock; ++i) {
        const auto re = static_cast<qint16>(std::cos(m_phase) * 12000.0);
        const auto im = static_cast<qint16>(std::sin(m_phase) * 12000.0);
        qToLittleEndian<quint16>(static_cast<quint16>(re), out + i * 4);
        qToLittleEndian<quint16>(static_cast<quint16>(im), out + i * 4 + 2);
        m_phase += step;
        if (m_phase > kTwoPi)
            m_phase -= kTwoPi;
    }

    const QByteArray header = makeHeader(MessageType::Int16Iq, m_sequence++,
                                         static_cast<quint32>(body.size()));
    m_client->write(header + body);
}

} // namespace dsdr::test
