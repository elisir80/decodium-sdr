// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il motore di trasmissione, su un thread suo.
//
// Percorso completo, dal microfono all'antenna:
//
//   ring del microfono → processore di voce → modulatore → interpolazione
//   → traslazione in banda (NCO) → ring TX del backend
//
// Il ritmo non lo detta né il microfono né la radio: lo detta l'orologio, come
// nel worker del backend demo. Quanti campioni sarebbero dovuti uscire da
// quando è stato premuto il PTT è un conto che si fa sul tempo trascorso, e
// non sul numero di giri del timer — un timer che jitta non deve poter far
// derivare la frequenza di ciò che esce in aria.
//
// Se il microfono è in ritardo si trasmette silenzio per quei campioni. È la
// scelta giusta: allungare la trasmissione per aspettarlo sposterebbe tutto il
// resto, e in CW vorrebbe dire allungare i punti.
#pragma once

#include "common/Types.h"
#include "core/SpectrumFeed.h"
#include "dsp/CwKeyer.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/InterpolatorChain.h"
#include "dsp/Modulator.h"
#include "dsp/Leveller.h"
#include "dsp/Limiter.h"
#include "dsp/MultibandCompressor.h"
#include "dsp/NoiseGate.h"
#include "dsp/Nco.h"
#include "dsp/SpeechProcessor.h"
#include "dsp/SpscRing.h"
#include "dsp/VoiceRecorder.h"

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <vector>

class QTimer;

namespace dsdr::core {

class TxEngine : public QObject
{
    Q_OBJECT

public:
    /// Che cosa si consegna alla radio.
    ///
    /// Non tutte le radio vogliono la stessa cosa, e la differenza non è un
    /// dettaglio del backend ma una capability: chi demodula a bordo
    /// (`DspLocation::Device`) modula anche a bordo, e da noi vuole **audio**.
    /// Chi consegna IQ grezzo vuole la banda base complessa. La catena si
    /// accorcia da sé nel primo caso — niente modulatore, niente
    /// interpolazione, niente traslazione — perché quei tre stadi li ha già la
    /// radio, e rifarli qui vorrebbe dire modulare due volte.
    enum class Domain {
        Baseband,   ///< IQ interleaved alla frequenza del device
        Audio,      ///< audio mono a 48 kHz verso il codec della radio
    };
    Q_ENUM(Domain)

    /// Segnale di prova al posto del microfono.
    ///
    /// Un tono solo serve ad accordare: un'ampiezza costante da cui
    /// l'accordatore e il rosmetro possano leggere qualcosa di fermo.
    ///
    /// Due toni servono a un'altra cosa, e sono la prova che nessuno fa mai:
    /// due sinusoidi di pari ampiezza attraversano un finale lineare e ne
    /// escono ancora due. Se ne escono cinque o sette — i prodotti di
    /// intermodulazione del terzo e quinto ordine — quel finale sta occupando
    /// la banda dei vicini, e chi trasmette è l'unico a non sentirlo.
    enum class TestSignal {
        None,
        Tone,
        TwoTone,
    };
    Q_ENUM(TestSignal)

    explicit TxEngine(QObject *parent = nullptr);
    ~TxEngine() override;

    bool isTransmitting() const { return m_transmitting.load(std::memory_order_acquire); }

    /// Lo spettro di ciò che si sta trasmettendo.
    ///
    /// In mezzo duplex la radio si assorda mentre trasmette, e il
    /// panadattatore resterebbe una riga piatta proprio nei secondi in cui
    /// servirebbe di più: sono quelli in cui si vorrebbe vedere se il segnale
    /// è largo, se il compressore sta esagerando, se i due toni della prova
    /// ne sono diventati cinque.
    ///
    /// Con una radio che modula a bordo il segnale in aria non ce l'abbiamo:
    /// lo ricaviamo modulando noi lo stesso audio che le stiamo mandando. È
    /// quello che lei farà, e mostrarlo è più onesto che non mostrare niente.
    SpectrumFeed *spectrumFeed() const noexcept { return m_spectrum; }

    /// Letture per gli indicatori. Sono atomiche perché la UI le legge dal
    /// proprio thread mentre il motore gira: un segnale per ogni blocco
    /// costerebbe più dell'elaborazione.
    /// Gli ultimi dieci secondi della propria voce, prima e dopo la catena.
    ///
    /// Non è un accessorio: è il solo modo di regolare un trasmettitore senza
    /// avere un secondo ricevitore in stazione. Vedi `dsp::VoiceRecorder`.
    dsp::VoiceRecorder &recorder() noexcept { return m_recorder; }
    const dsp::VoiceRecorder &recorder() const noexcept { return m_recorder; }

