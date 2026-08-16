// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend RTL-SDR nativo, in stile SDR++.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/rtlsdr/RtlSdrTuningPlan.h"
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

protected:
    /// Gate software usato dai backend composti che ricevono l'IF di una
    /// radio. Mantiene il tuner aperto ma ferma immediatamente read_async e
    /// svuota il ring; non pretende di sostituire una protezione RF fisica.
    void setIqInputBlocked(bool blocked);
    bool iqInputBlocked() const { return m_iqInputBlocked; }

    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);

private:
    void onDeviceOpened(const RtlSdrDeviceProfile &profile);
    QString deviceIdentity() const;
    TuningPlan tuningPlanFor(qint64 dialFrequencyHz) const;
    bool hardwarePlanIsSupported(const TuningPlan &plan) const;
    bool applyTuningPlan(qint64 dialFrequencyHz, bool notifyCenter = true);
    bool autoIfUsesLsb() const;
    qint64 ifReferenceFrequency(qint64 fallbackFrequencyHz) const;
    QVariantMap directSamplingInfo() const;
    QVariantMap ifSettings() const;

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    DeviceDescriptor m_device;
    RtlSdrDeviceProfile m_profile;
    qint64 m_centerHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;
    double m_gainDb = -1.0;
    double m_autoGainDb = 22.0;
    double m_gainReductionDb = 0.0;
    int m_ppm = 0;
    bool m_biasTee = false;
    int m_directSampling = 0;
    bool m_offsetTuning = false;
    qint64 m_hardwareCenterHz = 100'000'000;
    double m_appliedBasebandTranslationHz = 0.0;
    bool m_appliedSpectrumInverted = false;
    bool m_ifEnabled = false;
    qint64 m_ifFrequencyHz = 8'830'000;
    qint64 m_ifUsbShiftHz = 1'500;
    qint64 m_ifLsbShiftHz = -1'500;
    int m_ifSideband = 0; // 0 automatico, 1 USB, 2 LSB
    bool m_ifSpectrumInverted = false;
    DemodMode m_activeDemod = DemodMode::Usb;
    bool m_iqInputBlocked = false;

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
