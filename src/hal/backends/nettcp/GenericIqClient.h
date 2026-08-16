// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ricevitore IQ interlacciato grezzo su TCP o UDP.
#pragma once

#include "hal/backends/nettcp/NetworkEndpoint.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

class QTcpSocket;
class QUdpSocket;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::nettcp {

class GenericIqClient final : public QObject
{
    Q_OBJECT

public:
    explicit GenericIqClient(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~GenericIqClient() override;

public slots:
    /// TCP e' un client; UDP apre la porta locale e accetta datagrammi da
    /// qualunque mittente. In entrambi i casi i campioni sono IQ interlacciati
    /// little-endian, come il Network Source di SDR++ sulle piattaforme comuni.
    void connectToSource(const QString &host, quint16 port, bool udp, int sampleFormat);
    void disconnectFromSource();

signals:
    void connected();
    void disconnected();
    void failed(const QString &message, bool fatal);
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private slots:
    void onTcpReadyRead();
    void onUdpReadyRead();
    void onSocketError();
    void onDisconnected();

private:
    void appendBytes(const QByteArray &bytes);
    void processPending();

    QTcpSocket *m_tcp = nullptr;
    QUdpSocket *m_udp = nullptr;
    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;
    QByteArray m_pending;
    std::vector<float> m_scratch;
    IqSampleFormat m_format = IqSampleFormat::Int16;
};

} // namespace dsdr::hal::nettcp
