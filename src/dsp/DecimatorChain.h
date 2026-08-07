// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — decimazione multistadio (§5.1).
//
// Un solo FIR che decima di 20 richiederebbe una transizione ripidissima e
// centinaia di tap. Spezzando in stadi (es. 20 = 5 × 4) ogni stadio lavora con
// una transizione larga rispetto alla propria frequenza di uscita, e il costo
// complessivo crolla di quasi un ordine di grandezza.
#pragma once

#include "dsp/FirDecimator.h"

#include <vector>

namespace dsdr::dsp {

class DecimatorChain
{
public:
    /// Configura la catena. `passbandHz` è la semilarghezza del segnale utile
    /// che deve sopravvivere alla decimazione (per un canale SSB bastano
    /// ~4 kHz). Alloca: da chiamare fuori dal percorso caldo.
    bool configure(double inputRate,
                   int totalDecimation,
                   double passbandHz,
                   double stopbandDb = 80.0);

    void reset() noexcept;

    /// Elabora un blocco di qualsiasi lunghezza: internamente lo spezza in
    /// porzioni compatibili con i buffer di lavoro pre-allocati.
    std::size_t process(const Complex *in, std::size_t n, Complex *out) noexcept;

    double outputRate() const noexcept { return m_outputRate; }
    int totalDecimation() const noexcept { return m_totalDecimation; }
    int stageCount() const noexcept { return static_cast<int>(m_stages.size()); }

    std::size_t maxOutput(std::size_t inputFrames) const noexcept
    {
        return inputFrames / static_cast<std::size_t>(m_totalDecimation) + 1;
    }

    /// Fattorizzazione usata: esposta per i test e per la diagnostica.
    static std::vector<int> factorize(int decimation);

private:
    std::vector<FirDecimator> m_stages;
    std::vector<Complex> m_scratchA;
    std::vector<Complex> m_scratchB;
    double m_inputRate = 0.0;
    double m_outputRate = 0.0;
    int m_totalDecimation = 1;
    std::size_t m_chunkFrames = 0;
};

} // namespace dsdr::dsp
