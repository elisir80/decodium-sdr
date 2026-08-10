// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/InterpolatorChain.h"
#include "dsp/DecimatorChain.h"
#include "dsp/FirDesign.h"

#include <algorithm>

namespace dsdr::dsp {

std::vector<int> InterpolatorChain::factorize(int interpolation)
{
    // Stessi fattori della decimazione — quali convenga usare non cambia con
    // il verso — ma in ordine inverso.
    //
    // Il conto: uno stadio che interpola di f a partire da r costa r·N
    // moltiplicazioni al secondo, e i tap N crescono proporzionalmente a f.
    // Per due stadi a·b = L il costo va come R·a + R·a·b = R·(a + L): dipende
    // solo dal **primo** fattore, e conviene quindi che sia il più piccolo.
    // È l'opposto della discesa, dove il fattore grande va davanti.
    std::vector<int> factors = DecimatorChain::factorize(interpolation);
    std::reverse(factors.begin(), factors.end());
    return factors;
}

bool InterpolatorChain::configure(double inputRate,
                                  int totalInterpolation,
                                  double passbandHz,
                                  double stopbandDb)
{
    m_stages.clear();
    m_inputRate = inputRate;
    m_totalInterpolation = std::max(1, totalInterpolation);
    m_outputRate = inputRate * m_totalInterpolation;

    if (inputRate <= 0.0)
        return false;

    // Il segnale utile deve stare nella banda passante del primo stadio, che è
    // quello che lavora alla frequenza più bassa.
    if (passbandHz >= inputRate * 0.5)
        passbandHz = inputRate * 0.45;

    const std::vector<int> factors = factorize(m_totalInterpolation);
    const double beta = kaiserBeta(stopbandDb);

    double stageInRate = inputRate;
    for (int f : factors) {
        const double stageOutRate = stageInRate * f;
        // Le immagini che l'interpolazione crea stanno attorno ai multipli
        // della frequenza d'**ingresso** dello stadio: è lì che il filtro deve
        // arrivare a tacere, e per questo il conto guarda stageInRate. Il
        // filtro però gira alla frequenza d'uscita, ed è lì che si contano i
        // tap: una transizione larga in proporzione costa pochissimo.
        const double cutoff = std::min(passbandHz, stageInRate * 0.40);
        const double transition = std::max(stageInRate * 0.5 - cutoff, stageInRate * 0.05);
        const int taps = estimateTaps(transition, stageOutRate, stopbandDb);

        FirInterpolator stage;
        stage.configure(designLowpass(cutoff, stageOutRate, taps, beta), f);
        m_stages.push_back(std::move(stage));

        stageInRate = stageOutRate;
    }

    // Buffer di lavoro. Qui il blocco **cresce** attraversando la catena: il
    // limite va messo sull'ingresso, non sull'uscita, altrimenti il primo
    // blocco pieno scriverebbe oltre lo scratch.
    m_chunkFrames = kMaxBlockFrames / static_cast<std::size_t>(m_totalInterpolation);
    if (m_chunkFrames == 0)
        m_chunkFrames = 1;
    if (!m_stages.empty()) {
        m_scratchA.assign(kMaxBlockFrames + 8, Complex(0.0f, 0.0f));
        m_scratchB.assign(kMaxBlockFrames + 8, Complex(0.0f, 0.0f));
    }
    return true;
}

void InterpolatorChain::reset() noexcept
{
    for (FirInterpolator &stage : m_stages)
        stage.reset();
}

std::size_t InterpolatorChain::process(const Complex *in, std::size_t n, Complex *out) noexcept
{
    if (m_stages.empty()) {
        std::copy(in, in + n, out);
        return n;
    }

    std::size_t produced = 0;
    std::size_t offset = 0;

    while (offset < n) {
        const std::size_t chunk = std::min(m_chunkFrames, n - offset);

        const Complex *src = in + offset;
        std::size_t count = chunk;
        bool useA = true;

        for (std::size_t s = 0; s < m_stages.size(); ++s) {
            const bool last = (s + 1 == m_stages.size());
            Complex *dst = last ? (out + produced) : (useA ? m_scratchA.data() : m_scratchB.data());
            count = m_stages[s].process(src, count, dst);
            src = dst;
            useA = !useA;
            if (count == 0)
                break;
        }

        produced += count;
        offset += chunk;
    }

    return produced;
}

} // namespace dsdr::dsp
