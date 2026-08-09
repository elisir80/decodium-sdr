// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/OverloadGuard.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Durata di una finestra di osservazione (SPEC-003 §3).
constexpr double kWindowSeconds = 0.1;

/// Sopra questa soglia l'ADC sta per andare in saturazione.
constexpr float kHotDbfs = -1.0f;

/// Sotto questa, c'è margine abbondante e si può restituire guadagno.
constexpr float kCoolDbfs = -12.0f;

/// Finestre consecutive in saturazione prima di intervenire: tre decimi di
/// secondo. Un picco isolato — una scarica, un'apertura di portante — non deve
/// far muovere niente.
constexpr int kHotWindowsToAct = 3;

/// Quanto si toglie in una volta, e quanto si restituisce. Si scende in
/// fretta e si risale piano: risalire troppo presto rimette in saturazione, e
/// l'operatore vede il guadagno ballare senza capire perché.
constexpr double kReduceStepDb = 6.0;
constexpr double kIncreaseStepDb = 3.0;

/// Quiete richiesta prima di restituire guadagno.
constexpr double kCoolSeconds = 30.0;

/// Riduzione massima accumulabile: oltre, il problema non è il guadagno.
constexpr double kMaxReductionDb = 30.0;

} // namespace

void OverloadGuard::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    m_windowSamples = static_cast<std::size_t>(kWindowSeconds * sampleRate);
    m_windowSamples = std::max<std::size_t>(m_windowSamples, 1);
    m_coolWindowsNeeded = std::max(1, static_cast<int>(kCoolSeconds / kWindowSeconds));
    reset();
}

void OverloadGuard::reset() noexcept
{
    m_windowCount = 0;
    m_windowPeak = 0.0f;
    m_lastPeakDb = -160.0f;
    m_overloaded = false;
    m_hotWindows = 0;
    m_coolWindows = 0;
    m_reductionDb = 0.0;
    m_pendingDb = 0.0;
    m_interventions = 0;
}

void OverloadGuard::feed(const Complex *iq, std::size_t n) noexcept
{
    if (m_mode == Mode::Off || m_windowSamples == 0 || iq == nullptr || n == 0)
        return;

    for (std::size_t i = 0; i < n; ++i) {
        // Il picco è sul modulo, non sulle singole componenti: un vettore IQ
        // a 45 gradi satura il convertitore prima che I o Q da soli tocchino
        // il fondo scala.
        m_windowPeak = std::max(m_windowPeak, magnitudeSquared(iq[i]));

        if (++m_windowCount >= m_windowSamples)
            closeWindow();
    }
}

void OverloadGuard::closeWindow() noexcept
{
    const float peak = std::sqrt(m_windowPeak);
    m_lastPeakDb = 20.0f * std::log10(std::max(peak, 1e-8f));

    m_windowCount = 0;
    m_windowPeak = 0.0f;

    m_overloaded = m_lastPeakDb > kHotDbfs;

    if (m_overloaded) {
        m_coolWindows = 0;
        if (++m_hotWindows >= kHotWindowsToAct) {
            m_hotWindows = 0;
            if (m_mode == Mode::Auto && m_reductionDb < kMaxReductionDb) {
                m_pendingDb -= kReduceStepDb;
                m_reductionDb = std::min(kMaxReductionDb, m_reductionDb + kReduceStepDb);
                ++m_interventions;
            }
        }
        return;
    }

    m_hotWindows = 0;

    if (m_lastPeakDb >= kCoolDbfs) {
        // Zona di mezzo: né saturazione né margine da restituire. È dove si
        // vuole stare, e dove la guardia non deve fare niente.
        m_coolWindows = 0;
        return;
    }

    if (++m_coolWindows < m_coolWindowsNeeded)
        return;

    m_coolWindows = 0;
    if (m_mode != Mode::Auto || m_reductionDb <= 0.0)
        return;

    // Si restituisce solo ciò che si era tolto: il livello impostato
    // dall'operatore è il tetto, e la guardia non lo scavalca nemmeno quando
    // la banda è vuota.
    const double give = std::min(kIncreaseStepDb, m_reductionDb);
    m_pendingDb += give;
    m_reductionDb -= give;
    ++m_interventions;
}

double OverloadGuard::takeRequestDb() noexcept
{
    const double pending = m_pendingDb;
    m_pendingDb = 0.0;
    return pending;
}

} // namespace dsdr::dsp
