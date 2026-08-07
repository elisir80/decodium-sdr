// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/SpectrumAnalyzer.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsdr::dsp {

SpectrumAnalyzer::SpectrumAnalyzer() = default;

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    destroyPlan();
}

void SpectrumAnalyzer::destroyPlan()
{
    if (m_plan) {
        fftwf_destroy_plan(reinterpret_cast<fftwf_plan>(m_plan));
        m_plan = nullptr;
    }
    if (m_fftIn) {
        fftwf_free(m_fftIn);
        m_fftIn = nullptr;
    }
    if (m_fftOut) {
        fftwf_free(m_fftOut);
        m_fftOut = nullptr;
    }
}

bool SpectrumAnalyzer::configure(int fftSize, double sampleRate, WindowType window)
{
    if (fftSize < 64 || (fftSize & (fftSize - 1)) != 0)
        return false;
    if (sampleRate <= 0.0)
        return false;

    destroyPlan();

    m_fftSize = fftSize;
    m_sampleRate = sampleRate;

    m_fftIn = static_cast<Complex *>(fftwf_malloc(sizeof(Complex) * static_cast<std::size_t>(fftSize)));
    m_fftOut = static_cast<Complex *>(fftwf_malloc(sizeof(Complex) * static_cast<std::size_t>(fftSize)));
    if (!m_fftIn || !m_fftOut) {
        destroyPlan();
        return false;
    }

    // FFTW_ESTIMATE: la pianificazione è immediata e deterministica; MEASURE
    // costerebbe centinaia di ms all'apertura del device senza un guadagno
    // percepibile a queste dimensioni.
    m_plan = reinterpret_cast<fftwf_plan_s *>(
        fftwf_plan_dft_1d(fftSize,
                          reinterpret_cast<fftwf_complex *>(m_fftIn),
                          reinterpret_cast<fftwf_complex *>(m_fftOut),
                          FFTW_FORWARD,
                          FFTW_ESTIMATE));
    if (!m_plan) {
        destroyPlan();
        return false;
    }

    m_accumulator.assign(static_cast<std::size_t>(fftSize), Complex(0.0f, 0.0f));
    m_scratchDb.assign(static_cast<std::size_t>(fftSize), -160.0f);
    m_averaged.assign(static_cast<std::size_t>(fftSize), -160.0f);
    computeWindow(window);

    m_filled = 0;
    m_frameCounter = 0;
    setOverlap(m_overlap);
    return true;
}

void SpectrumAnalyzer::computeWindow(WindowType type)
{
    m_window.assign(static_cast<std::size_t>(m_fftSize), 1.0f);
    const double n1 = static_cast<double>(m_fftSize - 1);
    double sum = 0.0;

    for (int i = 0; i < m_fftSize; ++i) {
        const double x = static_cast<double>(i) / n1;
        double w = 1.0;
        switch (type) {
        case WindowType::Rectangular:
            w = 1.0;
            break;
        case WindowType::Hann:
            w = 0.5 - 0.5 * std::cos(kTwoPi * x);
            break;
        case WindowType::BlackmanHarris:
            w = 0.35875 - 0.48829 * std::cos(kTwoPi * x) + 0.14128 * std::cos(2.0 * kTwoPi * x)
                - 0.01168 * std::cos(3.0 * kTwoPi * x);
            break;
        }
        m_window[static_cast<std::size_t>(i)] = static_cast<float>(w);
        sum += w;
    }

    // Guadagno coerente: normalizza l'ampiezza di una sinusoide piena scala a 0 dBFS.
    m_windowGain = static_cast<float>(sum);
    if (m_windowGain < 1e-9f)
        m_windowGain = 1.0f;
}

void SpectrumAnalyzer::setAveraging(float alpha)
{
    m_averaging = std::clamp(alpha, 0.01f, 1.0f);
}

void SpectrumAnalyzer::setOverlap(float overlap)
{
    m_overlap = std::clamp(overlap, 0.0f, 0.75f);
    const std::size_t size = static_cast<std::size_t>(std::max(m_fftSize, 1));
    m_hop = static_cast<std::size_t>(size * (1.0f - m_overlap));
    m_hop = std::clamp<std::size_t>(m_hop, 1, size);
}

void SpectrumAnalyzer::reset() noexcept
{
    std::fill(m_accumulator.begin(), m_accumulator.end(), Complex(0.0f, 0.0f));
    std::fill(m_averaged.begin(), m_averaged.end(), -160.0f);
    m_filled = 0;
}

bool SpectrumAnalyzer::push(const Complex *in, std::size_t n) noexcept
{
    if (!m_plan || m_fftSize <= 0)
        return false;

    const std::size_t size = static_cast<std::size_t>(m_fftSize);
    bool produced = false;
    std::size_t offset = 0;

    while (offset < n) {
        const std::size_t room = size - m_filled;
        const std::size_t take = std::min(room, n - offset);
        std::memcpy(m_accumulator.data() + m_filled, in + offset, take * sizeof(Complex));
        m_filled += take;
        offset += take;

        if (m_filled < size)
            break;

        transform();
        produced = true;

        // Sovrapposizione: conserva la coda per la finestra successiva.
        const std::size_t keep = size - m_hop;
        if (keep > 0)
            std::memmove(m_accumulator.data(), m_accumulator.data() + m_hop, keep * sizeof(Complex));
        m_filled = keep;
    }

    return produced;
}

void SpectrumAnalyzer::transform() noexcept
{
    const std::size_t size = static_cast<std::size_t>(m_fftSize);

    for (std::size_t i = 0; i < size; ++i)
        m_fftIn[i] = m_accumulator[i] * m_window[i];

    fftwf_execute(reinterpret_cast<fftwf_plan>(m_plan));

    const float norm = 2.0f / m_windowGain;
    const std::size_t half = size / 2;

    for (std::size_t k = 0; k < size; ++k) {
        // fftshift: il bin 0 (DC) finisce al centro dell'asse.
        const std::size_t target = (k + half) % size;
        const Complex v = m_fftOut[k] * norm;
        m_scratchDb[target] = powerToDb(magnitudeSquared(v));
    }

    if (m_averaging >= 0.999f) {
        m_averaged.swap(m_scratchDb);
    } else {
        const float a = m_averaging;
        for (std::size_t i = 0; i < size; ++i)
            m_averaged[i] += (m_scratchDb[i] - m_averaged[i]) * a;
    }

    ++m_frameCounter;
}

double SpectrumAnalyzer::binWidthHz() const noexcept
{
    return (m_fftSize > 0) ? (m_sampleRate / m_fftSize) : 0.0;
}

} // namespace dsdr::dsp
