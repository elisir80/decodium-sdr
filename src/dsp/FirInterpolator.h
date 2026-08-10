// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — interpolatore FIR per segnali complessi (TX).
//
// È il gemello di FirDecimator, percorso all'incontrario: il segnale modulato
// nasce alla frequenza dell'audio e deve arrivare a quella del convertitore,
// che è cento volte più alta.
//
// L'interpolazione «di libro» inserisce L-1 zeri e filtra: il FIR farebbe
// allora L moltiplicazioni ogni L campioni contro zeri noti. Qui i coefficienti
// sono invece riordinati in L **fasi**, e ogni campione d'uscita costa
// numTaps/L moltiplicazioni — lo stesso risparmio che il decimatore ottiene
// calcolando solo le uscite che tiene.
//
// La linea di ritardo è a doppia scrittura (buffer 2N) come nel decimatore:
// la finestra degli ultimi N campioni resta contigua e il ciclo interno
// vettorizzabile.
#pragma once

#include "dsp/DspTypes.h"

#include <vector>

namespace dsdr::dsp {

class FirInterpolator
{
public:
    /// Prepara il filtro. `taps` è il passa-basso progettato alla frequenza
    /// **d'uscita**; il guadagno L necessario a compensare gli zeri inseriti
    /// lo applica questa funzione, non il chiamante. Alloca: da chiamare fuori
    /// dal percorso caldo.
    void configure(std::vector<float> taps, int interpolation);

    void reset() noexcept;

    /// Elabora `n` campioni d'ingresso e ne scrive `n × interpolation` in
    /// `out`, che deve avere spazio per `maxOutput(n)`. Restituisce il numero
    /// di campioni prodotti.
    std::size_t process(const Complex *in, std::size_t n, Complex *out) noexcept;

    int interpolation() const noexcept { return m_interpolation; }
    std::size_t tapCount() const noexcept { return m_tapsPerPhase * static_cast<std::size_t>(m_interpolation); }
    std::size_t tapsPerPhase() const noexcept { return m_tapsPerPhase; }

    std::size_t maxOutput(std::size_t inputFrames) const noexcept
    {
        return inputFrames * static_cast<std::size_t>(m_interpolation);
    }

private:
    /// Coefficienti riordinati: fase p, tap k → m_phases[p * m_tapsPerPhase + k].
    std::vector<float> m_phases;
    std::vector<Complex> m_delay; ///< 2 × tapsPerPhase
    std::size_t m_position = 0;
    std::size_t m_tapsPerPhase = 0;
    int m_interpolation = 1;
};

} // namespace dsdr::dsp
