// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — filtro di picco audio (APF), per la CW (SPEC-003 §7).
//
// È il gemello opposto del notch: invece di scavare una fenditura, alza una
// campana stretta attorno alla nota che si sta copiando. Non sostituisce il
// filtro di canale — quello lavora in RF e taglia le stazioni vicine — ma
// aggiunge selettività *dentro* il passa-banda, dove restano il fruscio e le
// note fuori tono.
//
// L'orecchio ci guadagna due volte: il segnale emerge, e tutto ciò che non è
// alla frequenza giusta si allontana. Con Q alti diventa un filtro «a
// campana» molto stretto: suona innaturale sul parlato — per questo esiste
// solo in CW — ma su una nota tenuta è esattamente ciò che serve.
#pragma once

#include <cstddef>

namespace dsdr::dsp {

class PeakFilter
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    /// Frequenza della campana e fattore di merito.
    ///
    /// Q basso allarga e lascia respirare; Q alto stringe fino a far
    /// «cantare» il filtro. Il campo utile per la CW è 5–50: sotto non si
    /// sente la differenza, sopra il filtro suona da sé sul rumore.
    void setPeak(double frequencyHz, double q) noexcept;

    double frequencyHz() const noexcept { return m_frequency; }
    double q() const noexcept { return m_q; }

    void process(float *audio, std::size_t n) noexcept;

private:
    void redesign() noexcept;

    double m_sampleRate = 0.0;
    double m_frequency = 600.0;
    double m_q = 12.0;

    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;

    bool m_configured = false;
};

} // namespace dsdr::dsp
