// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — thread di ingest del backend demo.
//
// Genera campioni a ritmo reale: il debito è calcolato sull'orologio monotono,
// non sul numero di tick, così il flusso non deriva anche se il timer jitta.
// Scrive nel ring SPSC di cui è l'unico produttore.
#pragma once

#include "dsp/DspTypes.h"
#include "hal/backends/demo/SyntheticBand.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

class QTimer;

namespace dsdr::hal::demo {

class DemoWorker : public QObject
{
    Q_OBJECT

public:
    explicit DemoWorker(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~DemoWorker() override;

    /// Chiamata dal thread proprietario PRIMA di `moveToThread()`: evita di
    /// dover registrare un metatype per il piano di banda solo per farlo
    /// attraversare una connessione queued.
    void configure(double sampleRate, qint64 centerHz, std::vector<StationSpec> stations);

public slots:
    void start();
    void stop();
    void setCenterFrequency(qint64 hz);
    void setTransmitting(bool transmitting);

    /// Attenuazione applicata al segnale generato, in dB.
    ///
    /// Serve a rendere vera la richiesta della guardia contro la saturazione:
    /// un demo che accettasse il comando senza cambiare i campioni farebbe
    /// passare in CI un automatismo che nella realtà non fa niente.
    void setGainReductionDb(double db);

signals:
    /// Emesso dopo ogni blocco scritto nel ring: `dropped` conta le coppie
    /// I/Q perse per overrun (consumatore troppo lento).
    void framesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private slots:
    void tick();

private:
    dsp::SpscRing<float> *m_ring = nullptr;
    QTimer *m_timer = nullptr;
    SyntheticBand m_band;
    QElapsedTimer m_clock;

    std::vector<dsp::Complex> m_block;
    std::vector<float> m_interleaved;

    double m_sampleRate = 192000.0;
    qint64 m_centerHz = 7'100'000;
    quint64 m_framesGenerated = 0;
    bool m_running = false;
    bool m_transmitting = false;
    float m_gainScale = 1.0f;   ///< attenuazione richiesta, in lineare
};

} // namespace dsdr::hal::demo
