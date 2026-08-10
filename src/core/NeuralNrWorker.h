// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — lo stadio neurale su un thread suo (DSDR-SPEC-003 §8.1).
//
// Consuma dal ring che il DSP riempie e ripubblica su un ring gemello, da cui
// legge l'AudioRouter. Non tocca il thread del DSP: l'inferenza ha un costo
// che varia da blocco a blocco, e un ritardo lì dentro non si sentirebbe su
// un canale ma su tutti insieme.
//
// Quando lo stadio è spento il worker resta in mezzo e copia: una memcpy su
// audio a 48 kHz non si misura, e vale il prezzo di non dover cambiare ring
// sotto l'AudioRouter mentre suona — che è il genere di cosa che funziona
// mille volte e la millesima consegna audio di un ring smontato.
//
// Si sorveglia da solo. Se l'inferenza non sta al passo con il tempo reale
// si spegne e lo dice, invece di far uscire audio a scatti: un difetto che
// si sente ma non si spiega è il peggiore da diagnosticare.
#pragma once

#include "dsp/NeuralDenoiser.h"
#include "dsp/SpscRing.h"

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

class QTimer;

namespace dsdr::core {

class NeuralNrWorker : public QObject
{
    Q_OBJECT

public:
    explicit NeuralNrWorker(QObject *parent = nullptr);
    ~NeuralNrWorker() override;

    /// Il ring da cui l'AudioRouter deve leggere. Esiste da subito e non
    /// cambia mai: è ciò che permette di accendere e spegnere lo stadio senza
    /// toccare la catena audio.
    dsp::SpscRing<float> *outputRing() const noexcept { return m_output.get(); }

    /// Aggancia la sorgente — il ring dell'audio mixato del DSP.
    void setSource(dsp::SpscRing<float> *ring, double sampleRate, int channels);

    bool isEnabled() const { return m_enabled.load(std::memory_order_acquire); }

    /// Quota del tempo reale consumata dall'inferenza, 0..1. Sopra il 90 % si
    /// è a un passo dal non starci più.
    double load() const { return m_load.load(std::memory_order_acquire); }

    /// Quanto la rete ritiene «voce» ciò che passa, 0..1.
    double speechProbability() const { return m_speech.load(std::memory_order_acquire); }

signals:
    /// Lo stadio si è spento da solo perché non stava al passo.
    void overrun(double load);
    void enabledChanged(bool enabled);

public slots:
    void start();
    void stop();

    /// Accende o spegne lo stadio. È uno slot perché la chiamata arriva da un
    /// altro thread e viene accodata per nome: un metodo pubblico e basta si
    /// sarebbe limitato a non essere trovato, in silenzio.
    void setEnabled(bool enabled);

private slots:
    void pump();

private:
    std::unique_ptr<dsp::SpscRing<float>> m_output;
    std::atomic<dsp::SpscRing<float> *> m_source{nullptr};
    std::atomic<double> m_sourceRate{48000.0};
    std::atomic<int> m_channels{2};
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_needsReset{true};
    std::atomic<double> m_load{0.0};
    std::atomic<double> m_speech{0.0};

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;

    // Un denoiser per canale audio: la rete è monofonica, e mescolare i due
    // canali in uno solo cancellerebbe proprio la spazializzazione del CW
    // binaurale.
    dsp::NeuralDenoiser m_left;
    dsp::NeuralDenoiser m_right;

    std::vector<float> m_interleaved;
    std::vector<float> m_mono;
    int m_overrunStreak = 0;
};

} // namespace dsdr::core
