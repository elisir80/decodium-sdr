// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/NoiseGate.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Un millisecondo per aprirsi: l'attacco di una consonante dura meno di
/// questo, e aprirsi più piano vuol dire mangiarselo.
constexpr double kAttackMs = 1.0;

/// Centoventi millisecondi di tenuta: è la pausa più lunga che ci sta dentro
/// una frase — fra due sillabe, o dopo una occlusiva — e chiudersi lì produce
/// un buco in mezzo a una parola.
constexpr double kHoldMs = 120.0;

/// Duecento per chiudersi. Più veloce si sente il taglio della coda di ogni
/// parola, che dà più fastidio del rumore che si voleva togliere.
constexpr double kReleaseMs = 200.0;

} // namespace

void NoiseGate::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    m_sampleRate = sampleRate;
    m_attackCoeff = std::exp(-1.0 / (sampleRate * kAttackMs / 1000.0));
    m_releaseCoeff = std::exp(-1.0 / (sampleRate * kReleaseMs / 1000.0));
    m_holdFrames = static_cast<std::size_t>(sampleRate * kHoldMs / 1000.0);
    m_configured = true;
    reset();
}

void NoiseGate::reset() noexcept
{
    m_envelope = 0.0;
    m_gain = 0.0;
    m_held = 0;
    m_opening.store(0.0, std::memory_order_relaxed);
}

void NoiseGate::setThresholdDb(double db) noexcept
{
    m_thresholdDb.store(std::clamp(db, -80.0, -10.0), std::memory_order_relaxed);
}

void NoiseGate::process(float *audio, std::size_t frames) noexcept
{
    if (!m_configured || frames == 0)
        return;

    if (!m_enabled.load(std::memory_order_relaxed)) {
        // Spento vuol dire aperto: uscendo dal bypass il gate non deve
        // richiudersi per duecento millisecondi su una voce che sta parlando.
        m_gain = 1.0;
        m_opening.store(1.0, std::memory_order_relaxed);
        return;
    }

    const double threshold = std::pow(10.0, m_thresholdDb.load(std::memory_order_relaxed) / 20.0);

    for (std::size_t i = 0; i < frames; ++i) {
        const double magnitude = std::abs(static_cast<double>(audio[i]));

        // Rivelatore di picco veloce: qui interessa il fatto che ci sia del
        // segnale, non quanto vale con precisione.
        m_envelope = std::max(magnitude, m_envelope * m_releaseCoeff);

        if (m_envelope >= threshold)
            m_held = m_holdFrames;
        else if (m_held > 0)
            --m_held;

        const double target = (m_envelope >= threshold || m_held > 0) ? 1.0 : 0.0;
        const double coeff = target > m_gain ? m_attackCoeff : m_releaseCoeff;
        m_gain = target + coeff * (m_gain - target);

        audio[i] = static_cast<float>(static_cast<double>(audio[i]) * m_gain);
    }

    m_opening.store(m_gain, std::memory_order_relaxed);
}

} // namespace dsdr::dsp
