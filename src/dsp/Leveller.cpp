// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/Leveller.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Duecento millisecondi per scendere, un secondo e mezzo per salire.
///
/// Sono lenti apposta: questo stadio insegue la distanza dal microfono, non le
/// consonanti. Con costanti veloci farebbe il lavoro del compressore, e lo
/// farebbe male — due stadi che inseguono la stessa cosa si rincorrono, e il
/// risultato pompa.
constexpr double kAttackMs = 200.0;
constexpr double kReleaseMs = 1500.0;

/// Il tetto del guadagno. Senza, nelle pause l'AGC alza finche' il rumore di
/// fondo arriva al livello della voce.
constexpr double kMaxGainDb = 20.0;

/// Sotto questo livello non si insegue niente: e' silenzio, e inseguirlo vuol
/// dire alzare la stanza.
constexpr double kFloorDb = -55.0;

} // namespace

void Leveller::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    m_sampleRate = sampleRate;
    m_attackCoeff = std::exp(-1.0 / (sampleRate * kAttackMs / 1000.0));
    m_releaseCoeff = std::exp(-1.0 / (sampleRate * kReleaseMs / 1000.0));
    m_configured = true;
    reset();
}

void Leveller::reset() noexcept
{
    m_envelope = 0.0;
    m_currentGainDb = 0.0;
    m_gainDb.store(0.0, std::memory_order_relaxed);
}

void Leveller::setTargetDb(double db) noexcept
{
    m_targetDb.store(std::clamp(db, -40.0, -6.0), std::memory_order_relaxed);
}

void Leveller::process(float *audio, std::size_t frames) noexcept
{
    if (!m_configured || frames == 0)
        return;

    if (!m_enabled.load(std::memory_order_relaxed)) {
        m_currentGainDb = 0.0;
        m_gainDb.store(0.0, std::memory_order_relaxed);
        return;
    }

    const double target = m_targetDb.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < frames; ++i) {
        const double magnitude = std::abs(static_cast<double>(audio[i]));
        const double coeff = magnitude > m_envelope ? m_attackCoeff : m_releaseCoeff;
        m_envelope = magnitude + coeff * (m_envelope - magnitude);

        const double levelDb = 20.0 * std::log10(std::max(m_envelope, 1e-7));

        // Nel silenzio il guadagno resta dov'e': non sale a cercare qualcosa
        // che non c'e', e non scende a dimenticare quello che aveva imparato.
        if (levelDb > kFloorDb) {
            const double wanted = std::clamp(target - levelDb, -kMaxGainDb, kMaxGainDb);
            const double gainCoeff = wanted < m_currentGainDb ? m_attackCoeff : m_releaseCoeff;
            m_currentGainDb = wanted + gainCoeff * (m_currentGainDb - wanted);
        }

        audio[i] = static_cast<float>(static_cast<double>(audio[i])
                                      * std::pow(10.0, m_currentGainDb / 20.0));
    }

    m_gainDb.store(m_currentGainDb, std::memory_order_relaxed);
}

} // namespace dsdr::dsp
