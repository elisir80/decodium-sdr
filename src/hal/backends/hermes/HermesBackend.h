// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend OpenHPSDR protocollo 1 (Hermes-Lite 2 e famiglia).
//
// Classe raw-IQ: la radio consegna campioni e nient'altro, tutto il resto lo
// fa il DSP client. È la stessa forma del ColibriNANO, con la rete al posto
// dell'USB e un protocollo aperto al posto di una libreria del costruttore.
//
// Copre l'Hermes-Lite 2 e le schede che parlano lo stesso protocollo — Hermes,
// Angelia, Orion, Metis — ma le capability dichiarate sono quelle
// dell'Hermes-Lite 2: è la radio su cui il backend è stato scritto, e
// dichiarare per le altre quello che non si è verificato sarebbe una promessa
// presa a prestito.
//
// Solo ricezione. Il protocollo prevede la trasmissione e il posto per i
// campioni c'è già nei pacchetti che mandiamo, ma finché non è provata su una
// radio vera resta `TxSupport::None`: meglio niente PTT che un PTT che manda
// in aria qualcosa che nessuno ha misurato (CONSTITUTION §7).
#pragma once

#include "hal/IRadioBackend.h"

#include <QHash>
#include <QHostAddress>
#include <QPointer>

#include <atomic>
#include <memory>

class QThread;

namespace dsdr::hal {
class RadioScout;
}

namespace dsdr::hal::hermes {

class HermesWorker;

class HermesBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit HermesBackend(QObject *parent = nullptr);
    ~HermesBackend() override;

    QString backendId() const override { return QStringLiteral("hermes"); }
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

    double setGainReduction(double db) override;
    double gainReduction() const override { return m_gainReductionDb; }

    SampleRing *iqStream(ChannelId channel = kInvalidChannel) const override;
    SampleRing *audioStream(ChannelId channel) const override;
    SampleRing *spectrumStream(PanId pan) const override;

    QVariant nativeCommand(const QString &command, const QVariantMap &args) override;

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    void pushGain();

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_discovering = false;

    DeviceDescriptor m_device;
    QHostAddress m_address;
    QString m_model;
    qint64 m_centerHz = 7'100'000;
    double m_sampleRate = 192000.0;

    /// Guadagno d'ingresso scelto dall'operatore, e quanto la guardia contro
    /// la saturazione ne sta togliendo. Sono due numeri e non uno: rimettere
    /// le mani sulla manopola azzera la riduzione, non la eredita.
    double m_operatorGainDb = 20.0;
    double m_gainReductionDb = 0.0;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;
    QThread *m_thread = nullptr;
    QPointer<HermesWorker> m_worker;
    RadioScout *m_scout = nullptr;
    std::atomic<bool> m_adcOverload{false};
    quint64 m_sequence = 0;
    quint64 m_lostPackets = 0;
};

} // namespace dsdr::hal::hermes

namespace dsdr::hal {
using hermes::HermesBackend;
}