    /// Il ring di quello che si sente **al posto della banda**: il monitor
    /// mentre si trasmette, il riascolto quando il PTT è libero.
    ///
    /// Uno solo, e non due, perché i due casi non possono capitare insieme —
    /// non ci si riascolta mentre si parla. Stereo interlacciato, come l'uscita
    /// audio se lo aspetta. Vive quanto il motore: chi lo aggancia lo tiene.
    dsp::SpscRing<float> *localAudioStream() noexcept { return &m_playback; }

    // ── Lo spettro della voce, prima e dopo la catena ────────────────────
    //
    // È l'altra metà del confronto: l'orecchio dice se una voce è bella, lo
    // spettro dice *perché*. Una gobba a 200 Hz che l'orecchio chiama «calda»
    // qui si vede, e si vede anche che occupa il doppio della banda.

    /// Quante barre. Con la trasformata a 2048 punti su 48 kHz coprono da zero
    /// a poco meno di quattro chilohertz: tutta la banda di una SSB e niente
    /// di più, perché sopra non c'è niente da guardare.
    static constexpr int kVoiceBins = 160;
    static constexpr int kVoiceFftSize = 2048;

    double voiceSpectrumSpanHz() const noexcept
    {
        return m_audioRate * kVoiceBins / kVoiceFftSize;
    }

    /// La barra `index` in dBFS. Atomica perché la legge la UI dal suo thread
    /// mentre il motore la scrive: un segnale per barra costerebbe più della
    /// trasformata.
    float voiceBinDb(bool wet, int index) const noexcept
    {
        if (index < 0 || index >= kVoiceBins)
            return -140.0f;
        return (wet ? m_wetBins : m_dryBins)[index].load(std::memory_order_relaxed);
    }

    bool monitorEnabled() const { return m_monitor.load(std::memory_order_relaxed); }
    double monitorLevel() const { return m_monitorLevel.load(std::memory_order_relaxed); }

    float micPeak() const { return m_micPeak.load(std::memory_order_relaxed); }
    float compressionDb() const { return m_compressionDb.load(std::memory_order_relaxed); }
    float outputPeak() const { return m_outputPeak.load(std::memory_order_relaxed); }
    quint64 starvedFrames() const { return m_starved.load(std::memory_order_relaxed); }

public slots:
    void start();
    void stop();

    /// Aggancia la radio. `deviceRate` è la frequenza del ring TX del backend;
    /// deve essere un multiplo intero di quella dell'audio, altrimenti la
    /// trasmissione viene rifiutata con `refused()` invece di uscire storta.
    void attach(dsdr::dsp::SpscRing<float> *txRing, double deviceRate,
                Domain domain = Domain::Baseband);
    void detach();

    /// Aggancia il microfono. Può arrivare dopo: senza, si trasmette silenzio
    /// (e in CW la portante, che non ha bisogno del microfono).
    void setMicSource(dsdr::dsp::SpscRing<float> *micRing, double micRate);

    void setSettings(const dsdr::dsp::TxSettings &settings);
    void setMicGainDb(double db);
    void setCompressionDb(double db);

    /// Il compressore multibanda, per chi lo comanda e per chi lo misura.
    dsp::MultibandCompressor &multiband() noexcept { return m_cfc; }

    /// Gli stadi di dinamica, nell'ordine in cui il segnale li attraversa.
    dsp::NoiseGate &gate() noexcept { return m_gate; }
    dsp::Leveller &leveller() noexcept { return m_leveller; }
    dsp::Limiter &limiter() noexcept { return m_limiter; }

    /// Livello d'uscita 0…1, applicato in banda base. Non è la potenza del
    /// finale — quella la conosce solo la radio — ma quanto del fondo scala
    /// del convertitore si sta usando.
    void setDrive(double drive);

    /// Traslazione del canale nella banda del device: la differenza fra la
    /// frequenza di trasmissione e il centro sintonizzato.
    void setOffsetHz(double hz);

    void setTransmitting(bool transmitting);

    /// Tasto CW. È uno slot perché arriva da un altro thread, e un metodo
    /// normale accodato per nome fallirebbe in silenzio.
    void setKeyDown(bool down);

    /// Sostituisce il microfono con un segnale di prova. In CW il segnale è
    /// la portante piena, perché è così che si accorda in CW.
    void setTestSignal(int signal);

    /// La frequenza a cui il monitor va ancorato: quella su cui si trasmette.
    void setMonitorCenter(qint64 hz);

    /// Riascolta quello che c'è: 0 la voce com'era, 1 quella che parte verso
    /// la radio. Rifiutato mentre si trasmette — non si può stare in due posti.
    void playRecording(int source);
    void stopRecordingPlayback();

    /// Commuta fra prima e dopo **senza perdere il punto**: è così che il
    /// confronto smette di dipendere dalla memoria di com'era.
    void setPlaybackSource(int source);

