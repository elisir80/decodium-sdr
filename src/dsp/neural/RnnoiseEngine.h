// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — motore RNNoise (IMPL-001 §1, fallback bundled).
//
// RNNoise è C puro, sta nel pacchetto, funziona offline e costa meno del
// cinque per cento di un core. Non è il migliore — DeepFilterNet3 lo supera —
// ma è quello che c'è sempre, ed è il motore su cui si collauda il telaio:
// stati, ring, macchina a stati, degrado. Quando DFN3 arriverà, troverà tutto
// già provato.
//
// Due dettagli che non si indovinano.
//
// La rete lavora su campioni in scala PCM a 16 bit, non in ±1: passarle
// direttamente il nostro audio normalizzato le darebbe un segnale
// quarantamila volte più piccolo di quello su cui è stata addestrata, e la
// rete concluderebbe che è tutto rumore.
//
// E l'attenuazione non è un suo parametro: RNNoise toglie quello che decide
// lui. Il comando di intensità si applica quindi **fuori**, mescolando
// l'asciutto con il bagnato — che è anche il modo in cui si ottiene un
// passaggio morbido invece di un interruttore.
#pragma once

#include "dsp/neural/INrEngine.h"

#include <vector>

struct DenoiseState;

namespace dsdr::dsp::neural {

class RnnoiseEngine : public INrEngine
{
public:
    RnnoiseEngine();
    ~RnnoiseEngine() override;

    bool prepare(const QString &modelPath, QString *error) override;
    void processFrame(float *samples) override;
    void setAttenuationLimitDb(float db) override;
    NrEngineInfo info() const override;
    void reset() override;

    /// Vero se questa compilazione ha il motore. Senza, `prepare` fallisce
    /// dicendolo: un motore che non c'è non deve fingere di lavorare.
    static bool isAvailable();

    /// Quanto asciutto resta, per una data attenuazione in dB. Pura e statica
    /// perché è la mappa che decide come si comporta il cursore, ed è meglio
    /// verificarla che fidarsene.
    static float dryMixFor(float attenuationDb);

private:
    DenoiseState *m_state = nullptr;
    std::vector<float> m_scaled;   ///< il fotogramma in scala PCM
    std::vector<float> m_dry;      ///< copia dell'ingresso, per la miscela
    float m_dryMix = 0.0f;
    float m_attenuationDb = 100.0f;
    int m_frameSamples = 480;
};

} // namespace dsdr::dsp::neural
