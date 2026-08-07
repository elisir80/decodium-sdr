// SPDX-License-Identifier: GPL-3.0-or-later
// Server rtl_tcp finto, per far girare i test senza una chiavetta.
//
// Riproduce il protocollo dal lato server: saluto, accettazione dei comandi e
// flusso continuo di campioni a 8 bit. Genera un tono a offset noto, così un
// test può verificare non solo che i byte arrivino, ma che arrivino *giusti*.
#pragma once

#include <QByteArray>
#include <QObject>

#include <atomic>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace dsdr::test {

class RtlTcpMockServer : public QObject
{
    Q_OBJECT

public:
    explicit RtlTcpMockServer(QObject *parent = nullptr);
    ~RtlTcpMockServer() override;

    /// Ascolta su una porta effimera di loopback. Restituisce false se il
    /// sistema non concede la porta.
    bool listen();
    void stop();

    quint16 port() const;
    QString endpoint() const;
    bool hasClient() const { return m_client != nullptr; }

    /// Tipo di tuner dichiarato nel saluto (default R820T).
    void setTunerType(quint32 type) { m_tunerType = type; }

    /// Offset del tono generato rispetto al centro della banda.
    void setToneOffsetHz(double hz) { m_toneOffsetHz = hz; }

    /// Se falso, il server accetta la connessione ma non manda il saluto:
    /// serve a verificare che il client non scambi un servizio qualsiasi per
    /// un rtl_tcp.
    void setSendGreeting(bool send) { m_sendGreeting = send; }

    // Comandi osservati, per verificare che il client li mandi davvero.
    qint64 lastFrequencyHz() const { return m_lastFrequency; }
    qint64 lastSampleRate() const { return m_lastSampleRate; }
    int lastGainMode() const { return m_lastGainMode; }
    int commandCount() const { return m_commandCount; }
    quint64 bytesSent() const { return m_bytesSent; }

signals:
    void clientConnected();
    void commandReceived(quint8 command, qint32 value);

private:
    void onNewConnection();
    void onClientData();
    void sendBlock();

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_incoming;

    quint32 m_tunerType = 5;      ///< R820T
    double m_sampleRate = 2'048'000.0;
    double m_toneOffsetHz = 100'000.0;
    double m_phase = 0.0;
    bool m_sendGreeting = true;

    std::atomic<qint64> m_lastFrequency{0};
    std::atomic<qint64> m_lastSampleRate{0};
    std::atomic<int> m_lastGainMode{-1};
    std::atomic<int> m_commandCount{0};
    std::atomic<quint64> m_bytesSent{0};
};

} // namespace dsdr::test
