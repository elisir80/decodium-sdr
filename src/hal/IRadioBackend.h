// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il seam (§4.1).
//
// Nessun modulo sopra la HAL conosce il tipo concreto di radio. La UI e il
// core parlano solo con questa interfaccia e con BackendCapabilities.
// Aggiungere una radio nuova non tocca mai la UI.
#pragma once

#include "dsp/SpscRing.h"
#include "hal/BackendCapabilities.h"
#include "hal/DeviceDescriptor.h"
#include "hal/Frames.h"

#include <QObject>
#include <QVariantMap>

namespace dsdr::hal {

/// Ring di campioni. I campioni IQ sono interleaved I,Q; l'audio è mono o
/// interleaved per canale, come dichiarato in AudioFrame::channelCount.
using SampleRing = dsp::SpscRing<float>;

class IRadioBackend : public QObject
{
    Q_OBJECT

public:
    explicit IRadioBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IRadioBackend() override = default;

    // ── Identità e capacità ──────────────────────────────────────────────
    // Valide dopo la connessione; parziali (ma coerenti) durante la discovery.

    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual BackendState state() const = 0;

    // ── Ciclo di vita ────────────────────────────────────────────────────

    virtual void startDiscovery() = 0;
    virtual void stopDiscovery() = 0;
    virtual void open(const DeviceDescriptor &device) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual DeviceDescriptor currentDevice() const = 0;

    // ── Accordatura del device ───────────────────────────────────────────

    virtual void setCenterFrequency(qint64 hz) = 0;
    virtual qint64 centerFrequency() const = 0;
    virtual void setSampleRate(double rate) = 0;
    virtual double sampleRate() const = 0;

    // ── Canali RX ────────────────────────────────────────────────────────
    // L'handle è opaco; è il backend a validare i limiti e a rifiutare con
    // BackendError::ResourceExhausted oltre maxRxChannels.

    virtual ChannelId createRxChannel(const RxChannelConfig &config) = 0;
    virtual void destroyRxChannel(ChannelId channel) = 0;
    virtual QList<ChannelId> channels() const = 0;

    virtual void setFrequency(ChannelId channel, qint64 hz) = 0;
    virtual void setDemod(ChannelId channel, DemodMode mode) = 0;    ///< no-op se DSP client
    virtual void setFilter(ChannelId channel, int lowHz, int highHz) = 0; ///< idem

    // ── Guadagno d'ingresso ──────────────────────────────────────────────
    //
    // Il core non conosce attenuatori né preamplificatori: chiede quanti dB
    // togliere all'ingresso, e il device sceglie con che cosa farlo. È il
    // canale per cui la guardia contro la saturazione (SPEC-003 §3) smette di
    // essere una spia e diventa un rimedio.
    //
    // Ha un'implementazione di default che non fa nulla e restituisce zero:
    // un backend che non sa togliere guadagno — un file registrato, un server
    // che consegna audio già demodulato — non deve scrivere una riga, e la sua
    // capability `maxGainReductionDb` resta a zero.
    //
    // Restituisce la riduzione **davvero** applicata, che quasi mai è quella
    // chiesta: gli attenuatori hanno passi discreti, e il chiamante deve poter
    // mostrare il valore vero invece di quello sperato.

    virtual double setGainReduction(double db);
    virtual double gainReduction() const;

    // ── Panadattatori ────────────────────────────────────────────────────

    virtual PanId createPanadapter(const PanConfig &config) = 0;
    virtual void destroyPanadapter(PanId pan) = 0;

    // ── TX (solo se capabilities().tx != TxSupport::None) ────────────────

    virtual void setPtt(bool transmit) = 0;
    virtual bool ptt() const = 0;
    virtual void setTxFrequency(qint64 hz) = 0;

    /// Ring in cui il **client scrive** i campioni da trasmettere.
    ///
    /// Che cosa ci sia dentro lo dice la capability `demod`, come per il verso
    /// della ricezione:
    ///
    ///   `DspLocation::Client`  IQ interleaved I,Q alla frequenza `sampleRate()`
    ///   `DspLocation::Device`  audio mono a 48 kHz — la radio modula da sé, e
    ///                          consegnarle banda base vorrebbe dire modulare
    ///                          due volte
    ///
    /// Non sono due metodi perché non sono due percorsi: è lo stesso ring, e
    /// chi lo riempie legge la capability che già consulta per tutto il resto.
    ///
    /// È l'unico punto del seam in cui i dati vanno nella direzione opposta, e
    /// per il resto la regola non cambia: i campioni non passano dai signal
    /// (§4.1), il produttore scrive e il consumatore legge alla propria
    /// cadenza. Il backend consuma quando la radio glielo chiede — nessuno lo
    /// avvisa, come nessuno avvisa noi quando arrivano i campioni in ricezione.
    ///
    /// Il default restituisce `nullptr`: un backend che non trasmette non
    /// scrive una riga, e chi vuole trasmettere deve comunque guardare
    /// `capabilities().tx` prima di chiedere il ring. Un `nullptr` restituito
    /// a fronte di `tx != None` è un difetto del backend, e la conformance
    /// suite lo tratta come tale.
    virtual SampleRing *txStream();

    // ── Accesso ai dati ──────────────────────────────────────────────────
    // I campioni NON passano dai signal (§4.1). Il consumatore legge da qui
    // quando il signal corrispondente segnala disponibilità.
    //
    // Per i backend raw-IQ lo stream è unico e device-wide: si richiede con
    // `kInvalidChannel`. Per i backend che canalizzano a bordo (dlink, hpsdr)
    // esiste un ring per canale.

    virtual SampleRing *iqStream(ChannelId channel = kInvalidChannel) const = 0;
    virtual SampleRing *audioStream(ChannelId channel) const = 0;
    virtual SampleRing *spectrumStream(PanId pan) const = 0;

    // ── Valvola di sfogo controllata (§4.1) ──────────────────────────────
    // Comandi nativi del protocollo. Usabile SOLO dai pannelli QML
    // backend-specifici, mai dal core: il seam resta stretto perché ciò che
    // passa di qui non entra mai nell'interfaccia generale.

    virtual QVariant nativeCommand(const QString &command, const QVariantMap &args);

signals:
    void deviceFound(const dsdr::hal::DeviceDescriptor &device);
    void discoveryFinished();
    void stateChanged(dsdr::BackendState state);
    void errorOccurred(const dsdr::hal::BackendError &error);
    void capabilitiesChanged();

    // Notifiche di disponibilità dati: il payload è un descrittore, i
    // campioni stanno nel ring corrispondente.
    void iqFrameReady(const dsdr::hal::IqFrame &frame);
    void audioFrameReady(const dsdr::hal::AudioFrame &frame);
    void spectrumFrameReady(const dsdr::hal::SpectrumFrame &frame);
    void meterUpdate(const dsdr::hal::MeterFrame &meters);

    void centerFrequencyChanged(qint64 hz);
    void sampleRateChanged(double rate);
    void pttChanged(bool transmit);
};

} // namespace dsdr::hal
