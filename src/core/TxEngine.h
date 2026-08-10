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
#include "dsp/Nco.h"
#include "dsp/SpeechProcessor.h"
#include "dsp/SpscRing.h"

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

signals:
    /// La trasmissione non può partire, e perché. La UI lo mostra invece di
    /// lasciare l'operatore davanti a un PTT premuto che non trasmette.
    void refused(const QString &reason);

    /// Emesso a bassa cadenza per gli indicatori.
    void metersUpdated();

private slots:
    void pump();

private:
    bool rebuildChain();
    void produce(std::size_t audioFrames);
    void configureMonitor();

    dsp::SpeechProcessor m_speech;
    dsp::Modulator m_modulator;
    dsp::InterpolatorChain m_interpolator;
    dsp::CwKeyer m_keyer;
    dsp::Nco m_nco;
    dsp::SpectrumAnalyzer m_analyzer;
    SpectrumFeed *m_spectrum = nullptr;
    qint64 m_monitorCenterHz = 0;

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
};

} // namespace dsdr::core
