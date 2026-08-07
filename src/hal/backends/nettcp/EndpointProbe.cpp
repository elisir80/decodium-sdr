// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/EndpointProbe.h"
#include "hal/backends/nettcp/RtlTcpProtocol.h"

#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cstring>

namespace dsdr::hal::nettcp {

namespace {
/// Quanto si attende il saluto prima di dichiarare l'endpoint assente. Deve
/// bastare a una connessione su rete locale, non a una intercontinentale: chi
/// usa un server lontano lo aggiunge esplicitamente e la connessione vera ha
/// tempi propri.
constexpr int kProbeTimeoutMs = 900;
} // namespace

EndpointProbe::EndpointProbe(const QString &host, quint16 port, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_port(port)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        m_buffer.append(m_socket->readAll());
        if (m_buffer.size() < kGreetingSize)
            return;

        if (std::memcmp(m_buffer.constData(), "RTL0", 4) == 0) {
            finish(true,
                   qFromBigEndian<quint32>(m_buffer.constData() + 4),
                   qFromBigEndian<quint32>(m_buffer.constData() + 8));
        } else {
            finish(false, 0, 0);
        }
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { finish(false, 0, 0); });

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kProbeTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this] { finish(false, 0, 0); });
}

void EndpointProbe::start()
{
    m_timeout->start();
    m_socket->connectToHost(m_host, m_port);
}

void EndpointProbe::finish(bool found, quint32 tunerType, quint32 gainStepCount)
{
    if (m_done)
        return;
    m_done = true;

    m_timeout->stop();
    m_socket->abort();
    emit probed(this, found, tunerType, gainStepCount);
}

} // namespace dsdr::hal::nettcp
