// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/EndpointProbe.h"
#include "hal/backends/nettcp/RtlTcpProtocol.h"
#include "hal/backends/nettcp/SpyServerProtocol.h"

#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cstring>

namespace dsdr::hal::nettcp {

namespace {

/// Quanto si concede a un rtl_tcp per salutare spontaneamente. Deve bastare a
/// una rete locale: chi usa un server lontano lo aggiunge esplicitamente, e la
/// connessione vera ha tempi propri.
constexpr int kSilenceWindowMs = 450;

/// Tempo totale, incluso il tentativo SpyServer.
constexpr int kOverallTimeoutMs = 1600;

} // namespace

EndpointProbe::EndpointProbe(const QString &host, quint16 port, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_port(port)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::readyRead, this, &EndpointProbe::onData);
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this] { finish(NetProtocol::None, 0); });

    // Alla connessione parte la finestra di silenzio: se entro quella non
    // arriva un saluto, non è un rtl_tcp e si prova SpyServer.
    connect(m_socket, &QTcpSocket::connected, this, [this] { m_silenceTimer->start(); });

    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setSingleShot(true);
    m_silenceTimer->setInterval(kSilenceWindowMs);
    connect(m_silenceTimer, &QTimer::timeout, this, &EndpointProbe::trySpyServerHandshake);

    m_overallTimeout = new QTimer(this);
    m_overallTimeout->setSingleShot(true);
    m_overallTimeout->setInterval(kOverallTimeoutMs);
    connect(m_overallTimeout, &QTimer::timeout, this, [this] { finish(NetProtocol::None, 0); });
}

void EndpointProbe::start()
{
    m_overallTimeout->start();
    m_socket->connectToHost(m_host, m_port);
}

void EndpointProbe::onData()
{
    m_buffer.append(m_socket->readAll());

    // ── rtl_tcp: saluto spontaneo ────────────────────────────────────────
    if (!m_spyServerAttempted) {
        if (m_buffer.size() < kGreetingSize)
            return;
        if (std::memcmp(m_buffer.constData(), "RTL0", 4) == 0) {
            m_silenceTimer->stop();
            finish(NetProtocol::RtlTcp, qFromBigEndian<quint32>(m_buffer.constData() + 4));
            return;
        }
        // Ha parlato per primo ma non è rtl_tcp: non è nemmeno un SpyServer,
        // che resterebbe zitto.
        m_silenceTimer->stop();
        finish(NetProtocol::None, 0);
        return;
    }

    // ── SpyServer: risposta all'handshake ────────────────────────────────
    if (m_buffer.size() < spyserver::kMessageHeaderSize)
        return;

    const char *data = m_buffer.constData();
    const quint32 messageType = qFromLittleEndian<quint32>(data + 4);
    const quint32 bodySize = qFromLittleEndian<quint32>(data + 16);

    if (messageType != static_cast<quint32>(spyserver::MessageType::DeviceInfo)) {
        // Qualunque altra cosa: non sappiamo interpretarla come radio.
        if (bodySize > (16u << 20))
            finish(NetProtocol::None, 0);
        return;
    }

    if (static_cast<quint32>(m_buffer.size()) < spyserver::kMessageHeaderSize + bodySize)
        return;   // il corpo non è ancora arrivato per intero

    const quint32 deviceType =
        qFromLittleEndian<quint32>(data + spyserver::kMessageHeaderSize);
    finish(NetProtocol::SpyServer, deviceType);
}

void EndpointProbe::trySpyServerHandshake()
{
    if (m_done || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        finish(NetProtocol::None, 0);
        return;
    }

    m_spyServerAttempted = true;
    m_buffer.clear();

    // Handshake: versione di protocollo e nome del client.
    const QByteArray name("DECODIUM SDR");
    QByteArray body;
    body.resize(4);
    qToLittleEndian<quint32>(spyserver::kProtocolVersion, body.data());
    body.append(name);

    QByteArray packet;
    packet.resize(8);
    qToLittleEndian<quint32>(static_cast<quint32>(spyserver::Command::Hello), packet.data());
    qToLittleEndian<quint32>(static_cast<quint32>(body.size()), packet.data() + 4);
    packet.append(body);

    m_socket->write(packet);
}

void EndpointProbe::finish(NetProtocol protocol, quint32 detail)
{
    if (m_done)
        return;
    m_done = true;

    m_silenceTimer->stop();
    m_overallTimeout->stop();
    m_socket->abort();

    emit probed(this, protocol, detail);
}

} // namespace dsdr::hal::nettcp
