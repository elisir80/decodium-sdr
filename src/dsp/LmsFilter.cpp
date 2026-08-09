// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/LmsFilter.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

namespace {

/// Inseguimento della potenza del segnale, usata per normalizzare il passo.
constexpr float kPowerAlpha = 0.001f;

/// Perdita applicata ai coefficienti a ogni campione. Senza, in assenza di
/// segnale i pesi vagano e il filtro si inventa una risposta che poi colora
/// l'audio quando il segnale torna.
constexpr float kLeakage = 0.9999f;

} // namespace

void LmsFilter::configure(int taps, int delay)
{
    m_taps = std::clamp(taps, 4, 256);
    m_delay = std::clamp(delay, 1, 128);

    m_weights.assign(static_cast<std::size_t>(m_taps), 0.0f);
    m_history.assign(static_cast<std::size_t>(m_taps + m_delay + 1), 0.0f);
    m_write = 0;
    m_power = 1e-6f;
}

void LmsFilter::reset() noexcept
{
    std::fill(m_weights.begin(), m_weights.end(), 0.0f);
    std::fill(m_history.begin(), m_history.end(), 0.0f);
    m_write = 0;
    m_power = 1e-6f;
}

void LmsFilter::setRate(float rate) noexcept
{
    m_rate = std::clamp(rate, 0.001f, 0.5f);
}

void LmsFilter::process(float *audio, std::size_t n, Output output) noexcept
{
    if (m_taps <= 0 || audio == nullptr || n == 0)
        return;

    const std::size_t size = m_history.size();

    for (std::size_t k = 0; k < n; ++k) {
        const float x = audio[k];

        m_history[m_write] = x;
        m_write = (m_write + 1) % size;

        // Previsione a partire da `delay` campioni fa: il ritardo è ciò che
        // impedisce al filtro di "prevedere" il rumore copiando il campione
        // appena passato.
        float y = 0.0f;
        for (int j = 0; j < m_taps; ++j) {
            const std::size_t index =
                (m_write + size - static_cast<std::size_t>(m_delay + j + 1)) % size;
            y += m_weights[static_cast<std::size_t>(j)] * m_history[index];
        }

        const float error = x - y;

        // Passo normalizzato: quello che conta è l'errore *relativo* alla
        // potenza del segnale, altrimenti la stessa impostazione è inerte sul
        // debole e instabile sul forte.
        m_power += (x * x - m_power) * kPowerAlpha;
        const float step = m_rate * error / (m_power * static_cast<float>(m_taps) + 1e-6f);

        for (int j = 0; j < m_taps; ++j) {
            const std::size_t index =
                (m_write + size - static_cast<std::size_t>(m_delay + j + 1)) % size;
            auto &w = m_weights[static_cast<std::size_t>(j)];
            w = w * kLeakage + step * m_history[index];
        }

        audio[k] = (output == Output::Prediction) ? y : error;
    }
}

} // namespace dsdr::dsp
