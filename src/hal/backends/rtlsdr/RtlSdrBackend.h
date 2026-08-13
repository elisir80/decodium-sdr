// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend RTL-SDR nativo, in stile SDR++.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/rtlsdr/RtlSdrProfile.h"

#include <QHash>
#include <QPointer>

#include <memory>

class QThread;

namespace dsdr::hal::rtlsdr {

class RtlSdrWorker;

class RtlSdrBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit RtlSdrBackend(QObject *parent = nullptr);
    ~RtlSdrBackend() override;

    QString backendId() const override { return QStringLiteral("rtlsdr"); }
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
    double setGainReduction(double db) override;
    double gainReduction() const override { return m_gainReductionDb; }
    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void onDeviceOpened(const RtlSdrDeviceProfile &profile);

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    DeviceDescriptor m_device;
    RtlSdrDeviceProfile m_profile;
    qint64 m_centerHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;
    double m_gainDb = -1.0;
    double m_autoGainDb = 19.8;
    double m_gainReductionDb = 0.0;
    int m_ppm = 0;
    bool m_biasTee = false;
    int m_directSampling = 0;
    bool m_offsetTuning = false;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    QThread *m_thread = nullptr;
    QPointer<RtlSdrWorker> m_worker;
    quint64 m_sequence = 0;
};

} // namespace dsdr::hal::rtlsdr

namespace dsdr::hal {
using rtlsdr::RtlSdrBackend;
}
