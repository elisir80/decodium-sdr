// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — sonda per un singolo endpoint rtl_tcp.
//
// rtl_tcp non ha discovery: non annuncia nulla sulla rete. L'unico modo di
// sapere se a un indirizzo risponde una radio è chiederglielo.
//
// Connettersi non basta come prova: qualunque servizio in ascolto accetta la
// connessione. Solo il saluto "RTL0" dimostra che dall'altra parte c'è
// davvero un rtl_tcp, ed è ciò che questa classe attende.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace dsdr::hal::nettcp {

class EndpointProbe : public QObject
{
    Q_OBJECT

public:
    EndpointProbe(const QString &host, quint16 port, QObject *parent = nullptr);

    void start();

    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

signals:
    void probed(dsdr::hal::nettcp::EndpointProbe *probe,
                bool found,
                quint32 tunerType,
                quint32 gainStepCount);

private:
    void finish(bool found, quint32 tunerType, quint32 gainStepCount);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QByteArray m_buffer;
    QString m_host;
    quint16 m_port = 0;
    bool m_done = false;
};

} // namespace dsdr::hal::nettcp
