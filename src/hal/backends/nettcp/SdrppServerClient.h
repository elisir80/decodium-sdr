// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — client del protocollo SDR++ Server, senza dipendenze extra.
#pragma once

#include "hal/backends/nettcp/NetworkEndpoint.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>

#include <vector>

class QTcpSocket;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::nettcp {

class SdrppServerClient final : public QObject
{
    Q_OBJECT

public:
    explicit SdrppServerClient(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~SdrppServerClient() override;

public slots:
    void connectToServer(const QString &host, quint16 port, qint64 frequencyHz, int sampleFormat);
    void disconnectFromServer();
    void setFrequency(qint64 frequencyHz);

signals:
    void connected(double sampleRate);
    void disconnected();
    void failed(const QString &message, bool fatal);
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();

private:
    void sendCommand(quint32 command, const QByteArray &body = {});
    void parsePackets();
    void processPacket(quint32 type, const QByteArray &body);
    void processBaseband(const QByteArray &body);
    void failProtocol(const QString &message);

    QTcpSocket *m_socket = nullptr;
    dsp::SpscRing<float> *m_ring = nullptr;
    QByteArray m_pending;
    std::vector<float> m_scratch;
    QElapsedTimer m_clock;
    IqSampleFormat m_format = IqSampleFormat::Int16;
    bool m_sampleRateKnown = false;
};

} // namespace dsdr::hal::nettcp
