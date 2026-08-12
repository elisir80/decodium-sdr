// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/Limiter.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Due millisecondi di anticipo: bastano perche' il guadagno sia gia' sceso
/// quando la punta arriva, e non si sentono.
constexpr double kLookaheadMs = 2.0;

/// Cinquanta millisecondi per risalire. Piu' veloce e la voce pompa; piu'
/// lento e una sola punta tiene giu' la frase che segue.
constexpr double kReleaseMs = 50.0;

} // namespace

void Limiter::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    m_sampleRate = sampleRate;
    m_lookahead = std::max<std::size_t>(1, static_cast<std::size_t>(
        sampleRate * kLookaheadMs / 1000.0));
    // Alloca qui e mai piu': il percorso caldo non alloca (CONSTITUTION §5).
    m_delay.assign(m_lookahead + 1, 0.0f);
    m_releaseCoeff = std::exp(-1.0 / (sampleRate * kReleaseMs / 1000.0));
    m_configured = true;
    reset();
}

void Limiter::reset() noexcept
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
    m_write = 0;
    m_gainDb = 0.0;
    m_reduction.store(0.0, std::memory_order_relaxed);
}

void Limiter::setCeilingDb(double db) noexcept
{
    m_ceilingDb.store(std::clamp(db, -12.0, 0.0), std::memory_order_relaxed);
}

double Limiter::latencyMs() const noexcept
{
    if (!(m_sampleRate > 0.0))
        return 0.0;
    return 1000.0 * static_cast<double>(m_lookahead) / m_sampleRate;
}

void Limiter::process(float *audio, std::size_t frames) noexcept
{
    if (!m_configured || frames == 0 || m_delay.empty())
        return;

    if (!m_enabled.load(std::memory_order_relaxed)) {
        m_reduction.store(0.0, std::memory_order_relaxed);
        return;
    }

    const double ceiling = m_ceilingDb.load(std::memory_order_relaxed);
    const double ceilingLinear = std::pow(10.0, ceiling / 20.0);
    double worst = 0.0;

    for (std::size_t i = 0; i < frames; ++i) {
        const double incoming = static_cast<double>(audio[i]);

        // Il campione entra nella linea di ritardo e ne esce quello di due
        // millisecondi fa: il guadagno che gli si applica e' gia' stato
        // deciso guardando lui.
        m_delay[m_write] = static_cast<float>(incoming);
        m_write = (m_write + 1) % m_delay.size();
        const double delayed = static_cast<double>(m_delay[m_write]);

        // Il guadagno necessario per il campione che sta *arrivando*.
        const double magnitude = std::abs(incoming);
        double wantedDb = 0.0;
        if (magnitude > ceilingLinear) {
            wantedDb = 20.0 * std::log10(ceilingLinear / magnitude);
        }

        // Scende subito, risale piano: e' quello che distingue un limiter da
        // un compressore veloce.
        if (wantedDb < m_gainDb)
            m_gainDb = wantedDb;
        else
            m_gainDb = wantedDb + m_releaseCoeff * (m_gainDb - wantedDb);

        worst = std::min(worst, m_gainDb);
        audio[i] = static_cast<float>(delayed * std::pow(10.0, m_gainDb / 20.0));
    }

    m_reduction.store(-worst, std::memory_order_relaxed);
}

} // namespace dsdr::dsp
