// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — riproduzione di registrazioni IQ (RF-17, seconda metà).
//
// Chiude il cerchio del registratore: quello che l'applicazione scrive, la
// stessa applicazione lo riascolta. Dietro il seam una registrazione è una
// radio come un'altra — sintonizzabile, demodulabile, con il suo waterfall —
// con l'unica differenza che il tempo si può fermare e riavvolgere.
//
// La discovery elenca le registrazioni trovate nella cartella predefinita;
// `DSDR_IQFILE_PATH` aggiunge file o cartelle, ed è anche il modo in cui i
// test puntano il backend su una registrazione appena creata.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/iqfile/IqFileReader.h"

#include <QHash>
#include <QPointer>

#include <memory>

class QThread;

namespace dsdr::hal::iqfile {

class IqFileWorker;

class IqFileBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit IqFileBackend(QObject *parent = nullptr);
    ~IqFileBackend() override;

    QString backendId() const override { return QStringLiteral("iqfile"); }
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

    /// Registrazioni visibili al backend, nell'ordine in cui la discovery le
    /// annuncerebbe. Esposto perché i test possano aprirle senza aspettare i
    /// signal.
    static QList<DeviceDescriptor> availableRecordings();

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void startWorker(const QString &path);
    void stopWorker();
    static DeviceDescriptor describe(const RecordingInfo &info);

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_discovering = false;

    DeviceDescriptor m_device;
    RecordingInfo m_recording;
    qint64 m_centerHz = 0;
    double m_sampleRate = 0.0;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    QThread *m_thread = nullptr;
    QPointer<IqFileWorker> m_worker;
    quint64 m_sequence = 0;

    // Stato del trasporto, tenuto qui perché il pannello lo interroga dal
    // thread GUI mentre il worker vive altrove.
    bool m_paused = false;
    bool m_loop = true;
    double m_speed = 1.0;
    qint64 m_positionMs = 0;
};

} // namespace dsdr::hal::iqfile

namespace dsdr::hal {
using iqfile::IqFileBackend;
}
