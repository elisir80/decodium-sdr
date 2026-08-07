// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — tipi base del motore DSP.
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>

namespace dsdr::dsp {

using Real = float;
using Complex = std::complex<Real>;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/// Dimensione massima di un blocco elaborato in un colpo. Tutti i buffer di
/// lavoro sono dimensionati su questo valore in fase di `configure()`, così che
/// il percorso caldo non allochi mai (RNF-05, CONSTITUTION §5).
inline constexpr std::size_t kMaxBlockFrames = 16384;

/// Numero massimo di tap di un singolo stadio FIR. Serve a prenotare la
/// capacità dei vettori dei coefficienti: un cambio di filtro a runtime
/// riscrive i coefficienti senza riallocare.
inline constexpr std::size_t kMaxFirTaps = 1024;

inline float magnitudeSquared(Complex c) noexcept
{
    return c.real() * c.real() + c.imag() * c.imag();
}

/// dB di potenza da un modulo quadro, con floor per evitare -inf.
float powerToDb(float magSquared) noexcept;

} // namespace dsdr::dsp
