// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend `audiorig` (DSDR-SPEC-004).
//
// Una radio tradizionale non ha uscita IF, ma non per questo è fuori
// dall'architettura: è un backend di classe **server-DSP**, come un Flex o un
// Kiwi. La radio demodula, il client riceve audio e comanda via protocollo —
// solo che il protocollo è il CAT e il transport dei dati è la scheda audio.
// Nessuna classe nuova nel seam, una riga nuova nella matrice.
//
// Due piani, come dice la specifica:
//
//   dati       device audio d'ingresso → ring audio del seam
//   controllo  ICatDriver su thread proprio: frequenza, modo, PTT, S-meter
//
// Radio di riferimento: Yaesu FT-991A, un solo cavo USB per entrambi.
//
// L'audio che consegniamo torna banda base nel DspEngine, che ne ricostruisce
// il segnale analitico: da lì in poi è un flusso come tutti gli altri, e
// l'intera catena della SPEC-003 si applica senza saperne nulla.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/audiorig/ICatDriver.h"

#include <QHash>
#include <QPointer>

#include <atomic>
#include <limits>
#include <memory>

class QThread;
class QTimer;

namespace dsdr::audio {
class AudioOut;
class MicSource;
}

namespace dsdr::hal::audiorig {

class CatController;

class AudiorigBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit AudiorigBackend(QObject *parent = nullptr);
    ~AudiorigBackend() override;

    QString backendId() const override { return QStringLiteral("audiorig"); }
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
    SampleRing *txStream() override;

    SampleRing *iqStream(ChannelId channel = kInvalidChannel) const override;
    SampleRing *audioStream(ChannelId channel) const override;
    SampleRing *spectrumStream(PanId pan) const override;

    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

private slots:
    void onCatState(qint64 frequencyHz, int mode, bool transmitting, int sMeterRaw,
                    double signalDbm);
    void onCatLost(const QString &reason);

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void publishAudio();

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_ptt = false;
    bool m_discovering = false;

    DeviceDescriptor m_device;
    QString m_radioModel;
    qint64 m_centerHz = 0;
    double m_sampleRate = 48000.0;
    DemodMode m_radioMode = DemodMode::Usb;
    int m_sMeterRaw = -1;

    /// Il livello tarato, quando il CAT sa darlo. NaN altrimenti: allora si
    /// ricade sulla lettura grezza, che è quello che una radio manda sulla
    /// seriale e che senza il profilo del modello vale a occhio.
    double m_signalDbm = std::numeric_limits<double>::quiet_NaN();

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<audio::MicSource> m_capture;

    /// L'uscita verso il codec della radio. Il ring TX del seam **è** il suo:
    /// tenerne due e copiare fra l'uno e l'altro aggiungerebbe latenza al
    /// percorso più sensibile che ci sia, quello fra il PTT e l'antenna.
    std::unique_ptr<audio::AudioOut> m_playback;
    QThread *m_catThread = nullptr;
    QPointer<CatController> m_cat;
    QTimer *m_publishTimer = nullptr;

    /// La sonda delle porte seriali vive su un thread suo e può durare
    /// secondi. Va aspettata prima che il backend muoia: un thread che tiene
    /// un puntatore a un oggetto distrutto non sbaglia subito — sbaglia
    /// quando gli capita, e nella conformance suite capitava dentro il test
    /// di un altro backend.
    QPointer<QThread> m_prober;
    std::atomic<bool> m_abortDiscovery{false};
    quint64 m_sequence = 0;
    quint64 m_publishedFrames = 0;
};

} // namespace dsdr::hal::audiorig

namespace dsdr::hal {
using audiorig::AudiorigBackend;
}
