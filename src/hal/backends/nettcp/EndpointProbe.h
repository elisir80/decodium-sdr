// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — sonda per un endpoint di rete.
//
// Né rtl_tcp né SpyServer si annunciano sulla rete: l'unico modo di sapere se
// a un indirizzo risponde una radio è chiederglielo.
//
// I due protocolli si distinguono da come si comportano appena connessi:
//
//   rtl_tcp   manda 12 byte di saluto ("RTL0" + tuner) senza che nessuno
//             glielo chieda;
//   SpyServer resta in silenzio finché il client non si presenta.
//
// La sonda sfrutta proprio questa differenza: aspetta, e solo se non arriva
// nulla manda l'handshake SpyServer. L'ordine non è invertibile — mandare
// byte a un rtl_tcp prima del saluto significherebbe spedirgli comandi a caso,
// cambiandogli frequenza durante una semplice ricerca.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace dsdr::hal::nettcp {

/// Protocollo riconosciuto su un endpoint.
enum class NetProtocol {
    None,
    RtlTcp,
    SpyServer,
};

class EndpointProbe : public QObject
{
    Q_OBJECT

public:
    EndpointProbe(const QString &host, quint16 port, QObject *parent = nullptr);

    void start();

    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

signals:
    /// `detail` porta il tipo di tuner (rtl_tcp) o di device (SpyServer).
    void probed(dsdr::hal::nettcp::EndpointProbe *probe,
                dsdr::hal::nettcp::NetProtocol protocol,
                quint32 detail);

private:
    void onData();
    void trySpyServerHandshake();
    void finish(NetProtocol protocol, quint32 detail);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_silenceTimer = nullptr;   ///< attesa del saluto rtl_tcp
    QTimer *m_overallTimeout = nullptr;
    QByteArray m_buffer;
    QString m_host;
    quint16 m_port = 0;
    bool m_spyServerAttempted = false;
    bool m_done = false;
};

} // namespace dsdr::hal::nettcp
