// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>

class QTcpServer;
class QTcpSocket;

namespace dsdr::test {

class RawIqTcpMockServer final : public QObject
{
public:
    explicit RawIqTcpMockServer(QObject *parent = nullptr);
    bool listen();
    quint16 port() const;
    QString endpoint() const;

private:
    QTcpServer *m_server = nullptr;
};

class SdrppServerMockServer final : public QObject
{
public:
    explicit SdrppServerMockServer(QObject *parent = nullptr);
    bool listen();
    quint16 port() const;
    QString endpoint() const;
    bool receivedStart() const { return m_receivedStart; }
    qint64 requestedFrequencyHz() const { return m_requestedFrequencyHz; }

private:
    void handlePacket(QTcpSocket *socket, quint32 type, const QByteArray &body);
    void sendPacket(QTcpSocket *socket, quint32 type, const QByteArray &body);
    void sendCommand(QTcpSocket *socket, quint32 command, const QByteArray &body = {});
    void sendIq(QTcpSocket *socket);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_pending;
    bool m_receivedStart = false;
    qint64 m_requestedFrequencyHz = 0;
};

} // namespace dsdr::test
