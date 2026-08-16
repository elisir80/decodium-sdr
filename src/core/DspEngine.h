// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — orchestratore del DSP, su thread dedicato.
//
// Consuma il ring IQ del backend, distribuisce i campioni ai ChannelProcessor,
// mixa l'audio verso l'AudioRouter e alimenta il panadattatore.
//
// Vincoli (CONSTITUTION §5): nel percorso caldo non alloca, non prende lock e
// non emette signal con payload di campioni. Le uniche allocazioni avvengono
// in `reconfigure()`, fuori dallo streaming.
#pragma once

#include "core/IqRecorder.h"
#include "audio/NetworkAudioSink.h"
#include "core/IqModuleApi.h"
#include "core/SpectrumFeed.h"
#include "dsp/ChannelProcessor.h"
#include "dsp/ComplexFir.h"
#include "dsp/NoiseBlanker.h"
#include "dsp/OverloadGuard.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/SpscRing.h"
#include "dsp/TimeShiftBuffer.h"
#include "hal/Frames.h"

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QObject>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dsdr::core {

class DspEngine : public QObject
{
    Q_OBJECT

public:
    explicit DspEngine(QObject *parent = nullptr);
    ~DspEngine() override;

    /// Ring dell'audio mixato, letto dall'AudioRouter (consumatore unico).
    dsp::SpscRing<float> *audioRing() const noexcept { return m_audioRing.get(); }

    SpectrumFeed *spectrumFeed() const noexcept { return m_spectrum; }

    /// Lo spettro dell'audio che si sta ascoltando, non della banda.
    ///
    /// È un secondo feed con la stessa forma del primo — stesso ring, stesso
    /// contratto — ma alimentato dal mix audio finale, quello che esce dagli
    /// altoparlanti: passa dai filtri del canale, dall'AGC, dalla riduzione di
    /// rumore. È lì che si vede se il notch ha preso il fischio, se il filtro
    /// taglia dove si crede, se la riduzione di rumore sta mangiando le
    /// consonanti insieme al fruscio.
    ///
    /// Solo la metà positiva: l'audio è un segnale reale e la sua trasformata
    /// è simmetrica, quindi la metà sotto lo zero è la stessa cosa specchiata.
    SpectrumFeed *audioSpectrumFeed() const noexcept { return m_audioSpectrum; }

    /// L'audio nel dominio del tempo, per l'oscilloscopio.
    ///
    /// Un ring a parte e non quello che alimenta la scheda audio: quello ha un
    /// consumatore solo — è il contratto SPSC — e leggerlo in due vorrebbe
    /// dire rubarsi i campioni a vicenda, cioè far saltare l'ascolto per
    /// disegnare una traccia.
    ///
    /// Chi guarda può restare indietro: il ring è piccolo e chi scrive scarta
    /// il più vecchio. Un oscilloscopio in ritardo di mezzo secondo non è un
    /// oscilloscopio.
    dsp::SpscRing<float> *audioScopeRing() const noexcept { return m_scopeRing.get(); }

    /// Aggancia la sorgente IQ. Thread-safe: il thread DSP recepisce il
    /// cambiamento al frame successivo, senza fermare nulla a mano.
    void setSource(dsp::SpscRing<float> *ring, double sampleRate, qint64 centerFrequencyHz);

    // ── Sorgente audio: i backend server-DSP (SPEC-004) ─────────────────
    //
    // Una radio tradizionale non consegna banda base: consegna l'audio che ha
    // già demodulato. Non serve però un secondo motore, perché quell'audio
    // **è** un segnale in banda base — reale invece che complesso.
    //
    // Ricostruendone il segnale analitico si torna esattamente al caso di
    // sempre: una componente a 1500 Hz d'audio ridiventa una componente a
    // VFO+1500 Hz di radiofrequenza, il panadattatore si ancora alla
    // frequenza vera (SPEC-004 §4), e tutti gli stadi della SPEC-003 —
    // notch, EMNR, rete neurale, APF, binaurale — si applicano senza
    // saperne nulla.
    //
    // Da quale parte stia la banda laterale lo dice il modo della radio, e
    // non si può indovinare: in USB l'audio sale con la frequenza, in LSB
    // scende. Sbagliarlo mette il segnale dalla parte opposta del VFO.

    enum class Sideband {
        Upper,   ///< USB, DIGU, CW: RF = VFO + audio
        Lower,   ///< LSB, DIGL: RF = VFO − audio
        Double,  ///< AM, FM: l'emissione occupa entrambi i lati, e la
                 ///< immagine speculare dello spettro è la verità
    };

    void setAudioSource(dsp::SpscRing<float> *ring, double sampleRate,
                        qint64 centerFrequencyHz);
    void setAudioSideband(int sideband);

    void clearSource();

    /// Aggiorna la frequenza centrale (l'offset dei canali è relativo a essa).
    void setCenterFrequency(qint64 hz);

    /// Collega un registratore al flusso IQ del device. Il tap è preso prima
    /// di qualsiasi elaborazione: si registra ciò che la radio ha consegnato,
    /// non ciò che il DSP ne ha fatto. Thread-safe.
    void setRecorder(IqRecorder *recorder);

    // ── Macchina del tempo ──────────────────────────────────────────────
    //
    // Il motore tiene sempre gli ultimi secondi di banda in memoria. Con un
    // ritardo diverso da zero smette di demodulare la testa del flusso e si
    // sposta indietro: audio, spettro e waterfall raccontano insieme lo stesso
    // istante passato, mentre la radio continua a consegnare il presente.
    //
    // Thread-safe: la UI scrive, il thread DSP legge al blocco successivo.

    /// Riavvolge di `seconds` rispetto alla diretta. Zero torna al presente.
    /// Un ritardo più profondo della storia disponibile viene accorciato a
    /// quello che c'è, e il valore corretto si rilegge da `replayDelaySeconds`.
    void setReplayDelaySeconds(double seconds);
    double replayDelaySeconds() const;

    /// Di quanto si può tornare indietro adesso. Cresce dopo la connessione
    /// fino alla capacità decisa dal ritmo di campionamento.
    double historySeconds() const;

    /// Capacità della memoria di scorrimento, in secondi.
    double historyCapacitySeconds() const;

    // ── Noise blanker a banda piena (SPEC-003 §4) ───────────────────────
    //
    // Sta qui e non nel canale: un impulso è dell'ambiente, arriva su tutta la
    // banda e va tolto una volta sola, prima che ogni ricevitore decimi la sua
    // fetta. Farlo per canale significherebbe rifare lo stesso lavoro N volte
    // e — soprattutto — farlo troppo tardi.
    void setNoiseBlanker(bool enabled, double threshold);
    bool noiseBlankerEnabled() const { return m_nbEnabled.load(std::memory_order_acquire); }
    double noiseBlankerThreshold() const;

    /// Quota di campioni ricuciti nell'ultimo blocco, 0..1: la spia che
    /// distingue un blanker che lavora da uno che tagliuzza.
    float noiseBlankerActivity() const { return m_nbActivity.load(std::memory_order_acquire); }

    // ── Guardia contro la saturazione (SPEC-003 §3) ─────────────────────
    //
    // Guarda il picco del flusso come arriva dal device — prima del blanker e
    // prima di qualunque altra cosa — perché la saturazione avviene nel
    // convertitore, e ciò che il DSP ne fa dopo non la racconta più.
    void setOverloadMode(int mode);
    int overloadMode() const { return m_overloadMode.load(std::memory_order_acquire); }
    bool overloaded() const { return m_overloaded.load(std::memory_order_acquire); }
    double peakDbfs() const { return m_peakDbfs.load(std::memory_order_acquire); }
    /// Collega un registratore al mix audio stereo, dopo il DSP dei canali e
    /// prima del sink globale. Usa lo stesso contratto lock-free del recorder IQ.
    void setAudioRecorder(IqRecorder *recorder);

    /// Copia il mix lineare verso un trasporto PCM di rete. È un tap distinto
    /// dall'uscita locale: un client lento non può sottrarre campioni a chi
    /// ascolta sugli altoparlanti.
    void setNetworkAudioSink(audio::NetworkAudioSink *sink);

    /// Carica un modulo IQ C ABI. La chiamata va eseguita nel thread DSP;
    /// SessionManager la invoca con una BlockingQueuedConnection dal thread UI.
    Q_INVOKABLE bool loadIqModule(const QString &path);
    /// Scarica un solo modulo al confine fra due blocchi DSP. Non può correre
    /// in parallelo al suo callback perché entrambe le azioni vivono su questo
    /// stesso thread.
    Q_INVOKABLE bool unloadIqModule(const QString &path);
    Q_INVOKABLE void unloadIqModules();
    Q_INVOKABLE QStringList iqModuleNames() const;
    Q_INVOKABLE QString iqModuleName(const QString &path) const;
    Q_INVOKABLE QString lastIqModuleError() const;


public slots:
    void onIqFrameReady(const dsdr::hal::IqFrame &frame);

    /// Stessa cosa per i backend che consegnano audio: il segnale porta solo
    /// il descrittore, i campioni stanno nel ring (§4.1).
    void onAudioFrameReady(const dsdr::hal::AudioFrame &frame);
    void addChannel(dsdr::ChannelId id, const dsdr::dsp::ChannelSettings &settings);
    void updateChannel(dsdr::ChannelId id, const dsdr::dsp::ChannelSettings &settings);
    void removeChannel(dsdr::ChannelId id);
    void setFftSize(int size);

signals:
    /// Misure per la UI, aggregate: un'emissione per blocco, non per campione.
    void metersUpdated(dsdr::ChannelId id, float signalDb, float noiseFloorDb,
                       float snrDb, float audioLevelDb, float agcGainDb);
    void rdsUpdated(dsdr::ChannelId id, bool synced, const QString &pi,
                    int countryCode, int programCoverage, int referenceNumber,
                    const QString &callsign,
                    const QString &programType, const QString &alternateFrequencies,
                    const QString &programService, const QString &radioText);
    void overrunDetected(quint64 lostFrames);

    /// Stato della macchina del tempo, aggregato come i meter: la storia
    /// cresce a ogni blocco e non merita un segnale per blocco.
    void replayStateChanged(double delaySeconds, double historySeconds);

    /// L'ingresso è entrato o uscito dalla saturazione, e di quanto la guardia
    /// chiederebbe di correggere il guadagno (0 se non chiede nulla).
    void overloadStateChanged(bool overloaded, double peakDbfs, double requestDb);

    /// La riga più forte dello spettro audio, in hertz, e il suo livello.
    ///
    /// È il tono che si sente. Serve a due cose che si fanno di continuo e che
    /// finora si facevano a orecchio: portare una CW al passo giusto — quella
    /// nota che si sceglie una volta e si insegue per anni — e verificare che
    /// una portante stia dove si crede. Zero quando non c'è niente che
    /// emerga: un tono inventato è peggio di nessun tono.
    /// `thdPercent` è la distorsione armonica totale, o −1 quando non c'è un
    /// tono su cui misurarla. Si misura solo dove ha senso: su del rumore le
    /// «armoniche» sono altro rumore, e il numero che ne uscirebbe sarebbe
    /// preciso e privo di significato.
    void audioToneMeasured(double frequencyHz, double levelDb, double thdPercent);

    /// Picco e valore efficace dell'audio nel blocco appena mixato, in dBFS.
    ///
    /// Sono due misure diverse e servono a due cose diverse: il picco dice se
    /// si sta tosando, il valore efficace dice quanto è forte davvero. La
    /// distanza fra i due è il fattore di cresta, che su una voce sta attorno
    /// ai dodici decibel e su una portante a tre — ed è il numero che dice se
    /// il compressore sta esagerando.
    void audioLevelsMeasured(double peakDb, double rmsDb);

    /// Quanti campioni al secondo sono arrivati davvero, e quanti se ne
    /// aspettavano. Una volta al secondo, dallo stesso punto in cui il motore
    /// tira le somme per il log.
    ///
    /// È la misura che dice se il flusso regge. Su una sorgente locale il
    /// rapporto sta incollato a uno; su una di rete scende appena il
    /// collegamento comincia a perdere pezzi, e scende **prima** che si senta
    /// qualcosa — che è l'unico momento in cui saperlo serve a qualcosa.
    void streamRateMeasured(double measuredRate, double nominalRate);

private:
    void reconfigure();

    /// Trasforma il mix audio e ne ricava spettro e tono dominante.
    /// Gira sul thread DSP, dentro il ciclo di elaborazione.
    void analyzeAudio(std::size_t frames);

    void processAvailable();
    void attachSource(dsp::SpscRing<float> *ring, double sampleRate,
                      qint64 centerFrequencyHz);

    /// Trasforma `count` campioni audio reali (in `m_mono`) nel segnale
    /// analitico interleaved di `m_interleaved`. Da qui in poi il resto del
    /// motore non sa più da dove sia arrivato il flusso.
    void makeAnalytic(std::size_t count);

    struct Channel
    {
        std::unique_ptr<dsp::ChannelProcessor> processor;
        std::vector<float> audio;
        dsp::ChannelSettings settings;
        qint64 lastMeterNs = 0;
        qint64 lastRdsNs = 0;
        bool lastRdsSynced = false;
        QString lastRdsPi;
        int lastRdsCountryCode = -1;
        int lastRdsProgramCoverage = -1;
        int lastRdsReferenceNumber = -1;
        QString lastRdsCallsign;
        QString lastRdsProgramType;
        QString lastRdsAlternateFrequencies;
        QString lastRdsProgramService;
        QString lastRdsRadioText;
    };

    struct LoadedIqModule;

    // Sorgente: puntatori atomici perché il thread UI può sostituirla mentre
    // il thread DSP sta lavorando.
    std::atomic<dsp::SpscRing<float> *> m_source{nullptr};
    std::atomic<bool> m_sourceIsAudio{false};
    std::atomic<int> m_sideband{static_cast<int>(Sideband::Upper)};
    std::atomic<double> m_sourceRate{0.0};
    std::atomic<qint64> m_centerHz{0};
    std::atomic<bool> m_needsReconfigure{true};
    std::atomic<IqRecorder *> m_recorder{nullptr};
    std::atomic<IqRecorder *> m_audioRecorder{nullptr};
    std::atomic<audio::NetworkAudioSink *> m_networkAudioSink{nullptr};

    // Macchina del tempo. Il buffer appartiene al thread DSP; queste tre
    // atomiche sono la sola superficie che la UI tocca.
    std::atomic<std::size_t> m_replayDelayFrames{0};
    std::atomic<std::size_t> m_historyFrames{0};
    std::atomic<bool> m_historyDirty{false};

    std::atomic<bool> m_nbEnabled{false};
    std::atomic<int> m_overloadMode{0};
    std::atomic<bool> m_overloaded{false};
    std::atomic<double> m_peakDbfs{-160.0};
    std::atomic<double> m_nbThreshold{4.0};
    std::atomic<float> m_nbActivity{0.0f};

    double m_activeRate = 0.0;
    // Accesso solo dal thread DSP. Evita di accodare una continuazione per
    // ogni notifica IQ quando il producer è più veloce del processore.
    bool m_processContinuationPending = false;

    std::unique_ptr<dsp::SpscRing<float>> m_audioRing;
    SpectrumFeed *m_spectrum = nullptr;
    dsp::SpectrumAnalyzer m_analyzer;
    int m_fftSize = 4096;

    // ── Analisi dell'audio ───────────────────────────────────────────────
    //
    // Un secondo analizzatore, alla frequenza dell'audio e con una FFT molto
    // più corta: 2048 punti su 48 kHz danno poco più di venti hertz di
    // risoluzione, che su una passata di tre kilohertz è quello che serve —
    // si distingue una nota dall'altra senza spendere un millisecondo per
    // trasformata.
    SpectrumFeed *m_audioSpectrum = nullptr;
    std::unique_ptr<dsp::SpscRing<float>> m_scopeRing;
    dsp::SpectrumAnalyzer m_audioAnalyzer;
    std::vector<dsp::Complex> m_audioScratch;  ///< il mix, in forma complessa
    qint64 m_lastToneNs = 0;
    qint64 m_lastLevelNs = 0;

    // unordered_map e non QHash: Channel possiede un ChannelProcessor via
    // unique_ptr ed è solo movable, mentre i contenitori Qt richiedono la copia.
    std::unordered_map<ChannelId, Channel> m_channels;

    dsp::TimeShiftBuffer m_history;     ///< gli ultimi secondi di banda
    dsp::NoiseBlanker m_blanker;
    dsp::OverloadGuard m_overload;
    bool m_lastOverloadReported = false;

    dsp::ComplexFir m_analytic;         ///< passa-banda a sole frequenze positive
    std::vector<float> m_mono;          ///< audio reale, prima dell'analitico
    std::vector<dsp::Complex> m_analyticScratch;
    std::vector<float> m_interleaved;   ///< lettura grezza dal ring
    std::vector<dsp::Complex> m_iq;     ///< versione complessa
    std::vector<float> m_mix;           ///< audio mixato
    std::vector<float> m_moduleIq;      ///< conversione Complex -> I/Q C ABI
    std::vector<std::unique_ptr<LoadedIqModule>> m_iqModules;
    QString m_lastIqModuleError;
    QElapsedTimer m_uptime;             ///< base dei tempi per il throttling
    quint64 m_totalDropped = 0;
    qint64 m_lastOverrunReportNs = 0;
    qint64 m_lastReplayReportNs = 0;
    qint64 m_lastStatsNs = 0;
    quint64 m_statsIqFrames = 0;
    quint64 m_statsAudioFrames = 0;
    quint64 m_statsBlocks = 0;
};

} // namespace dsdr::core

Q_DECLARE_METATYPE(dsdr::dsp::ChannelSettings)
