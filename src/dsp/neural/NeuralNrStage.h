// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — lo stadio di riduzione neurale (IMPL-001 §4).
//
// È l'unico stadio della catena con un thread proprio, e il motivo è uno solo:
// l'inferenza non deve mai poter bloccare il DSP. Il costo di una rete varia
// da fotogramma a fotogramma; un ritardo dentro il thread del DSP non si
// sentirebbe su un canale, si sentirebbe su tutti insieme.
//
// L'audio entra da un ring, esce da un altro, e **passa sempre di qui**, anche
// a stadio spento. Da spento copia, e una copia su audio a 48 kHz non si
// misura: vale il prezzo di non dover cambiare ring sotto chi sta suonando —
// che è il genere di cosa che funziona mille volte e la millesima consegna
// audio da un anello smontato.
//
// ── La macchina a stati ─────────────────────────────────────────────────
//
//   Bypass    lo stadio è spento: passa l'asciutto, bit per bit
//   Warmup    acceso da poco: l'uscita passa dall'asciutto al bagnato con una
//             dissolvenza di venti millisecondi, non con uno scatto
//   Engaged   a regime
//   Degraded  il thread non sta al passo: si torna all'asciutto e **lo si
//             dice**, invece di consegnare audio a singhiozzo
//
// Il degrado è la parte che conta. Una rete che non ce la fa non produce un
// errore: produce buchi, e chi ascolta dà la colpa alla propagazione. Qui
// l'occupazione del ring d'ingresso viene sorvegliata, e mezzo secondo oltre
// soglia basta a tornare all'asciutto con una dissolvenza. Il riaggancio ha
// trenta secondi di isteresi: uno stadio che si accende e si spegne ogni due
// secondi è peggio di uno spento.
#pragma once

#include "dsp/SpscRing.h"
#include "dsp/neural/INrEngine.h"

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

class QTimer;

namespace dsdr::dsp::neural {

class NeuralNrStage : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Bypass,
        Warmup,
        Engaged,
        Degraded,
    };
    Q_ENUM(State)

    explicit NeuralNrStage(QObject *parent = nullptr);
    ~NeuralNrStage() override;

    /// Il ring in cui il DSP scrive l'audio post-AGC. Esiste da subito.
    SpscRing<float> *inputRing() const noexcept
    {
        return m_source ? m_source : m_input.get();
    }

    /// Aggancia un ring che appartiene a qualcun altro — quello del DSP —
    /// invece del proprio. Serve a innestare lo stadio in una catena che
    /// esiste già senza costringere chi produce a scrivere altrove.
    ///
    /// `channels` è il numero di canali **interlacciati** nel ring. Ogni
    /// canale ha un motore suo: la rete non deve mai vedere due canali
    /// correlati (IMPL-001 §9.3), e darle uno stereo binaurale le farebbe
    /// scambiare per rumore la differenza fra i due orecchi.
    void setSource(SpscRing<float> *ring, int channels);

    /// Il ring da cui l'AudioRouter legge. Esiste da subito e non cambia mai.
    SpscRing<float> *outputRing() const noexcept { return m_output.get(); }

    State state() const { return static_cast<State>(m_state.load(std::memory_order_acquire)); }
    bool isEnabled() const { return m_enabled.load(std::memory_order_acquire); }

    /// Ritardo complessivo dichiarato, in millisecondi: quello algoritmico del
    /// motore più la profondità media dei ring. Dichiarato e non scoperto —
    /// chi accende lo stadio deve poterlo leggere prima di sentirlo.
    double latencyMs() const { return m_latencyMs.load(std::memory_order_relaxed); }

    /// Quota del tempo reale consumata dall'inferenza, 0…1.
    double load() const { return m_load.load(std::memory_order_relaxed); }

    /// Quante volte lo stadio si è arreso da quando è acceso.
    quint64 degradeCount() const { return m_degrades.load(std::memory_order_relaxed); }

signals:
    void stateChanged(dsdr::dsp::neural::NeuralNrStage::State state);

    /// Lo stadio si è arreso, e perché. La UI ne fa un avviso: un difetto che
    /// si sente ma non si spiega è il peggiore da diagnosticare.
    void degraded(double load);

public slots:
    void start();
    void stop();

    /// Accende o spegne. È uno slot perché la chiamata arriva da un altro
    /// thread e viene accodata: un metodo normale fallirebbe in silenzio.
    void setEnabled(bool enabled);

    /// Intensità 0…100 dB, come il cursore della UI.
    void setIntensityDb(double db);

    /// Azzera gli stati ricorrenti del motore: a ogni cambio di canale o di
    /// modo la memoria del contesto precedente non deve colorare il nuovo.
    void resetEngine();

public:
    /// Installa il motore. Prende possesso, e va chiamata prima di `start()`.
    /// Il motore è iniettabile perché è così che un test può metterne uno
    /// lento e verificare che il degrado avvenga davvero.
    void setEngine(std::unique_ptr<INrEngine> engine);

    /// Un motore per canale. L'ordine è quello dell'interlacciamento.
    void setEngines(std::vector<std::unique_ptr<INrEngine>> engines);

    /// Elabora tutto ciò che è disponibile. La chiama il timer del thread, e
    /// la chiamano i test per non dipendere da un orologio.
    void pump();

private:
    void setState(State state);
    void writeFrame(const float *frame, std::size_t count);

    std::unique_ptr<SpscRing<float>> m_input;
    std::unique_ptr<SpscRing<float>> m_output;
    std::vector<std::unique_ptr<INrEngine>> m_engines;
    SpscRing<float> *m_source = nullptr;
    int m_channels = 1;

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;

    std::vector<float> m_interleaved;   ///< come arriva dal ring
    std::vector<float> m_frame;         ///< un canale alla volta
    std::vector<float> m_dry;           ///< la sua copia, per la dissolvenza

    int m_frameSamples = 480;
    /// Posizione dentro la dissolvenza, in campioni: zero è tutto asciutto,
    /// `m_crossfadeSamples` è tutto bagnato.
    int m_crossfade = 0;
    int m_crossfadeSamples = 960;

    qint64 m_overThresholdSinceNs = -1;
    qint64 m_degradedAtNs = -1;

    std::atomic<int> m_state{static_cast<int>(State::Bypass)};
    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_latencyMs{0.0};
    std::atomic<double> m_load{0.0};
    std::atomic<quint64> m_degrades{0};
};

} // namespace dsdr::dsp::neural
