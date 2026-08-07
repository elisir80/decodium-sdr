// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — filtro FIR a coefficienti complessi (passa-banda asimmetrico).
//
// Con coefficienti complessi la risposta non è più simmetrica attorno a DC:
// si può quindi far passare una sola banda laterale, che è il modo in cui
// DECODIUM SDR ottiene USB/LSB senza trasformata di Hilbert.
#pragma once

#include "dsp/DspTypes.h"

#include <vector>

namespace dsdr::dsp {

class ComplexFir
{
public:
    ComplexFir();

    /// Imposta i coefficienti. Se il numero di tap non supera la capacità già
    /// prenotata non c'è allocazione: è così che il cambio di filtro da UI
    /// resta compatibile con RNF-05.
    void setTaps(const std::vector<Complex> &taps);

    void reset() noexcept;

    void process(const Complex *in, Complex *out, std::size_t n) noexcept;

    std::size_t tapCount() const noexcept { return m_taps.size(); }

private:
    std::vector<Complex> m_taps;
    std::vector<Complex> m_delay; ///< 2 × numTaps
    std::size_t m_position = 0;
};

} // namespace dsdr::dsp
