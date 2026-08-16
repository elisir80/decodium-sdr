// SPDX-License-Identifier: GPL-3.0-or-later
// RTL-SDR sull'uscita IF di una radio, con il CAT come VFO autorevole.
#pragma once

#include "hal/backends/rtlsdr/RtlSdrBackend.h"

#include <QPointer>
#include <QVariantMap>

class QThread;

namespace dsdr::hal::audiorig {
class CatController;
}

namespace dsdr::hal::rtlcat {

class RtlSdrCatBackend final : public rtlsdr::RtlSdrBackend
{
    Q_OBJECT

public:
    explicit RtlSdrCatBackend(QObject *parent = nullptr);
    ~RtlSdrCatBackend() override;

    QString backendId() const override { return QStringLiteral("rtlcat"); }
    QString displayName() const override;
    BackendCapabilities capabilities() const override;

    void open(const DeviceDescriptor &device) override;
    void close() override;

    void setCenterFrequency(qint64 hz) override;
    ChannelId createRxChannel(const RxChannelConfig &config) override;
    void setFrequency(ChannelId channel, qint64 hz) override;
    void setDemod(ChannelId channel, DemodMode mode) override;
    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

private slots:
    void onCatState(qint64 frequencyHz, int mode, bool transmitting, bool pttKnown,
                    int sMeterRaw, double signalDbm);
    void onCatLost(const QString &reason);

private:
    void startCat();
    void stopCat();
    void requestRadioFrequency(qint64 hz);
    void requestRadioMode(DemodMode mode);
    QVariantList serialPorts() const;
    static QVariantList catDrivers();

    QVariantMap m_catProfile;
    QPointer<QThread> m_catThread;
    QPointer<audiorig::CatController> m_cat;
    QString m_radioModel;
    qint64 m_lastRequestedFrequency = 0;
    DemodMode m_radioMode = DemodMode::Usb;
    bool m_catStateKnown = false;
    bool m_radioTransmitting = false;
    bool m_pttUnavailableReported = false;
};

} // namespace dsdr::hal::rtlcat

namespace dsdr::hal {
using rtlcat::RtlSdrCatBackend;
}
