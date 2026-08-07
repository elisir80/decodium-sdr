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

    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void onDeviceOpened(const SoapyDeviceProfile &profile);

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_ptt = false;

    DeviceDescriptor m_device;
    SoapyDeviceProfile m_profile;
    qint64 m_centerHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;

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
