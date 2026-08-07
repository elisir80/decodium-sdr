// SPDX-License-Identifier: GPL-3.0-or-later
// Server SpyServer finto.
//
// A differenza del mock rtl_tcp, questo **non parla per primo**: resta in
// silenzio finché il client non si presenta. È esattamente il comportamento
// su cui si regge il riconoscimento automatico del protocollo, quindi il mock
// deve rispettarlo per essere una prova utile.
#pragma once

#include <QByteArray>
#include <QObject>

#include <atomic>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace dsdr::test {

class SpyServerMockServer : public QObject
{
    Q_OBJECT

public:
    explicit SpyServerMockServer(QObject *parent = nullptr);
    ~SpyServerMockServer() override;

    bool listen();
    void stop();

    quint16 port() const;
    QString endpoint() const;

    void setDeviceType(quint32 type) { m_deviceType = type; }
    void setMaximumSampleRate(quint32 rate) { m_maximumSampleRate = rate; }

    bool helloReceived() const { return m_helloReceived; }
    bool streamingEnabled() const { return m_streaming; }
    qint64 lastFrequencyHz() const { return m_lastFrequency; }
    int settingCount() const { return m_settingCount; }

private:
    void onNewConnection();
    void onClientData();
    void sendDeviceInfo();
    void sendIqBlock();

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_incoming;

    quint32 m_deviceType = 1;              ///< Airspy One
    quint32 m_maximumSampleRate = 10'000'000;
    quint32 m_sequence = 0;
    double m_phase = 0.0;

    std::atomic<bool> m_helloReceived{false};
    std::atomic<bool> m_streaming{false};
    std::atomic<qint64> m_lastFrequency{0};
    std::atomic<int> m_settingCount{0};
};

} // namespace dsdr::test
