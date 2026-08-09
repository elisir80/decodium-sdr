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
#include "core/SpectrumFeed.h"
#include "dsp/ChannelProcessor.h"
#include "dsp/NoiseBlanker.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/SpscRing.h"
#include "dsp/TimeShiftBuffer.h"
#include "hal/Frames.h"

#include <QElapsedTimer>
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

    /// Aggancia la sorgente IQ. Thread-safe: il thread DSP recepisce il
    /// cambiamento al frame successivo, senza fermare nulla a mano.
    void setSource(dsp::SpscRing<float> *ring, double sampleRate, qint64 centerFrequencyHz);
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

public slots:
    void onIqFrameReady(const dsdr::hal::IqFrame &frame);
    void addChannel(dsdr::ChannelId id, const dsdr::dsp::ChannelSettings &settings);
    void updateChannel(dsdr::ChannelId id, const dsdr::dsp::ChannelSettings &settings);
    void removeChannel(dsdr::ChannelId id);
    void setFftSize(int size);

signals:
    /// Misure per la UI, aggregate: un'emissione per blocco, non per campione.
    void metersUpdated(dsdr::ChannelId id, float signalDb, float agcGainDb);
    void overrunDetected(quint64 lostFrames);

    /// Stato della macchina del tempo, aggregato come i meter: la storia
    /// cresce a ogni blocco e non merita un segnale per blocco.
    void replayStateChanged(double delaySeconds, double historySeconds);

private:
    void reconfigure();
    void processAvailable();

    struct Channel
    {
        std::unique_ptr<dsp::ChannelProcessor> processor;
        std::vector<float> audio;
        dsp::ChannelSettings settings;
        qint64 lastMeterNs = 0;
    };

    // Sorgente: puntatori atomici perché il thread UI può sostituirla mentre
    // il thread DSP sta lavorando.
    std::atomic<dsp::SpscRing<float> *> m_source{nullptr};
    std::atomic<double> m_sourceRate{0.0};
    std::atomic<qint64> m_centerHz{0};
    std::atomic<bool> m_needsReconfigure{true};
    std::atomic<IqRecorder *> m_recorder{nullptr};

    // Macchina del tempo. Il buffer appartiene al thread DSP; queste tre
    // atomiche sono la sola superficie che la UI tocca.
    std::atomic<std::size_t> m_replayDelayFrames{0};
    std::atomic<std::size_t> m_historyFrames{0};
    std::atomic<bool> m_historyDirty{false};

    std::atomic<bool> m_nbEnabled{false};
    std::atomic<double> m_nbThreshold{4.0};
    std::atomic<float> m_nbActivity{0.0f};

    double m_activeRate = 0.0;

    std::unique_ptr<dsp::SpscRing<float>> m_audioRing;
    SpectrumFeed *m_spectrum = nullptr;
    dsp::SpectrumAnalyzer m_analyzer;
    int m_fftSize = 4096;

    // unordered_map e non QHash: Channel possiede un ChannelProcessor via
    // unique_ptr ed è solo movable, mentre i contenitori Qt richiedono la copia.
    std::unordered_map<ChannelId, Channel> m_channels;

    dsp::TimeShiftBuffer m_history;     ///< gli ultimi secondi di banda
    dsp::NoiseBlanker m_blanker;

    std::vector<float> m_interleaved;   ///< lettura grezza dal ring
    std::vector<dsp::Complex> m_iq;     ///< versione complessa
    std::vector<float> m_mix;           ///< audio mixato
    QElapsedTimer m_uptime;             ///< base dei tempi per il throttling
    quint64 m_totalDropped = 0;
    qint64 m_lastOverrunReportNs = 0;
    qint64 m_lastReplayReportNs = 0;
};

} // namespace dsdr::core

Q_DECLARE_METATYPE(dsdr::dsp::ChannelSettings)
