// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — manipolatore morse per il backend demo.
//
// Produce l'inviluppo di manipolazione di un testo CW, con fronti a coseno
// rialzato: senza sagomatura i click di manipolazione si spalmerebbero su
// tutta la banda e il waterfall della demo mostrerebbe artefatti che una
// stazione vera non ha.
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

namespace dsdr::hal::demo {

class MorseKeyer
{
public:
    void configure(double sampleRate, double wordsPerMinute = 22.0);

    /// Imposta il testo trasmesso in ciclo continuo.
    void setText(const QString &text);

    void reset() noexcept;

    /// Inviluppo del prossimo campione, 0.0–1.0.
    float nextEnvelope() noexcept;

private:
    void rebuild();

    struct Element
    {
        bool keyDown;
        int dits; ///< durata in unità di punto
    };

    QString m_text;
    std::vector<Element> m_elements;

    double m_sampleRate = 48000.0;
    double m_wpm = 22.0;
    std::size_t m_ditSamples = 0;
    std::size_t m_rampSamples = 0;

    std::size_t m_index = 0;
    std::size_t m_positionInElement = 0;
    std::size_t m_elementSamples = 0;
    float m_currentLevel = 0.0f;
};

} // namespace dsdr::hal::demo
