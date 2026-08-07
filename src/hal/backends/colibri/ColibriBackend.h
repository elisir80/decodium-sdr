// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — backend ColibriNANO.
//
// Ricevitore USB a campionamento diretto: ADC a 122,88 MHz, un DDC, IQ
// consegnato come coppie di float già normalizzate. Sola ricezione: non c'è
// trasmettitore, non c'è linea di manipolazione, e l'unica telemetria è un
// flag di sovraccarico dell'ADC che viaggia su ogni blocco.
//
// Classe raw-IQ: il device consegna campioni, tutto il resto lo fa il DSP
// Engine. Rispetto ad altri backend qui tutto **sottrae**: un solo ricevitore,
// un solo panadattatore, nessun TX.
//
// Provenienza: il porting parte dal backend ColibriNANO che IU8LMC ha scritto
// per AetherSDR (branch `colibri-nano-backend`), di cui è titolare. La
// specifica del protocollo è quella di `colibrinano_lib`; l'architettura qui è
// quella di DECODIUM SDR e il codice è riscritto su questo seam.
#pragma once

#include "hal/IRadioBackend.h"
#include "hal/backends/colibri/ColibriLibrary.h"

#include <QHash>
#include <QPointer>

#include <atomic>
#include <memory>

class QThread;

namespace dsdr::hal::colibri {

class ColibriBackend : public IRadioBackend
{
    Q_OBJECT

public:
    explicit ColibriBackend(QObject *parent = nullptr);
    ~ColibriBackend() override;

    QString backendId() const override { return QStringLiteral("colibri"); }
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

private:
    /// Chiamata dalla libreria sul **suo** thread. Scrive nel ring e basta:
    /// il ring è lock-free a produttore singolo, quindi non serve rimbalzare
    /// il blocco su un altro thread prima di consumarlo.
    static bool DSDR_COLIBRI_CALL rxTrampoline(ColibriComplex *iq,
                                               std::uint32_t length,
                                               bool adcOverload,
                                               void *user);
    void onSamples(ColibriComplex *iq, std::uint32_t length, bool adcOverload);

    void setState(BackendState state);
    void reportError(BackendError::Code code, const QString &message, bool fatal = false);
    bool startStream();
    void stopStream();

    BackendState m_state = BackendState::Idle;
    bool m_open = false;
    bool m_streaming = false;

    DeviceDescriptor m_device;
    ColibriDescriptor m_handle = nullptr;

    qint64 m_centerHz = 7'100'000;
    double m_sampleRate = 768000.0;
    float m_preampDb = 0.0f;

    QHash<ChannelId, RxChannelConfig> m_channels;
    QHash<PanId, PanConfig> m_panadapters;
    ChannelId m_nextChannelId = 1;
    PanId m_nextPanId = 1;

    std::unique_ptr<SampleRing> m_iqRing;

    /// Il segno della parte immaginaria dipende dalla convenzione del flusso:
    /// se le bande laterali risultano scambiate, si coniuga. Regolabile a
    /// runtime perché è una calibrazione, non una costante da compilare.
    std::atomic<bool> m_conjugate{false};

    std::atomic<quint64> m_sequence{0};
    std::atomic<quint64> m_overloadBlocks{0};
    std::atomic<bool> m_adcOverload{false};
};

} // namespace dsdr::hal::colibri

namespace dsdr::hal {
using colibri::ColibriBackend;
}
