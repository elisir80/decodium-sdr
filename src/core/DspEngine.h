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
#include "core/IqModuleApi.h"
#include "core/SpectrumFeed.h"
#include "dsp/ChannelProcessor.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/SpscRing.h"
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

    /// Collega un registratore al mix audio stereo, dopo il DSP dei canali e
    /// prima del sink globale. Usa lo stesso contratto lock-free del recorder IQ.
    void setAudioRecorder(IqRecorder *recorder);

    /// Carica un modulo IQ C ABI. La chiamata va eseguita nel thread DSP;
    /// SessionManager la invoca con una BlockingQueuedConnection dal thread UI.
    Q_INVOKABLE bool loadIqModule(const QString &path);
    Q_INVOKABLE void unloadIqModules();
    Q_INVOKABLE QStringList iqModuleNames() const;

public slots:
    void onIqFrameReady(const dsdr::hal::IqFrame &frame);
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

private:
    void reconfigure();
    void processAvailable();

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
    std::atomic<double> m_sourceRate{0.0};
    std::atomic<qint64> m_centerHz{0};
    std::atomic<bool> m_needsReconfigure{true};
    std::atomic<IqRecorder *> m_recorder{nullptr};
    std::atomic<IqRecorder *> m_audioRecorder{nullptr};

    double m_activeRate = 0.0;

    std::unique_ptr<dsp::SpscRing<float>> m_audioRing;
    SpectrumFeed *m_spectrum = nullptr;
    dsp::SpectrumAnalyzer m_analyzer;
    int m_fftSize = 4096;

    // unordered_map e non QHash: Channel possiede un ChannelProcessor via
    // unique_ptr ed è solo movable, mentre i contenitori Qt richiedono la copia.
    std::unordered_map<ChannelId, Channel> m_channels;

    std::vector<float> m_interleaved;   ///< lettura grezza dal ring
    std::vector<dsp::Complex> m_iq;     ///< versione complessa
    std::vector<float> m_mix;           ///< audio mixato
    std::vector<float> m_moduleIq;      ///< conversione Complex -> I/Q C ABI
    std::vector<std::unique_ptr<LoadedIqModule>> m_iqModules;
    QElapsedTimer m_uptime;             ///< base dei tempi per il throttling
    quint64 m_totalDropped = 0;
    qint64 m_lastOverrunReportNs = 0;
    qint64 m_lastStatsNs = 0;
    quint64 m_statsIqFrames = 0;
    quint64 m_statsAudioFrames = 0;
    quint64 m_statsBlocks = 0;
};

} // namespace dsdr::core

Q_DECLARE_METATYPE(dsdr::dsp::ChannelSettings)
