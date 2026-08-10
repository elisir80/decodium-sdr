// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — inviluppo della manipolazione CW.
//
// Un tasto che accende e spegne la portante di netto produce un gradino, e un
// gradino ha uno spettro che non finisce mai: sono i *click* che si sentono
// per chilohertz attorno a chi manipola. La cura è antica e sta tutta qui —
// far salire e scendere l'ampiezza con un bordo a coseno rialzato, che occupa
// una banda finita e stretta.
//
// Cinque millisecondi è il valore classico: sotto i due si torna a sentire il
// click, sopra i dieci la manipolazione veloce comincia a impastarsi perché il
// punto non fa in tempo ad arrivare a fondo scala.
#pragma once

#include <cstddef>

namespace dsdr::dsp {

class CwKeyer
{
public:
    bool configure(double sampleRate);

    /// Durata del fronte, in millisecondi. Vale sia per la salita sia per la
    /// discesa: un inviluppo asimmetrico si sente come uno schiocco su un solo
    /// lato ed è un difetto senza vantaggi.
    void setRiseMs(double ms);
    double riseMs() const noexcept { return m_riseMs; }

    /// Stato del tasto. Può cambiare in mezzo a un blocco: l'inviluppo riparte
    /// dal valore in cui si trova, non da zero, così un tasto ribattuto veloce
    /// non produce un gradino a metà fronte.
    void setKeyDown(bool down) noexcept { m_keyDown = down; }
    bool keyDown() const noexcept { return m_keyDown; }

    void reset() noexcept;

    /// Scrive `n` campioni di inviluppo in 0…1.
    void process(float *envelope, std::size_t n) noexcept;

    /// Vero quando il tasto è alzato e la coda del fronte si è esaurita: è il
    /// momento in cui si può lasciare il PTT senza troncare la nota.
    bool isIdle() const noexcept { return !m_keyDown && m_position == 0; }

private:
    double m_sampleRate = 48000.0;
    double m_riseMs = 5.0;
    std::size_t m_edgeSamples = 1;
    std::size_t m_position = 0;   ///< 0 = spento, m_edgeSamples = acceso
    bool m_keyDown = false;
};

} // namespace dsdr::dsp
