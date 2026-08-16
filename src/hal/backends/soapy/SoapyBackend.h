// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend SoapySDR (RF-01).
//
// «SoapySDR è il moltiplicatore di universalità: un solo backend copre decine
// di hardware. Ogni device esotico che ha un driver Soapy funziona gratis.»
// (spec §4.3)
//
// Classe raw-IQ. Le capability non sono costanti come negli altri backend: si
// leggono dal driver all'apertura, perché una chiavetta da 30 € e un USRP
// passano entrambi da qui.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/rtlsdr/RtlSdrTuningPlan.h"
#include "hal/backends/soapy/SoapyProfile.h"

#include <QHash>
#include <QPointer>

#include <memory>

class QThread;

namespace dsdr::hal::soapy {

class SoapyWorker;

class SoapyBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit SoapyBackend(QObject *parent = nullptr);
    ~SoapyBackend() override;

    QString backendId() const override { return QStringLiteral("soapy"); }
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
    bool ptt() const override { return m_ptt; }
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
    void onDeviceOpened(const SoapyDeviceProfile &profile);
    bool isRtlSdr() const;
    QString deviceIdentity() const;
    bool autoIfUsesLsb() const;
    rtlsdr::TuningPlan tuningPlanFor(qint64 dialFrequencyHz) const;
    bool hardwarePlanIsSupported(const rtlsdr::TuningPlan &plan) const;
    bool applyTuningPlan(qint64 dialFrequencyHz, bool notifyCenter = true);
    qint64 ifReferenceFrequency(qint64 fallbackFrequencyHz) const;
    QVariantMap directSamplingInfo() const;
    QVariantMap ifSettings() const;

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_ptt = false;

    DeviceDescriptor m_device;
    SoapyDeviceProfile m_profile;
    qint64 m_centerHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;
    double m_gainDb = -1.0;
    double m_autoGainDb = 0.0;
    double m_gainReductionDb = 0.0;
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

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    QThread *m_thread = nullptr;
    QPointer<SoapyWorker> m_worker;
    quint64 m_sequence = 0;
};

} // namespace dsdr::hal::soapy

namespace dsdr::hal {
using soapy::SoapyBackend;
}
