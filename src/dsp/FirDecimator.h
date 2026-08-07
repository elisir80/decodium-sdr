// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — decimatore FIR per segnali complessi.
//
// Il MAC viene calcolato solo sui campioni di uscita effettivi: il costo per
// campione d'ingresso è quindi numTaps/decimation, che è l'efficienza di una
// polifase senza doverne gestire esplicitamente i rami.
//
// La linea di ritardo è a doppia scrittura (buffer 2N): la finestra degli
// ultimi N campioni è sempre contigua, così il ciclo interno è vettorizzabile.
#pragma once

#include "dsp/DspTypes.h"

#include <vector>

namespace dsdr::dsp {

class FirDecimator
{
public:
    /// Prepara il filtro. Alloca; da chiamare fuori dal percorso caldo.
    void configure(std::vector<float> taps, int decimation);

    void reset() noexcept;

    /// Elabora `n` campioni d'ingresso, scrive gli usciti in `out` e ne
    /// restituisce il numero. `out` deve avere spazio per `maxOutput(n)`.
    std::size_t process(const Complex *in, std::size_t n, Complex *out) noexcept;

    int decimation() const noexcept { return m_decimation; }
    std::size_t tapCount() const noexcept { return m_taps.size(); }

    std::size_t maxOutput(std::size_t inputFrames) const noexcept
    {
        return inputFrames / static_cast<std::size_t>(m_decimation) + 1;
    }

private:
    std::vector<float> m_taps;
    std::vector<Complex> m_delay; ///< 2 × numTaps
    std::size_t m_position = 0;
    int m_decimation = 1;
    int m_phase = 0;
};

} // namespace dsdr::dsp
