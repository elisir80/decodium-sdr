// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend sintetico (RF-09).
//
// Classe raw-IQ: consegna solo campioni, tutto il resto lo fa il DSP Engine.
// È il backend di riferimento della conformance suite e l'unico che la
// CONSTITUTION §9 rende bloccante: se una PR lo rompe, la PR è rotta.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/demo/SyntheticBand.h"

#include <QHash>
#include <QPointer>

#include <memory>

class QThread;

namespace dsdr::hal::demo {

class DemoWorker;

class DemoBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit DemoBackend(QObject *parent = nullptr);
    ~DemoBackend() override;

    QString backendId() const override { return QStringLiteral("demo"); }
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

    double setGainReduction(double db) override;
    double gainReduction() const override { return m_gainReductionDb; }

    void setPtt(bool transmit) override;
    bool ptt() const override { return m_ptt; }
    void setTxFrequency(qint64 hz) override;
    SampleRing *txStream() override;

    SampleRing *iqStream(ChannelId channel = kInvalidChannel) const override;
    SampleRing *audioStream(ChannelId channel) const override;
    SampleRing *spectrumStream(PanId pan) const override;

    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

    /// Elenco dei device sintetici offerti dalla discovery. Esposto perché la
    /// conformance suite possa aprirli senza passare dai signal.
    static QList<DeviceDescriptor> syntheticDevices();

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    std::vector<StationSpec> bandPlanFor(const DeviceDescriptor &device) const;
    void restartWorker();

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_ptt = false;
    bool m_discovering = false;

    DeviceDescriptor m_device;
    qint64 m_centerHz = 7'100'000;
    double m_gainReductionDb = 0.0;
    qint64 m_txFrequencyHz = 7'100'000;
    double m_sampleRate = 192000.0;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    std::unique_ptr<SampleRing> m_txRing;
    QThread *m_thread = nullptr;
    QPointer<DemoWorker> m_worker;
    quint64 m_sequence = 0;
};

} // namespace dsdr::hal::demo

namespace dsdr::hal {
using demo::DemoBackend;
}
