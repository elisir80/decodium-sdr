// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/NoiseBlanker.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Finestra dell'inseguimento del livello tipico: ~10 ms (SPEC-003 §4.1).
/// Deve essere lenta rispetto a un impulso — microsecondi — e veloce rispetto
/// al fading, che si misura in secondi.
constexpr double kMedianSeconds = 0.01;

/// Durata massima di un evento: oltre, non è un impulso.
constexpr double kMaxBlankSeconds = 0.0005;   // 500 µs

/// Quanti campioni si tolgono anche prima e dopo l'impulso riconosciuto: il
/// filtro anti-alias del device lo ha già allargato di qualche campione, e
/// lasciarne i bordi vuol dire lasciare il "clic".
constexpr int kMargin = 2;

/// Blocco massimo che il motore consegna in una volta.
constexpr std::size_t kMaxBlock = 1 << 16;

} // namespace

void NoiseBlanker::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    // Il passo dell'inseguimento è una frazione del livello per campione: con
    // finestra di 10 ms la mediana attraversa l'intero campo utile in quel
    // tempo, indipendentemente dal ritmo di campionamento.
    m_step = static_cast<float>(1.0 / (kMedianSeconds * sampleRate));
    m_step = std::clamp(m_step, 1e-6f, 0.5f);

    m_maxBlank = std::max(1, static_cast<int>(std::lround(kMaxBlankSeconds * sampleRate)));
    m_margin = kMargin;

    m_magnitude.assign(kMaxBlock, 0.0f);
    m_blank.assign(kMaxBlock, false);

    m_configured = true;
    reset();
}

void NoiseBlanker::reset() noexcept
{
    m_median = 0.0f;
    m_primed = false;
    m_lastRatio = 0.0f;
    m_lastRefused = 0;
}

void NoiseBlanker::setThreshold(double factor) noexcept
{
    m_threshold = std::clamp(factor, 2.0, 8.0);
}

void NoiseBlanker::trackMedian(float magnitude) noexcept
{
    // Inseguimento per segno: la stima sale o scende sempre dello stesso
    // passo, quindi converge alla mediana e non alla media. Una scarica cento
    // volte più alta del fondo la sposta esattamente quanto la sposterebbe un
    // campione appena sopra: è ciò che le impedisce di nascondersi dietro la
    // propria statistica.
    const float delta = m_median * m_step;
    if (magnitude > m_median)
        m_median += delta;
    else
        m_median -= delta;

    m_median = std::max(m_median, 1e-9f);
}

std::size_t NoiseBlanker::process(Complex *iq, std::size_t n) noexcept
{
    if (!m_configured || iq == nullptr || n == 0)
        return 0;

    n = std::min(n, m_magnitude.size());
    m_lastRefused = 0;

    // ── Prima passata: riconoscere gli eventi ───────────────────────────
    for (std::size_t i = 0; i < n; ++i) {
        m_magnitude[i] = std::sqrt(magnitudeSquared(iq[i]));
        m_blank[i] = false;

        if (!m_primed) {
            m_median = std::max(m_magnitude[i], 1e-9f);
            m_primed = true;
        }
    }

    const auto threshold = static_cast<float>(m_threshold);
    std::size_t i = 0;
    while (i < n) {
        const float limit = m_median * threshold;
        if (m_magnitude[i] <= limit) {
            trackMedian(m_magnitude[i]);
            ++i;
            continue;
        }

        // Un evento è cominciato: si guarda quanto dura prima di decidere che
        // farne. La mediana non si aggiorna qui dentro — sarebbe come chiedere
        // al disturbo di stabilire quanto è normale.
        std::size_t end = i;
        while (end < n && m_magnitude[end] > limit)
            ++end;

        const std::size_t length = end - i;
        if (length > static_cast<std::size_t>(m_maxBlank)) {
            // Troppo lungo per essere un impulso: è un segnale forte, e
            // cancellarlo sarebbe il difetto storico dei blanker aggressivi —
            // la stazione locale che sparisce appena si accende il NB.
            ++m_lastRefused;
            for (std::size_t k = i; k < end; ++k)
                trackMedian(m_magnitude[k]);
            i = end;
            continue;
        }

        const std::size_t from = (i > static_cast<std::size_t>(m_margin))
                                     ? i - static_cast<std::size_t>(m_margin)
                                     : 0;
        const std::size_t to = std::min(n, end + static_cast<std::size_t>(m_margin));
        for (std::size_t k = from; k < to; ++k)
            m_blank[k] = true;

        i = end;
    }

    // ── Seconda passata: ricucire ───────────────────────────────────────
    std::size_t suppressed = 0;
    std::size_t k = 0;
    while (k < n) {
        if (!m_blank[k]) {
            ++k;
            continue;
        }

        std::size_t end = k;
        while (end < n && m_blank[end])
            ++end;

        // Si interpola fra i campioni sani che stanno ai due lati. Ai bordi
        // del blocco uno dei due può mancare: allora si prolunga quello che
        // c'è, che è comunque più liscio di un salto a zero.
        const bool hasLeft = k > 0;
        const bool hasRight = end < n;
        const Complex left = hasLeft ? iq[k - 1] : (hasRight ? iq[end] : Complex(0.0f, 0.0f));
        const Complex right = hasRight ? iq[end] : left;

        const auto span = static_cast<float>(end - k + 1);
        for (std::size_t j = k; j < end; ++j) {
            const float t = static_cast<float>(j - k + 1) / span;
            iq[j] = Complex(left.real() + (right.real() - left.real()) * t,
                            left.imag() + (right.imag() - left.imag()) * t);
            ++suppressed;
        }

        k = end;
    }

    m_lastRatio = static_cast<float>(suppressed) / static_cast<float>(n);
    return suppressed;
}

} // namespace dsdr::dsp
