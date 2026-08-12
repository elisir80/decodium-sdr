// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend FlexRadio serie 6000 (RF-04).
//
// Mette insieme le due metà che c'erano già: il canale di comando su TCP 4992
// e il decodificatore VITA-49. In mezzo c'è la sequenza che apre il flusso DAX
// IQ, che è la parte che nessuno di noi ha potuto provare su una radio vera.
//
// **Come si comporta un backend scritto senza avere l'apparato.**
//
// La regola del progetto è che un backend che si collega e non consegna niente
// è la peggiore delle promesse (CONSTITUTION §7). Qui la si rispetta in un
// modo diverso dal tenerlo spento: il backend **si accorge di non ricevere e
// lo dice**, con il comando esatto su cui si è fermato e il codice che la
// radio ha risposto. Non resta zitto, non finge, e non lascia l'operatore a
// guardare una traccia piatta chiedendosi se sia la banda o il programma.
//
// Le fonti pubbliche descrivono **due forme** del comando che crea il flusso:
// una con `daxiq=<canale>` e una con `type=iq`. Non si sceglie a memoria — si
// prova la prima, e se la radio la rifiuta si prova la seconda. Il firmware
// che risponde è l'unico documento che conti.
//
// Chi ha un Flex davanti e trova qualcosa che non torna ha nel log tutto ciò
// che serve a dirlo: ogni comando mandato e ogni risposta ricevuta.
#pragma once

#include "hal/IRadioBackend.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>

#include <memory>

class QTimer;
class QUdpSocket;

namespace dsdr::hal::flex {

class FlexClient;

class FlexBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit FlexBackend(QObject *parent = nullptr);
    ~FlexBackend() override;

    QString backendId() const override { return QStringLiteral("flex"); }
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

    /// I passi della sequenza che apre il flusso, per il diario.
    ///
    /// Serve a una cosa sola, e vale la pena averla: quando su una radio vera
    /// qualcosa non torna, il primo passo che non ha risposto `0` dice dove
    /// guardare. Senza, resterebbe «non arrivano campioni», che non è una
    /// diagnosi.
    enum class Step {
        Idle,
        UdpPort,        ///< `client udpport`
        CreateStream,   ///< `stream create daxiq=…` (o la forma alternativa)
        CreatePan,      ///< `display pan create`
        BindStream,     ///< `dax iq s … daxiq_rate=…`
        Streaming,      ///< la sequenza è passata: si aspettano i pacchetti
    };
    Q_ENUM(Step)

private slots:
    void onCommandConnected();
    void onCommandFailed(const QString &reason);
    void onResponse(quint32 sequence, quint32 code, const QString &payload);
    void readDatagrams();
    void checkForSilence();

private:
    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);

    /// Manda il passo successivo della sequenza e annota il numero d'ordine
    /// con cui tornerà la risposta.
    void advance(Step step);
    void sendStep(Step step);

    /// Il nome del passo, per i messaggi. In italiano perché finisce sotto gli
    /// occhi di chi opera, non solo nel log.
    static QString stepName(Step step);

    /// L'indirizzo con cui questa macchina raggiunge la radio.
    ///
    /// Non `localhost` e non il primo della lista: la radio deve mandare i
    /// campioni sulla scheda da cui le stiamo parlando, e su una macchina con
    /// più reti — una cablata verso la radio, una senza fili verso casa — la
    /// scelta sbagliata produce un flusso che parte e non arriva.
    QString localAddressFor(const QHostAddress &radio) const;

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_ptt = false;
    bool m_discovering = false;

    DeviceDescriptor m_device;
    QHostAddress m_radioAddress;
    qint64 m_centerHz = 14'100'000;
    double m_sampleRate = 192'000.0;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<FlexClient> m_client;
    std::unique_ptr<QUdpSocket> m_udp;
    std::unique_ptr<SampleRing> m_iqRing;

    Step m_step = Step::Idle;
    quint32 m_pendingSequence = 0;

    /// Se la prima forma del comando di creazione è stata rifiutata. La
    /// seconda si prova una volta sola: rifiutate entrambe, il problema non è
    /// la forma.
    bool m_triedAlternateForm = false;

    QString m_panStreamId;
    quint16 m_udpPort = 0;
    int m_daxChannel = 1;

    /// Quanti pacchetti sono arrivati, e da quando si aspetta.
    quint64 m_packets = 0;
    quint64 m_frames = 0;
    quint64 m_sequence = 0;
    QElapsedTimer m_sinceBind;
    QTimer *m_silenceTimer = nullptr;
    bool m_silenceReported = false;

    /// Il contatore a quattro bit dei pacchetti VITA: serve a vedere i buchi.
    /// Negativo finché non è arrivato il primo.
    int m_lastPacketCount = -1;
    quint64 m_gaps = 0;

    std::vector<float> m_decoded;
};

} // namespace dsdr::hal::flex

namespace dsdr::hal {
using flex::FlexBackend;
}
