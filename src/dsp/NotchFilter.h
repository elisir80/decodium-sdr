// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — notch manuale sull'audio.
//
// L'ANF toglie le righe che trova da sé; questo toglie quella che dice
// l'operatore. Serve quando il notch automatico non basta o dà fastidio — per
// esempio in CW, dove la nota che si vuole ascoltare *è* una riga fissa e
// l'ANF la porterebbe via insieme al disturbo.
//
// Biquad RBJ in forma diretta II trasposta: due poli, due zeri, coefficienti
// che si ricalcolano solo quando l'operatore muove la manopola.
#pragma once

#include <cstddef>

namespace dsdr::dsp {

class NotchFilter
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    /// Frequenza da togliere e larghezza della fenditura, in hertz. Una
    /// fenditura stretta lascia intatto il parlato attorno; una larga toglie
    /// anche le armoniche del disturbo, ma si sente come un buco.
    void setNotch(double frequencyHz, double widthHz) noexcept;

    double frequencyHz() const noexcept { return m_frequency; }
    double widthHz() const noexcept { return m_width; }

    void process(float *audio, std::size_t n) noexcept;

private:
    void redesign() noexcept;

    double m_sampleRate = 0.0;
    double m_frequency = 1000.0;
    double m_width = 120.0;

    // Coefficienti normalizzati su a0.
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;

    bool m_configured = false;
};

} // namespace dsdr::dsp
