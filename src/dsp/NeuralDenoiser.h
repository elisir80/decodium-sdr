// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — riduzione di rumore neurale (DSDR-SPEC-003 §8, stadio [E]).
//
// Sotto c'è RNNoise (Xiph, BSD-3): una rete ricorrente addestrata su parlato e
// rumore, che lavora a 48 kHz su blocchi da 480 campioni. Non è il modello più
// potente in circolazione — quello arriverà con ONNX e DeepFilterNet — ma è
// l'unico che gira ovunque, e «ovunque» comprende il CM5 e i portatili di chi
// ascolta la radio, che non sono workstation.
//
// **Non gira sul thread del DSP.** L'inferenza ha un costo variabile e un
// blocco lì dentro si sentirebbe come audio a scatti su tutti i canali: questo
// stadio consuma da un ring e ripubblica su un altro, con una latenza
// dichiarata. Chi lo aziona è `NeuralNrWorker`, che vive su un thread suo.
//
// Il confine di §8.3 è una regola, non una raccomandazione: questo stadio è
// per l'orecchio umano. Non deve mai finire nel percorso IQ verso i decoder
// weak-signal — un denoiser a valle distrugge le statistiche soft su cui
// lavora l'LDPC, e un FT8 che smette di decodificare per colpa di un filtro
// «che migliora l'audio» è un difetto impossibile da diagnosticare.
#pragma once

#include <cstddef>
#include <vector>

struct DenoiseState;

namespace dsdr::dsp {

class NeuralDenoiser
{
public:
    NeuralDenoiser() = default;
    ~NeuralDenoiser();

    NeuralDenoiser(const NeuralDenoiser &) = delete;
    NeuralDenoiser &operator=(const NeuralDenoiser &) = delete;

    /// Vero se questa compilazione ha il motore neurale.
    ///
    /// È una proprietà della build, non dello stato: senza il sorgente di
    /// RNNoise la capability resta assente e la UI non mostra l'interruttore
    /// (CONSTITUTION §7). Una funzione che a volte c'è e a volte no sarebbe
    /// peggio di una che non c'è.
    static bool isAvailable() noexcept;

    bool configure(double sampleRate);
    void reset() noexcept;

    /// Elabora in place. La rete lavora a blocchi fissi: ciò che avanza resta
    /// in attesa del prossimo giro, e in uscita si consegna con il ritardo
    /// dichiarato da `latencySamples()`.
    void process(float *audio, std::size_t count) noexcept;

    /// Ritardo introdotto, in campioni: tre blocchi da dieci millisecondi.
    ///
    /// Uno è la nostra coda — un blocco entra prima di poterne uscire uno — e
    /// due sono l'analisi interna della rete, che guarda avanti prima di
    /// decidere quanto attenuare. Il numero è misurato, non dedotto: un test
    /// cerca il ritardo che riallinea uscita e ingresso e pretende sia questo,
    /// perché ci si allineano altri flussi.
    int latencySamples() const noexcept { return m_frameSize * 3; }

    /// Quanto la rete ritiene «voce» ciò che ha appena sentito, 0..1. RNNoise
    /// la calcola comunque per decidere quanto attenuare: mostrarla costa
    /// zero e dice all'operatore se lo stadio sta lavorando su qualcosa.
    float speechProbability() const noexcept { return m_speech; }

    bool isConfigured() const noexcept { return m_state != nullptr; }

private:
    DenoiseState *m_state = nullptr;
    int m_frameSize = 0;

    std::vector<float> m_pending;   ///< ingresso non ancora completo
    std::vector<float> m_ready;     ///< uscita elaborata, in attesa
    std::vector<float> m_scratch;
    std::size_t m_pendingCount = 0;
    std::size_t m_readyCount = 0;
    float m_speech = 0.0f;
};

} // namespace dsdr::dsp
