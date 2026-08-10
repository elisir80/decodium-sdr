// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — interpolazione multistadio (TX).
//
// Specchio di DecimatorChain, e per lo stesso motivo: portare 48 kS/s a
// 768 kS/s con un solo filtro vorrebbe dire una transizione ripidissima a
// frequenza alta, cioè centinaia di tap fatti girare alla frequenza d'uscita —
// il punto più caro della catena. Spezzando in stadi (16 = 4 × 4) ogni stadio
// ha una transizione larga rispetto alla propria frequenza, e i tap in più
// stanno dove i campioni sono ancora pochi.
//
// L'ordine dei fattori è l'opposto di quello della decimazione: qui i fattori
// **piccoli** vanno per primi. Il costo di due stadi a·b = L vale R·(a + L),
// e dipende quindi solo dal primo fattore: quello grande conviene lasciarlo
// per ultimo, quando i campioni sono già tanti ma il filtro può essere corto.
#pragma once

#include "dsp/FirInterpolator.h"

#include <vector>

namespace dsdr::dsp {

class InterpolatorChain
{
public:
    /// Configura la catena. `passbandHz` è la semilarghezza del segnale utile
    /// che deve sopravvivere (per un canale SSB bastano ~4 kHz). Alloca: da
    /// chiamare fuori dal percorso caldo.
    bool configure(double inputRate,
                   int totalInterpolation,
                   double passbandHz,
                   double stopbandDb = 80.0);

    void reset() noexcept;

    /// Elabora un blocco di qualsiasi lunghezza: internamente lo spezza in
    /// porzioni compatibili con i buffer di lavoro pre-allocati.
    std::size_t process(const Complex *in, std::size_t n, Complex *out) noexcept;

    double outputRate() const noexcept { return m_outputRate; }
    int totalInterpolation() const noexcept { return m_totalInterpolation; }
    int stageCount() const noexcept { return static_cast<int>(m_stages.size()); }

    std::size_t maxOutput(std::size_t inputFrames) const noexcept
    {
        return inputFrames * static_cast<std::size_t>(m_totalInterpolation);
    }

    /// Numero di campioni d'ingresso che la catena può accettare in un colpo
    /// solo senza che i buffer interni traboccino. Blocchi più lunghi vanno
    /// bene lo stesso: `process` li spezza da sé.
    std::size_t maxInputChunk() const noexcept { return m_chunkFrames; }

    /// Fattorizzazione usata: esposta per i test e per la diagnostica.
    static std::vector<int> factorize(int interpolation);

private:
    std::vector<FirInterpolator> m_stages;
    std::vector<Complex> m_scratchA;
    std::vector<Complex> m_scratchB;
    double m_inputRate = 0.0;
    double m_outputRate = 0.0;
    int m_totalInterpolation = 1;
    std::size_t m_chunkFrames = 0;
};

} // namespace dsdr::dsp
