// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — analizzatore di spettro (FFTW3 single-precision, §5.1).
//
// Il tap è preso post-decimazione, in parallelo alla catena di demodulazione.
// L'uscita è già in ordine di frequenza crescente (fftshift applicato) e in
// dBFS, pronta per essere caricata come riga di texture dal waterfall.
#pragma once

#include "dsp/DspTypes.h"

#include <cstdint>
#include <vector>

struct fftwf_plan_s;

namespace dsdr::dsp {

enum class WindowType {
    Rectangular,
    Hann,
    BlackmanHarris, ///< 4 termini: -92 dB di lobi laterali, il default
};

class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();

    SpectrumAnalyzer(const SpectrumAnalyzer &) = delete;
    SpectrumAnalyzer &operator=(const SpectrumAnalyzer &) = delete;

    /// `fftSize` deve essere una potenza di due. Alloca (piano FFTW + buffer):
    /// da chiamare fuori dal percorso caldo.
    bool configure(int fftSize, double sampleRate, WindowType window = WindowType::BlackmanHarris);

    /// Coefficiente della media esponenziale fra frame: 1.0 = nessuna media
    /// (massima reattività), 0.1 = spettro molto smorzato.
    void setAveraging(float alpha);

    /// Frazione di sovrapposizione fra FFT successive (0.0–0.75).
    void setOverlap(float overlap);

    void reset() noexcept;

    /// Accumula campioni; restituisce true se almeno un nuovo frame è pronto.
    bool push(const Complex *in, std::size_t n) noexcept;

    const std::vector<float> &magnitudesDb() const noexcept { return m_averaged; }
    int binCount() const noexcept { return m_fftSize; }
    double binWidthHz() const noexcept;
    double sampleRate() const noexcept { return m_sampleRate; }
    std::uint64_t frameCounter() const noexcept { return m_frameCounter; }

private:
    void destroyPlan();

    /// Come sopra, ma con il lucchetto della pianificazione già in mano: chi
    /// la chiama sta già dentro la sezione protetta, e riprenderlo bloccherebbe
    /// il programma su se stesso.
    void destroyPlanLocked();
    void computeWindow(WindowType type);
    void transform() noexcept;

    fftwf_plan_s *m_plan = nullptr;
    Complex *m_fftIn = nullptr;  ///< fftwf_malloc
    Complex *m_fftOut = nullptr; ///< fftwf_malloc

    std::vector<Complex> m_accumulator;
    std::vector<float> m_window;
    std::vector<float> m_scratchDb;
    std::vector<float> m_averaged;

    int m_fftSize = 0;
    std::size_t m_filled = 0;
    std::size_t m_hop = 0;
    float m_overlap = 0.5f;
    float m_averaging = 0.45f;
    float m_windowGain = 1.0f;
    double m_sampleRate = 0.0;
    std::uint64_t m_frameCounter = 0;
};

} // namespace dsdr::dsp