    /// Il monitor: sentire in cuffia quello che si sta mandando alla radio.
    ///
    /// Suona **solo mentre si trasmette**, e non c'è modo di lasciarlo acceso
    /// per sbaglio: in mezzo duplex la ricezione tace comunque, quindi non si
    /// scontra con niente, e a PTT alzato smette da sé.
    void setMonitorEnabled(bool enabled);
    void setMonitorLevel(double level);

signals:
    /// La trasmissione non può partire, e perché. La UI lo mostra invece di
    /// lasciare l'operatore davanti a un PTT premuto che non trasmette.
    void refused(const QString &reason);

    /// Emesso a bassa cadenza per gli indicatori.
    void metersUpdated();

    /// Il riascolto è partito, o è finito. Serve alla UI per non lasciare un
    /// tasto premuto su una riproduzione che si è già chiusa da sola.
    void playbackChanged();

private slots:
    void pump();

private:
    bool rebuildChain();
    void produce(std::size_t audioFrames);

    /// Travasa il riascolto dal registratore al suo ring. Gira a ogni giro di
    /// timer, anche a PTT libero.
    void pumpPlayback();

    /// Le due trasformate del confronto prima/dopo.
    void analyzeVoice(std::size_t audioFrames);

    /// Manda in cuffia quello che sta andando alla radio.
    void pumpMonitor(std::size_t audioFrames);
    void configureMonitor();

    dsp::SpeechProcessor m_speech;

    /// Il compressore multibanda (SPEC-005 §4.1). Sta dopo il processore di
    /// voce e prima del livello: quello livella e comprime a banda intera,
    /// questo divide la voce in quattro e tratta ogni parte per conto suo.
    dsp::MultibandCompressor m_cfc;

    // In testa alla catena: il gate toglie la stanza, il leveller corregge la
    // distanza dal microfono. In coda il limiter, che è l'unico che non deve
    // mai lasciar passare niente oltre il tetto.
    dsp::NoiseGate m_gate;
    dsp::Leveller m_leveller;
    dsp::Limiter m_limiter;
    dsp::Modulator m_modulator;
    dsp::InterpolatorChain m_interpolator;
    dsp::CwKeyer m_keyer;
    dsp::Nco m_nco;
    dsp::SpectrumAnalyzer m_analyzer;
    SpectrumFeed *m_spectrum = nullptr;
    qint64 m_monitorCenterHz = 0;

    dsp::VoiceRecorder m_recorder;

    /// Le due trasformate del confronto. Costano due FFT a 2048 punti ogni
    /// blocco, e solo mentre si trasmette: meno dell'interpolazione che c'è
    /// già subito dopo.
    dsp::SpectrumAnalyzer m_dryAnalyzer;
    dsp::SpectrumAnalyzer m_wetAnalyzer;
    std::vector<dsp::Complex> m_dryComplex;
    std::vector<dsp::Complex> m_wetComplex;
    std::atomic<float> m_dryBins[kVoiceBins];
    std::atomic<float> m_wetBins[kVoiceBins];

    std::atomic<bool> m_monitor{false};
    std::atomic<float> m_monitorLevel{0.5f};

    /// Il ring del riascolto. Mezzo secondo è più che sufficiente: lo riempie
    /// lo stesso timer che produce la trasmissione, e tenerlo corto vuol dire
    /// che «ferma» ferma davvero, e non mezzo secondo dopo.
    dsp::SpscRing<float> m_playback;

    /// La copia della voce prima della catena. Va presa qui perché la catena
    /// lavora sul posto: dopo il gate, l'originale non esiste più.
    std::vector<float> m_dry;
    std::vector<float> m_playbackMono;
    std::vector<float> m_playbackStereo;

    dsp::SpscRing<float> *m_txRing = nullptr;
    dsp::SpscRing<float> *m_micRing = nullptr;

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;

    std::vector<float> m_audio;
    std::vector<dsp::Complex> m_baseband;
    std::vector<dsp::Complex> m_upsampled;
    std::vector<float> m_interleaved;

    dsp::TxSettings m_settings;
    Domain m_domain = Domain::Baseband;
    double m_cwPhase = 0.0;
    double m_tonePhaseA = 0.0;
    double m_tonePhaseB = 0.0;
    std::atomic<int> m_testSignal{static_cast<int>(TestSignal::None)};
    double m_deviceRate = 0.0;
    double m_audioRate = 48000.0;
    double m_offsetHz = 0.0;
    float m_drive = 0.9f;
    int m_interpolation = 1;
    quint64 m_framesSent = 0;
    int m_meterCountdown = 0;
    int m_logCountdown = 0;

    std::atomic<bool> m_transmitting{false};
    std::atomic<float> m_micPeak{0.0f};
    std::atomic<float> m_compressionDb{0.0f};
    std::atomic<float> m_outputPeak{0.0f};
    std::atomic<quint64> m_starved{0};
    bool m_keyDown = false;
    bool m_chainReady = false;
    bool m_playbackActive = false;
    int m_playbackCountdown = 0;
};

} // namespace dsdr::core
