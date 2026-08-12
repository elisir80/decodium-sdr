// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/ParametricEq.h"

#include <algorithm>
#include <cmath>
#include <complex>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

namespace dsdr::dsp {

namespace {

/// Le frequenze di partenza delle cinque celle.
///
/// Non equispaziate in hertz ma in ottave, perché è così che si sente: fra 200
/// e 400 c'è la stessa distanza percepita che fra 1600 e 3200. Coprono la
/// passata di una voce da un capo all'altro — il corpo sui 150, la presenza
/// sui 3200 — così che aprendo l'equalizzatore ci sia già un punto dove serve,
/// invece di cinque punti ammucchiati da distribuire a mano.
constexpr double kDefaultFrequencies[ParametricEq::kBands] = {
    150.0, 400.0, 900.0, 1800.0, 3200.0,
};

} // namespace

void ParametricEq::configure(double sampleRate, int channels)
{
    if (!(sampleRate > 0.0))
        return;

    m_sampleRate = sampleRate;
    m_channels = std::clamp(channels, 1, 2);

    if (!m_configured) {
        for (int i = 0; i < kBands; ++i) {
            m_bands[i].frequency.store(kDefaultFrequencies[i], std::memory_order_relaxed);
            m_bands[i].gainDb.store(0.0, std::memory_order_relaxed);
            m_bands[i].q.store(1.0, std::memory_order_relaxed);
        }
        m_configured = true;
    }

    reset();
    redesign();
    m_dirty.store(false, std::memory_order_release);
}

void ParametricEq::reset() noexcept
{
    for (Cell &cell : m_cells) {
        cell.z1.fill(0.0);
        cell.z2.fill(0.0);
    }
}

void ParametricEq::setBand(int index, double frequencyHz, double gainDb, double q) noexcept
{
    if (index < 0 || index >= kBands)
        return;

    // I limiti stanno qui e non nella UI: è il filtro a sapere che cosa può
    // fare senza diventare instabile, e una UI che se ne dimentica uno produce
    // un rumore che nessuno collega alla manopola che l'ha causato.
    const double nyquist = m_sampleRate > 0.0 ? m_sampleRate * 0.5 : 24000.0;
    m_bands[index].frequency.store(std::clamp(frequencyHz, 20.0, nyquist * 0.95),
                                   std::memory_order_relaxed);
    m_bands[index].gainDb.store(std::clamp(gainDb, -kMaxGainDb, kMaxGainDb),
                                std::memory_order_relaxed);
    // Sotto 0,3 la campana diventa una collina che copre tutto lo spettro;
    // sopra 12 diventa una riga, e a quel punto serve un notch, non un'EQ.
    m_bands[index].q.store(std::clamp(q, 0.3, 12.0), std::memory_order_relaxed);

    m_dirty.store(true, std::memory_order_release);
}

double ParametricEq::bandFrequency(int index) const noexcept
{
    if (index < 0 || index >= kBands)
        return 0.0;
    return m_bands[static_cast<std::size_t>(index)].frequency.load(std::memory_order_relaxed);
}

double ParametricEq::bandGainDb(int index) const noexcept
{
    if (index < 0 || index >= kBands)
        return 0.0;
    return m_bands[static_cast<std::size_t>(index)].gainDb.load(std::memory_order_relaxed);
}

double ParametricEq::bandQ(int index) const noexcept
{
    if (index < 0 || index >= kBands)
        return 0.0;
    return m_bands[static_cast<std::size_t>(index)].q.load(std::memory_order_relaxed);
}

void ParametricEq::redesign() noexcept
{
    if (!(m_sampleRate > 0.0))
        return;

    // Campana RBJ: la formula del ricettario di Robert Bristow-Johnson, che è
    // pubblica, riscritta qui. `a` è la radice del guadagno perché una campana
    // lo applica per metà sopra e per metà sotto il punto centrale.
    for (int i = 0; i < kBands; ++i) {
        const auto band = static_cast<std::size_t>(i);
        const double gainDb = m_bands[band].gainDb.load(std::memory_order_relaxed);
        const double frequency = m_bands[band].frequency.load(std::memory_order_relaxed);
        const double q = m_bands[band].q.load(std::memory_order_relaxed);

        Cell &cell = m_cells[band];

        // Guadagno nullo: la cella passa tutto. Ricavarlo dalla formula darebbe
        // lo stesso risultato, ma con cinque celle sempre in cascata conviene
        // che la strada trasparente sia anche la più corta.
        if (std::abs(gainDb) < 1e-4) {
            cell.b0 = 1.0;
            cell.b1 = cell.b2 = cell.a1 = cell.a2 = 0.0;
            continue;
        }

        const double amplitude = std::pow(10.0, gainDb / 40.0);
        const double omega = 2.0 * M_PI * frequency / m_sampleRate;
        const double sn = std::sin(omega);
        const double cs = std::cos(omega);
        const double alpha = sn / (2.0 * q);

        const double b0 = 1.0 + alpha * amplitude;
        const double b1 = -2.0 * cs;
        const double b2 = 1.0 - alpha * amplitude;
        const double a0 = 1.0 + alpha / amplitude;
        const double a1 = -2.0 * cs;
        const double a2 = 1.0 - alpha / amplitude;

        cell.b0 = b0 / a0;
        cell.b1 = b1 / a0;
        cell.b2 = b2 / a0;
        cell.a1 = a1 / a0;
        cell.a2 = a2 / a0;
    }
}

double ParametricEq::responseDb(double frequencyHz) const noexcept
{
    if (!(m_sampleRate > 0.0))
        return 0.0;

    // Dai coefficienti e non da una seconda formula: se un giorno la campana
    // cambia, la curva cambia con lei invece di restare quella di prima. Una
    // curva che mostra un filtro diverso da quello che si sente è peggio di
    // nessuna curva.
    const double omega = 2.0 * M_PI * frequencyHz / m_sampleRate;
    const std::complex<double> z = std::polar(1.0, -omega);
    const std::complex<double> zz = z * z;

    std::complex<double> total{1.0, 0.0};
    for (const Cell &cell : m_cells) {
        const std::complex<double> numerator = cell.b0 + cell.b1 * z + cell.b2 * zz;
        const std::complex<double> denominator = 1.0 + cell.a1 * z + cell.a2 * zz;
        if (std::abs(denominator) < 1e-12)
            continue;
        total *= numerator / denominator;
    }

    return 20.0 * std::log10(std::max(std::abs(total), 1e-9));
}

void ParametricEq::process(float *audio, std::size_t frames) noexcept
{
    if (!m_configured || frames == 0)
        return;

    // I coefficienti si rifanno qui, sul thread audio, e solo quando qualcosa
    // è cambiato: costa qualche decina di operazioni per cinque celle, una
    // volta ogni tanto, e toglie di mezzo il problema di pubblicare in modo
    // coerente cinque terne di numeri da un thread all'altro.
    if (m_dirty.exchange(false, std::memory_order_acquire))
        redesign();

    if (!m_enabled.load(std::memory_order_relaxed))
        return;

    for (Cell &cell : m_cells) {
        // Una cella trasparente non si attraversa: con cinque celle quasi
        // sempre a zero, saltarle è la differenza fra cinque moltiplicazioni
        // per campione e nessuna.
        if (cell.b0 == 1.0 && cell.b1 == 0.0 && cell.b2 == 0.0
            && cell.a1 == 0.0 && cell.a2 == 0.0) {
            continue;
        }

        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (int channel = 0; channel < m_channels; ++channel) {
                const std::size_t at = frame * static_cast<std::size_t>(m_channels)
                    + static_cast<std::size_t>(channel);
                const double in = static_cast<double>(audio[at]);

                // Diretta II trasposta: due stati per canale, e nessun accumulo
                // di errore che cresca con il tempo.
                const double out = cell.b0 * in + cell.z1[static_cast<std::size_t>(channel)];
                cell.z1[static_cast<std::size_t>(channel)] =
                    cell.b1 * in - cell.a1 * out + cell.z2[static_cast<std::size_t>(channel)];
                cell.z2[static_cast<std::size_t>(channel)] = cell.b2 * in - cell.a2 * out;

                audio[at] = static_cast<float>(out);
            }
        }
    }
}

} // namespace dsdr::dsp
