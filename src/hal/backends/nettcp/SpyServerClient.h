// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — client SpyServer.
//
// Vive nel thread di ingest come il client rtl_tcp e scrive nello stesso tipo
// di ring: dal punto di vista del backend i due protocolli sono
// intercambiabili, ed è esattamente il motivo per cui `nettcp` è nato come
// backend e non come "backend rtl_tcp".
#pragma once

#include "dsp/DspTypes.h"
#include "hal/backends/nettcp/SpyServerProtocol.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

class QTcpSocket;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::nettcp {

class SpyServerClient : public QObject
{
    Q_OBJECT

public:
    explicit SpyServerClient(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~SpyServerClient() override;

public slots:
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    void setFrequency(qint64 hz);
    /// Il rate non è libero: si sceglie uno stadio di decimazione fra quelli
    /// che il server dichiara.
    void setDecimationStage(int stage);
    void setGainIndex(int index);

signals:
    void connected(const dsdr::hal::nettcp::spyserver::DeviceInfo &info);
    void disconnected();
    void failed(const QString &message, bool fatal);
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private slots:
    void onReadyRead();
    void onSocketError();
    void onDisconnected();

private:
    void sendHello();
    void sendSetting(spyserver::Setting setting, quint32 value);
    bool parseMessages();
    void handleDeviceInfo(const QByteArray &body);
    void handleIqData(const QByteArray &body, spyserver::MessageType type);
    void startStreaming();

    QTcpSocket *m_socket = nullptr;
    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;

    QByteArray m_pending;
    std::vector<float> m_scratch;

    spyserver::DeviceInfo m_deviceInfo;
    bool m_deviceInfoReceived = false;
    bool m_streaming = false;

    qint64 m_frequencyHz = 100'000'000;
    int m_decimationStage = 0;
    int m_gainIndex = 0;
};

} // namespace dsdr::hal::nettcp

Q_DECLARE_METATYPE(dsdr::hal::nettcp::spyserver::DeviceInfo)
