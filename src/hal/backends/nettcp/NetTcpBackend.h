// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend per sorgenti IQ di rete (RF-07).
//
// Classe raw-IQ. Oggi parla rtl_tcp, che è ciò che rende utilizzabile una
// chiavetta da 30 € — anche remota, anche su un Raspberry in giardino.
// SpyServer è previsto dalla stessa spec ma non ancora implementato: quando
// arriverà entrerà come secondo protocollo dietro la stessa facciata, senza
// che il core se ne accorga.
//
// Non esiste discovery broadcast per rtl_tcp: si sondano endpoint noti.
// Sorgenti degli endpoint, in ordine:
//   1. la variabile d'ambiente DSDR_NETTCP_HOSTS ("host:porta,host:porta")
//   2. gli host aggiunti a runtime con nativeCommand("nettcp.addHost")
//   3. localhost:1234, il default di rtl_tcp
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/nettcp/EndpointProbe.h"
#include "hal/backends/nettcp/SpyServerProtocol.h"

#include <QHash>
#include <QPointer>
#include <QStringList>

#include <memory>

class QThread;

namespace dsdr::hal::nettcp {

class RtlTcpClient;
class SpyServerClient;

class NetTcpBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit NetTcpBackend(QObject *parent = nullptr);
    ~NetTcpBackend() override;

    QString backendId() const override { return QStringLiteral("nettcp"); }
    QString displayName() const override;
    BackendCapabilities capabilities() const override;
    BackendState state() const override { return m_state; }

    void startDiscovery() override;
    void stopDiscovery() override;
    void open(const DeviceDescriptor &device) override;
    void close() override;
    bool isOpen() const override { return m_open; }
    DeviceDescriptor currentDevice() const override { return m_device; }

    void setCenterFrequency(qint64 hz) override;
    qint64 centerFrequency() const override { return m_centerHz; }
    void setSampleRate(double rate) override;
    double sampleRate() const override { return m_sampleRate; }

    ChannelId createRxChannel(const RxChannelConfig &config) override;
    void destroyRxChannel(ChannelId channel) override;
    QList<ChannelId> channels() const override;

    void setFrequency(ChannelId channel, qint64 hz) override;
    void setDemod(ChannelId channel, DemodMode mode) override;
    void setFilter(ChannelId channel, int lowHz, int highHz) override;

    PanId createPanadapter(const PanConfig &config) override;
    void destroyPanadapter(PanId pan) override;

    void setPtt(bool transmit) override;
    bool ptt() const override { return false; }
    void setTxFrequency(qint64 hz) override;

    SampleRing *iqStream(ChannelId channel = kInvalidChannel) const override;
    SampleRing *audioStream(ChannelId channel) const override;
    SampleRing *spectrumStream(PanId pan) const override;

    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

    /// Endpoint da sondare. Esposta per i test, che vi registrano un mock.
    static QStringList configuredEndpoints();
    static void addEndpoint(const QString &hostPort);
    static void clearAddedEndpoints();

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void onClientConnected(quint32 tunerType, quint32 gainStepCount);
    void onSpyServerConnected(const spyserver::DeviceInfo &info);
    void openRtlTcp(const QString &host, quint16 port);
    void openSpyServer(const QString &host, quint16 port);

    /// Frequenze di campionamento offerte da un SpyServer: il rate non è
    /// libero, si sceglie uno stadio di decimazione fra quelli dichiarati.
    QList<double> spyServerSampleRates() const;

    BackendState m_state = BackendState::Idle;
    bool m_open = false;

    DeviceDescriptor m_device;
    qint64 m_centerHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;
    int m_gainTenthsDb = -1;   ///< negativo = guadagno automatico
    int m_ppm = 0;
    quint32 m_tunerType = 0;
    quint32 m_gainStepCount = 0;

    NetProtocol m_protocol = NetProtocol::RtlTcp;
    spyserver::DeviceInfo m_spyDeviceInfo;
    bool m_spyDeviceInfoValid = false;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    QThread *m_thread = nullptr;
    // Un puntatore per protocollo invece di una classe base: i due client
    // condividono il ring e il ciclo di vita, non l'interfaccia.
    QPointer<RtlTcpClient> m_client;
    QPointer<SpyServerClient> m_spyClient;
    QList<EndpointProbe *> m_probes;
    int m_pendingProbes = 0;
    quint64 m_sequence = 0;
};

} // namespace dsdr::hal::nettcp

namespace dsdr::hal {
using nettcp::NetTcpBackend;
}
