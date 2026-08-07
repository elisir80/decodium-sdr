// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/demo/MorseKeyer.h"

#include <algorithm>
#include <cmath>

namespace dsdr::hal::demo {
namespace {

// Tabella morse minima: lettere, cifre e i segni che compaiono nei nostri testi.
struct MorseEntry
{
    char ch;
    const char *code;
};

constexpr MorseEntry kMorseTable[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
    {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
    {'Z', "--.."},  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
    {'9', "----."}, {'/', "-..-."}, {'?', "..--.."},{'.', ".-.-.-"},{',', "--..--"},
    {'-', "-....-"},{'=', "-...-"},
};

const char *codeFor(char ch)
{
    for (const MorseEntry &e : kMorseTable) {
        if (e.ch == ch)
            return e.code;
    }
    return nullptr;
}

} // namespace

void MorseKeyer::configure(double sampleRate, double wordsPerMinute)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_wpm = std::clamp(wordsPerMinute, 5.0, 60.0);

    // PARIS standard: un punto dura 1.2/WPM secondi.
    m_ditSamples = static_cast<std::size_t>(1.2 / m_wpm * m_sampleRate);
    m_ditSamples = std::max<std::size_t>(m_ditSamples, 1);

    // Fronti da 5 ms: sopprimono i click restando ben dentro il punto.
    m_rampSamples = static_cast<std::size_t>(0.005 * m_sampleRate);
    m_rampSamples = std::min(m_rampSamples, m_ditSamples / 3);

    rebuild();
}

void MorseKeyer::setText(const QString &text)
{
    m_text = text.toUpper();
    rebuild();
}

void MorseKeyer::rebuild()
{
    m_elements.clear();
    if (m_text.isEmpty() || m_ditSamples == 0) {
        m_elements.push_back({false, 1});
        reset();
        return;
    }

    for (const QChar qc : m_text) {
        const char ch = qc.toLatin1();
        if (ch == ' ') {
            // Spazio fra parole: 7 dit totali, 3 già presenti dalla lettera.
            if (!m_elements.empty())
                m_elements.back().dits += 4;
            else
                m_elements.push_back({false, 7});
            continue;
        }

        const char *code = codeFor(ch);
        if (!code)
            continue;

        for (const char *p = code; *p; ++p) {
            m_elements.push_back({true, (*p == '-') ? 3 : 1});
            m_elements.push_back({false, 1}); // spazio fra elementi
        }
        if (!m_elements.empty())
            m_elements.back().dits = 3; // spazio fra lettere
    }

    if (m_elements.empty())
        m_elements.push_back({false, 1});

    reset();
}

void MorseKeyer::reset() noexcept
{
    m_index = 0;
    m_positionInElement = 0;
    m_currentLevel = 0.0f;
    m_elementSamples = m_elements.empty()
        ? 0
        : static_cast<std::size_t>(m_elements[0].dits) * m_ditSamples;
}

float MorseKeyer::nextEnvelope() noexcept
{
    if (m_elements.empty())
        return 0.0f;

    const Element &element = m_elements[m_index];
    const float target = element.keyDown ? 1.0f : 0.0f;

    // Rampa a coseno rialzato verso il livello dell'elemento corrente.
    if (m_rampSamples > 0 && m_currentLevel != target) {
        const float step = 1.0f / static_cast<float>(m_rampSamples);
        m_currentLevel += (target > m_currentLevel) ? step : -step;
        m_currentLevel = std::clamp(m_currentLevel, 0.0f, 1.0f);
    } else {
        m_currentLevel = target;
    }

    if (++m_positionInElement >= m_elementSamples) {
        m_positionInElement = 0;
        m_index = (m_index + 1) % m_elements.size();
        m_elementSamples =
            static_cast<std::size_t>(m_elements[m_index].dits) * m_ditSamples;
    }

    // Coseno rialzato sul livello lineare: fronti dolci, spettro pulito.
    const float shaped = 0.5f - 0.5f * std::cos(m_currentLevel * 3.14159265f);
    return shaped;
}

} // namespace dsdr::hal::demo
