// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/DecimatorChain.h"
#include "dsp/FirDesign.h"

#include <algorithm>

namespace dsdr::dsp {

std::vector<int> DecimatorChain::factorize(int decimation)
{
    std::vector<int> factors;
    if (decimation <= 1)
        return factors;

    int remaining = decimation;
    // Fattori grandi per primi: il primo stadio è quello che gira alla
    // frequenza più alta, conviene che tolga più banda possibile.
    for (int f : {8, 7, 6, 5, 4, 3, 2}) {
        while (remaining % f == 0) {
            factors.push_back(f);
            remaining /= f;
        }
    }
    if (remaining > 1)
        factors.push_back(remaining); // fattore primo grande: stadio singolo
    return factors;
}

bool DecimatorChain::configure(double inputRate,
                               int totalDecimation,
                               double passbandHz,
                               double stopbandDb)
{
    m_stages.clear();
    m_inputRate = inputRate;
    m_totalDecimation = std::max(1, totalDecimation);
    m_outputRate = inputRate / m_totalDecimation;

    if (inputRate <= 0.0)
        return false;

    // Il segnale utile deve stare nella banda passante dell'ultimo stadio.
    if (passbandHz >= m_outputRate * 0.5)
        passbandHz = m_outputRate * 0.45;

    const std::vector<int> factors = factorize(m_totalDecimation);
    const double beta = kaiserBeta(stopbandDb);

    double stageInRate = inputRate;
    for (int f : factors) {
        const double stageOutRate = stageInRate / f;
        // Manteniamo intatta la banda utile e lasciamo che l'aliasing cada
        // fuori da essa: il cutoff è il minore fra la banda utile e il 40%
        // della nuova frequenza di campionamento.
        const double cutoff = std::min(passbandHz, stageOutRate * 0.40);
        const double transition = std::max(stageOutRate * 0.5 - cutoff, stageOutRate * 0.05);
        const int taps = estimateTaps(transition, stageInRate, stopbandDb);

        FirDecimator stage;
        stage.configure(designLowpass(cutoff, stageInRate, taps, beta), f);
        m_stages.push_back(std::move(stage));

        stageInRate = stageOutRate;
    }

    // Buffer di lavoro: dimensionati una volta sola per il blocco massimo.
    m_chunkFrames = kMaxBlockFrames;
    if (!m_stages.empty()) {
        m_scratchA.assign(m_chunkFrames + 8, Complex(0.0f, 0.0f));
        m_scratchB.assign(m_chunkFrames + 8, Complex(0.0f, 0.0f));
    }
    return true;
}

void DecimatorChain::reset() noexcept
{
    for (FirDecimator &stage : m_stages)
        stage.reset();
}

std::size_t DecimatorChain::process(const Complex *in, std::size_t n, Complex *out) noexcept
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
