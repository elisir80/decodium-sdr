// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/Modulator.h"
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {

/// L'audio reale porta metà della sua energia nelle frequenze negative. Tenendo
/// una sola banda laterale se ne butta via una metà, e senza questo fattore la
/// SSB uscirebbe 6 dB sotto la DSB a parità di audio d'ingresso: l'operatore
/// vedrebbe l'indicatore di potenza scendere cambiando modo, e cercherebbe il
/// guasto dalla parte sbagliata.
constexpr float kSingleSidebandGain = 2.0f;

/// Larghezza della transizione del filtro di trasmissione. Stretta abbastanza
/// da non regalare banda al vicino di canale, larga abbastanza da non
/// richiedere più tap di quanti kMaxFirTaps ne conceda.
double transitionFor(double lowHz, double highHz) noexcept
{
    return std::max(200.0, (highHz - lowHz) * 0.12);
}

} // namespace

bool Modulator::configure(double audioRate)
{
    if (audioRate <= 0.0)
        return false;

    m_audioRate = audioRate;
    // Capacità prenotata una volta sola: da qui in avanti il cambio di modo
    // riscrive i coefficienti senza allocare (RNF-05).
    m_taps.reserve(kMaxFirTaps);
    m_scratch.assign(kMaxBlockFrames, Complex(0.0f, 0.0f));
    designFilter();
    reset();
    return true;
}

void Modulator::setSettings(const TxSettings &settings)
{
    const bool filterChanged = settings.mode != m_settings.mode
        || settings.lowHz != m_settings.lowHz
        || settings.highHz != m_settings.highHz;

    m_settings = settings;
    m_settings.amDepth = std::clamp(m_settings.amDepth, 0.0, 1.0);

    if (filterChanged && m_audioRate > 0.0)
        designFilter();
}

void Modulator::designFilter()
{
    const double lo = static_cast<double>(m_settings.lowHz);
    const double hi = static_cast<double>(m_settings.highHz);

    double loHz = 0.0;
    double hiHz = 0.0;
    m_filterActive = true;

    switch (m_settings.mode) {
    case DemodMode::Usb:
    case DemodMode::DigU:
        loHz = lo;
        hiHz = hi;
        break;
    case DemodMode::Lsb:
    case DemodMode::DigL:
        loHz = -hi;
        hiHz = -lo;
        break;
    case DemodMode::Am:
    case DemodMode::Sam:
    case DemodMode::Dsb:
    case DemodMode::Fm:
    case DemodMode::Nfm:
        // Doppia banda laterale: il filtro torna simmetrico attorno a DC, cioè
        // un passa-basso. Il taglio in basso non si fa qui — un FIR complesso
        // non può togliere una banda al centro senza toglierla anche di lato —
        // ma nel processore di voce, che ha già il suo passa-alto.
        loHz = -hi;
        hiHz = hi;
        break;
    case DemodMode::Cw:
    case DemodMode::Cwr:
        // La portante CW **sta** a frequenza zero: qualunque passa-banda che
        // parta da 300 Hz la cancellerebbe del tutto. La forma del segnale la
        // dà l'inviluppo del manipolatore, ed è lì che si evita il click.
        m_filterActive = false;
        break;
    case DemodMode::Iq:
        // Iniezione diretta in banda base: chi la sceglie sa cosa sta
        // mandando, e un filtro che non ha chiesto sarebbe una sorpresa.
        m_filterActive = false;
        break;
    }

    if (!m_filterActive)
        return;

    const double transition = transitionFor(lo, hi);
    int taps = estimateTaps(transition, m_audioRate, 70.0);
    taps = std::min(taps, static_cast<int>(kMaxFirTaps) - 1);
    if ((taps & 1) == 0)
        ++taps;

    designBandpassInto(m_taps, loHz, hiHz, m_audioRate, taps, kaiserBeta(70.0));
    m_filter.setTaps(m_taps);
}

void Modulator::reset() noexcept
{
    m_filter.reset();
    m_fmPhase = 0.0;
    m_lastPeak = 0.0f;
}

void Modulator::process(const float *audio, std::size_t n, Complex *out) noexcept
{
    if (n == 0)
        return;
    n = std::min(n, m_scratch.size());

    // L'audio entra come segnale complesso con parte immaginaria nulla: è la
    // forma su cui il FIR complesso sa lavorare, ed è anche il motivo per cui
    // una sola banda laterale sopravvive al filtro.
    for (std::size_t i = 0; i < n; ++i)
        m_scratch[i] = Complex(audio[i], 0.0f);

    if (m_filterActive)
        m_filter.process(m_scratch.data(), m_scratch.data(), n);

    const DemodMode mode = m_settings.mode;

    switch (mode) {
    case DemodMode::Usb:
    case DemodMode::Lsb:
    case DemodMode::DigU:
    case DemodMode::DigL:
        for (std::size_t i = 0; i < n; ++i)
            out[i] = m_scratch[i] * kSingleSidebandGain;
        break;

    case DemodMode::Dsb:
    case DemodMode::Iq:
        for (std::size_t i = 0; i < n; ++i)
            out[i] = m_scratch[i];
        break;

    case DemodMode::Am:
    case DemodMode::Sam: {
        const float depth = static_cast<float>(m_settings.amDepth);
        // Normalizzando per (1+m) il picco resta unitario qualunque sia la
        // profondità: senza, alzare la modulazione manderebbe in saturazione
        // lo stadio successivo invece che aumentare la resa.
        const float norm = 1.0f / (1.0f + depth);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = Complex((1.0f + depth * m_scratch[i].real()) * norm, 0.0f);
        break;
    }

    case DemodMode::Fm:
    case DemodMode::Nfm: {
        const double step = kTwoPi * m_settings.fmDeviationHz / m_audioRate;
        for (std::size_t i = 0; i < n; ++i) {
            m_fmPhase += step * m_scratch[i].real();
            // La fase si riporta in ±π a ogni campione: lasciarla crescere
            // farebbe perdere precisione al seno e al coseno dopo qualche
            // minuto di trasmissione, e la portante comincerebbe a sporcarsi
            // senza che nulla nel codice sia cambiato.
            if (m_fmPhase > kPi)
                m_fmPhase -= kTwoPi;
            else if (m_fmPhase < -kPi)
                m_fmPhase += kTwoPi;
            out[i] = Complex(static_cast<float>(std::cos(m_fmPhase)),
                             static_cast<float>(std::sin(m_fmPhase)));
        }
        break;
    }

    case DemodMode::Cw:
    case DemodMode::Cwr:
        // Portante a frequenza zero, modulata solo dall'inviluppo.
        for (std::size_t i = 0; i < n; ++i)
            out[i] = Complex(m_scratch[i].real(), 0.0f);
        break;
    }

    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
        peak = std::max(peak, magnitudeSquared(out[i]));
    m_lastPeak = std::sqrt(peak);
}

} // namespace dsdr::dsp
