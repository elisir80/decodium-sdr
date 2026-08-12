// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — limiter, l'ultimo stadio della catena TX (SPEC-005 §4.1).
//
// L'unico che non deve mai lasciar passare niente oltre il tetto. Tutto quello
// che viene prima — leveller, compressore, multibanda — lavora sul suono; il
// limiter lavora sul fondo scala, che non e' una questione di gusto: oltre, il
// modulatore tosa, e tosare in banda base vuol dire allargare il segnale sulle
// frequenze dei vicini.
//
// Con anticipo, e non e' un lusso. Un limiter che reagisce quando la punta e'
// gia' arrivata la lascia passare per il tempo del suo attacco; guardando
// avanti di un paio di millisecondi, il guadagno e' gia' sceso quando la punta
// arriva. Il prezzo e' un ritardo di quei due millisecondi, che su una voce
// non si sente e su un TX non conta.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace dsdr::dsp {

class Limiter
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    /// Il tetto in dBFS. Meno uno lascia un decibel di margine ai
    /// ricampionatori che vengono dopo, che possono superare di poco il picco
    /// che hanno ricevuto.
    void setCeilingDb(double db) noexcept;
    double ceilingDb() const noexcept { return m_ceilingDb.load(std::memory_order_relaxed); }

    /// Di quanto sta tenendo giu', in decibel. Un limiter che lavora sempre
    /// dice che il drive e' troppo alto: e' un rimedio, non un modo di
    /// guadagnare potenza.
    double reductionDb() const noexcept { return m_reduction.load(std::memory_order_relaxed); }

    /// Il ritardo che introduce, in millisecondi. Va dichiarato: chi lo somma
    /// alla latenza della catena deve poterlo leggere.
    double latencyMs() const noexcept;

    void process(float *audio, std::size_t frames) noexcept;

private:
    double m_sampleRate = 0.0;
    bool m_configured = false;

    double m_releaseCoeff = 0.0;

    /// La linea di ritardo dell'anticipo. Circolare, dimensionata una volta.
    std::vector<float> m_delay;
    std::size_t m_write = 0;
    std::size_t m_lookahead = 0;

    double m_gainDb = 0.0;

    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_ceilingDb{-1.0};
    std::atomic<double> m_reduction{0.0};
};

} // namespace dsdr::dsp
