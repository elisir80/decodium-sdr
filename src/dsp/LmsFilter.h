// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — predittore adattivo: riduzione di rumore e notch automatico.
//
// Sono la stessa macchina, presa da due lati opposti, ed è il motivo per cui
// stanno in una classe sola.
//
// Un filtro adattivo impara a prevedere il campione che sta per arrivare a
// partire da quelli di poco prima. Ciò che è periodico — un tono, la voce, un
// battimento — è prevedibile; il rumore diffuso no, per definizione.
//
//   · **NR**  si tiene la previsione: passa ciò che è prevedibile, cioè il
//     segnale, e resta fuori il rumore.
//   · **ANF** si tiene l'errore: passa ciò che il filtro *non* è riuscito a
//     prevedere, cioè tutto tranne le righe fisse — l'eterodina sparisce.
//
// Il ritardo di decorrelazione è la chiave: senza, il filtro predice anche il
// rumore a partire dal campione immediatamente precedente e non toglie niente.
//
// Adattamento normalizzato sulla potenza del segnale (NLMS): con un passo
// fisso, un segnale forte fa divergere i coefficienti e l'audio esplode in un
// fischio — capita esattamente quando arriva la stazione che si aspettava.
#pragma once

#include <cstddef>
#include <vector>

namespace dsdr::dsp {

class LmsFilter
{
public:
    /// Che cosa esce dal filtro: la parte prevedibile o quel che resta.
    enum class Output
    {
        Prediction,   ///< NR: tiene il segnale, scarta il rumore
        Error,        ///< ANF: scarta le righe fisse, tiene il resto
    };

    /// `taps` è la memoria del predittore, `delay` la distanza da cui guarda.
    /// Alloca: va chiamata fuori dal percorso caldo.
    void configure(int taps, int delay);
    void reset() noexcept;

    /// Velocità di apprendimento, 0..1 in scala relativa. Alta insegue in
    /// fretta ma "respira" sul parlato; bassa è pulita ma lenta a togliere una
    /// eterodina appena arrivata.
    void setRate(float rate) noexcept;
    float rate() const noexcept { return m_rate; }

    /// Elabora in place.
    void process(float *audio, std::size_t n, Output output) noexcept;

    bool isConfigured() const noexcept { return m_taps > 0; }

private:
    std::vector<float> m_weights;
    std::vector<float> m_history;   ///< buffer circolare dei campioni passati
    std::size_t m_write = 0;
    int m_taps = 0;
    int m_delay = 0;
    float m_rate = 0.05f;
    float m_power = 1e-6f;          ///< potenza media, per la normalizzazione
};

} // namespace dsdr::dsp
