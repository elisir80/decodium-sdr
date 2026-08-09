// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — soppressore di rumore impulsivo a banda larga (SPEC-003 §4.1).
//
// Le scariche — accensioni, recinti elettrici, alimentatori switching — non
// sono rumore diffuso: sono impulsi brevissimi e di ampiezza enorme. Un AGC li
// prende per segnale e abbassa il guadagno di tutto il resto, così ogni
// scarica si porta via anche mezzo secondo di ascolto.
//
// Lavora sul flusso a banda piena, **prima della decimazione**: un impulso è
// largo quanto tutta la banda, e dopo un filtro stretto è già diventato una
// coda di millisecondi — a quel punto toglierlo significa bucare il segnale
// insieme al disturbo. Per lo stesso motivo sta nel motore e non nel canale:
// l'impulso è dell'ambiente, e ripulirlo una volta vale per tutti i ricevitori.
//
// Tre scelte che distinguono un blanker utile da uno che rovina l'ascolto:
//
//   · **mediana e non media** come livello di riferimento: la media la alzano
//     gli impulsi stessi, e dopo tre scariche la soglia non scatta più;
//   · **interpolazione e non azzeramento**: uno zero è a sua volta un gradino,
//     e un gradino è a banda larga quanto l'impulso che si voleva togliere;
//   · **un limite di durata**: oltre mezzo millisecondo non è un impulso, è un
//     segnale forte — e il blanker si ritira invece di cancellarlo.
#pragma once

#include "dsp/DspTypes.h"

#include <cstddef>
#include <vector>

namespace dsdr::dsp {

class NoiseBlanker
{
public:
    void configure(double sampleRate);
    void reset() noexcept;

    /// Quanto sopra il livello tipico far scattare la soppressione, in
    /// multipli della mediana. Sotto 2 comincia a mordere il segnale, sopra 8
    /// non scatta quasi mai (SPEC-003 §4.1: default 4, campo 2–8).
    void setThreshold(double factor) noexcept;
    double threshold() const noexcept { return m_threshold; }

    /// Elabora in place e restituisce quanti campioni sono stati sostituiti.
    std::size_t process(Complex *iq, std::size_t n) noexcept;

    /// Quota di campioni sostituiti nell'ultimo blocco, 0..1. È la spia che
    /// distingue un blanker che sta lavorando da uno che sta tagliuzzando.
    float lastSuppressedRatio() const noexcept { return m_lastRatio; }

    /// Eventi troppo lunghi per essere impulsi, incontrati nell'ultimo blocco:
    /// se questo numero cresce, la soglia è troppo bassa per la banda di
    /// adesso e il blanker si sta ritirando in continuazione.
    std::size_t lastRefusedEvents() const noexcept { return m_lastRefused; }

private:
    /// Livello tipico inseguito per mediana: sale e scende di un passo fisso a
    /// ogni campione, così un impulso — per quanto violento — lo sposta come
    /// un campione qualunque.
    void trackMedian(float magnitude) noexcept;

    double m_threshold = 4.0;
    float m_median = 0.0f;
    float m_step = 0.0f;          ///< passo dell'inseguimento, per campione
    int m_maxBlank = 8;           ///< durata massima di un evento, in campioni
    int m_margin = 2;             ///< campioni tolti anche attorno all'impulso
    float m_lastRatio = 0.0f;
    std::size_t m_lastRefused = 0;
    bool m_primed = false;
    bool m_configured = false;

    /// Magnitudini del blocco: servono due passate — prima si riconoscono gli
    /// eventi, poi si interpola fra i campioni sani che li circondano.
    std::vector<float> m_magnitude;
    std::vector<bool> m_blank;
};

} // namespace dsdr::dsp
