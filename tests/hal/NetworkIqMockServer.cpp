// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetworkIqMockServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QtEndian>

#include <cstring>

namespace dsdr::test {

namespace {
constexpr quint32 kPacketCommand = 0;
constexpr quint32 kPacketCommandAck = 1;
constexpr quint32 kPacketBaseband = 2;
constexpr quint32 kCommandGetUi = 0;
constexpr quint32 kCommandStart = 2;
constexpr quint32 kCommandSetFrequency = 4;
constexpr quint32 kCommandSetSampleRate = 0x80;
constexpr quint32 kHeaderSize = 8;
constexpr quint32 kCommandHeaderSize = 4;

QByteArray doubleBody(double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray body(sizeof(bits), Qt::Uninitialized);
    qToLittleEndian(bits, body.data());
    return body;
}

double readDouble(const char *data)
{
    const quint64 bits = qFromLittleEndian<quint64>(data);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
} // namespace

RawIqTcpMockServer::RawIqTcpMockServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            QByteArray iq(4 * 4, Qt::Uninitialized);
            // Quattro coppie IQ int16 LE, volutamente non simmetriche: il
            // test presidia ordine I/Q, scala e segno.
            const qint16 values[] = {32767, 0, 0, -16384, -32768, 16384, 8192, -8192};
            for (int i = 0; i < 8; ++i)
                qToLittleEndian(values[i], iq.data() + i * static_cast<int>(sizeof(qint16)));
            socket->write(iq);
        }
    });
}

bool RawIqTcpMockServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost);
}

quint16 RawIqTcpMockServer::port() const
{
    return m_server->serverPort();
}

QString RawIqTcpMockServer::endpoint() const
{
    return QStringLiteral("tcp://127.0.0.1:%1?rate=96000&format=int16").arg(port());
}

SdrppServerMockServer::SdrppServerMockServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                QByteArray &pending = m_pending[socket];
                pending.append(socket->readAll());
                while (pending.size() >= static_cast<int>(kHeaderSize)) {
                    const quint32 type = qFromLittleEndian<quint32>(pending.constData());
                    const quint32 size = qFromLittleEndian<quint32>(pending.constData() + 4);
                    if (size < kHeaderSize || size > 1'000'000 || pending.size() < size)
                        return;
                    const QByteArray body = pending.mid(static_cast<int>(kHeaderSize),
                                                        static_cast<int>(size - kHeaderSize));
                    pending.remove(0, static_cast<int>(size));
                    handlePacket(socket, type, body);
                }
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_pending.remove(socket);
                socket->deleteLater();
            });
            sendCommand(socket, kCommandSetSampleRate, doubleBody(1'024'000.0));
        }
    });
}

bool SdrppServerMockServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost);
}

quint16 SdrppServerMockServer::port() const
{
    return m_server->serverPort();
}

QString SdrppServerMockServer::endpoint() const
{
    return QStringLiteral("sdrpp://127.0.0.1:%1?format=int16").arg(port());
}

void SdrppServerMockServer::handlePacket(QTcpSocket *socket, quint32 type, const QByteArray &body)
{
    if (type != kPacketCommand || body.size() < static_cast<int>(kCommandHeaderSize))
        return;
    const quint32 command = qFromLittleEndian<quint32>(body.constData());
    const QByteArray payload = body.mid(static_cast<int>(kCommandHeaderSize));
    if (command == kCommandGetUi) {
        sendPacket(socket, kPacketCommandAck, body.left(static_cast<int>(kCommandHeaderSize)));
    } else if (command == kCommandSetFrequency && payload.size() == static_cast<int>(sizeof(double))) {
        m_requestedFrequencyHz = static_cast<qint64>(readDouble(payload.constData()));
        sendPacket(socket, kPacketCommandAck, body.left(static_cast<int>(kCommandHeaderSize)));
    } else if (command == kCommandStart) {
        m_receivedStart = true;
        sendIq(socket);
    }
}

void SdrppServerMockServer::sendPacket(QTcpSocket *socket, quint32 type, const QByteArray &body)
{
    QByteArray packet(static_cast<int>(kHeaderSize + body.size()), Qt::Uninitialized);
    qToLittleEndian(type, packet.data());
    qToLittleEndian(static_cast<quint32>(packet.size()), packet.data() + 4);
    if (!body.isEmpty())
        std::memcpy(packet.data() + kHeaderSize, body.constData(), static_cast<std::size_t>(body.size()));
    socket->write(packet);
}

void SdrppServerMockServer::sendCommand(QTcpSocket *socket, quint32 command, const QByteArray &body)
{
    QByteArray payload(static_cast<int>(kCommandHeaderSize + body.size()), Qt::Uninitialized);
    qToLittleEndian(command, payload.data());
    if (!body.isEmpty())
        std::memcpy(payload.data() + kCommandHeaderSize, body.constData(),
                    static_cast<std::size_t>(body.size()));
    sendPacket(socket, kPacketCommand, payload);
}

void SdrppServerMockServer::sendIq(QTcpSocket *socket)
{
    QByteArray iq(4 * 4, Qt::Uninitialized);
    const qint16 values[] = {16384, -16384, -8192, 8192, 0, 32767, -32768, 0};
    for (int i = 0; i < 8; ++i)
        qToLittleEndian(values[i], iq.data() + i * static_cast<int>(sizeof(qint16)));
    sendPacket(socket, kPacketBaseband, iq);
}

} // namespace dsdr::test
