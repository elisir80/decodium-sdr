// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/MultibandCompressor.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

namespace dsdr::dsp {

namespace {

/// Il fattore di merito di una sezione di Butterworth del secondo ordine.
/// Due in cascata fanno un Linkwitz-Riley del quarto ordine, che è la
/// separazione che somma piatta.
constexpr double kButterworthQ = 0.70710678118654752;

/// Attacco e rilascio, in millisecondi.
///
/// Sei millisecondi di attacco lasciano passare l'attacco della consonante —
/// che è quello che rende una voce intelligibile — e prendono il resto. Il
/// rilascio a centoventi è il compromesso fra il pompaggio, che si sente sotto
/// i cinquanta, e la voce che resta abbassata dopo una punta, che si sente
/// sopra i trecento.
constexpr double kAttackMs = 6.0;
constexpr double kReleaseMs = 120.0;

/// Sotto questo livello il rivelatore non insegue: è il rumore di fondo del
/// microfono, e inseguirlo vorrebbe dire alzare il guadagno nelle pause —
/// cioè fare entrare la stanza fra una frase e l'altra.
constexpr double kFloorDb = -70.0;

double toDb(double linear) noexcept
{
    return 20.0 * std::log10(std::max(linear, 1e-7));
}

} // namespace

double MultibandCompressor::BandCompressor::process(double in) noexcept
{
    const double magnitude = std::abs(in);

    // Rivelatore di picco: sale con l'attacco, scende con il rilascio. Due
    // costanti diverse, perché una voce sale in un istante e scende piano —
    // e un rivelatore simmetrico o taglia gli attacchi o insegue il rumore.
    const double coeff = magnitude > envelope ? attackCoeff : releaseCoeff;
    envelope = magnitude + coeff * (envelope - magnitude);

    const double levelDb = toDb(envelope);
    double targetGainDb = 0.0;
    if (levelDb > thresholdDb && levelDb > kFloorDb) {
        const double over = levelDb - thresholdDb;
        // Sopra la soglia si lascia passare solo la frazione 1/rapporto:
        // dell'eccesso resta `over / ratio`, il resto si toglie.
        targetGainDb = -(over - over / ratio);
    }

    // Il guadagno insegue con le stesse costanti dell'inviluppo: applicarlo di
    // scatto produce un clic a ogni sillaba.
    const double gainCoeff = targetGainDb < gainDb ? attackCoeff : releaseCoeff;
    gainDb = targetGainDb + gainCoeff * (gainDb - targetGainDb);

    return in * std::pow(10.0, gainDb / 20.0);
}

void MultibandCompressor::configure(double sampleRate)
{
    if (!(sampleRate > 0.0))
        return;

    m_sampleRate = sampleRate;
    m_configured = true;

    const double attack = std::exp(-1.0 / (sampleRate * kAttackMs / 1000.0));
    const double release = std::exp(-1.0 / (sampleRate * kReleaseMs / 1000.0));
    for (BandCompressor &compressor : m_compressors) {
        compressor.attackCoeff = attack;
        compressor.releaseCoeff = release;
    }

    designCrossovers();
    applyPunch();
    reset();
    m_dirty.store(false, std::memory_order_release);
}

void MultibandCompressor::reset() noexcept
{
    for (auto &pair : m_lowpass) {
        for (Biquad &section : pair)
            section.reset();
    }
    for (auto &pair : m_highpass) {
        for (Biquad &section : pair)
            section.reset();
    }
    for (Biquad &section : m_allpassBand0)
        section.reset();
    m_allpassBand1.reset();

    for (BandCompressor &compressor : m_compressors)
        compressor.reset();
    for (auto &value : m_reduction)
        value.store(0.0, std::memory_order_relaxed);
}

void MultibandCompressor::setPunch(double punch) noexcept
{
    m_punch.store(std::clamp(punch, 0.0, 10.0), std::memory_order_relaxed);
    m_dirty.store(true, std::memory_order_release);
}

double MultibandCompressor::gainReductionDb(int band) const noexcept
{
    if (band < 0 || band >= kBands)
        return 0.0;
    return m_reduction[static_cast<std::size_t>(band)].load(std::memory_order_relaxed);
}

void MultibandCompressor::designCrossovers() noexcept
{
    if (!(m_sampleRate > 0.0))
        return;

    // Passa-basso, passa-alto e passa-tutto del secondo ordine, formule RBJ.
    const auto design = [this](double frequency, Biquad &lowpass, Biquad &highpass,
                               Biquad *allpass) {
        const double omega = 2.0 * M_PI * frequency / m_sampleRate;
        const double sn = std::sin(omega);
        const double cs = std::cos(omega);
        const double alpha = sn / (2.0 * kButterworthQ);
        const double a0 = 1.0 + alpha;

        lowpass.b0 = ((1.0 - cs) / 2.0) / a0;
        lowpass.b1 = (1.0 - cs) / a0;
        lowpass.b2 = lowpass.b0;
        lowpass.a1 = (-2.0 * cs) / a0;
        lowpass.a2 = (1.0 - alpha) / a0;

        highpass.b0 = ((1.0 + cs) / 2.0) / a0;
        highpass.b1 = (-(1.0 + cs)) / a0;
        highpass.b2 = highpass.b0;
        highpass.a1 = lowpass.a1;
        highpass.a2 = lowpass.a2;

        if (allpass) {
            allpass->b0 = (1.0 - alpha) / a0;
            allpass->b1 = (-2.0 * cs) / a0;
            allpass->b2 = 1.0;
            allpass->a1 = (-2.0 * cs) / a0;
            allpass->a2 = (1.0 - alpha) / a0;
        }
    };

    for (int split = 0; split < kBands - 1; ++split) {
        const auto at = static_cast<std::size_t>(split);
        design(kCrossovers[at], m_lowpass[at][0], m_highpass[at][0], nullptr);
        // La seconda sezione è identica: due Butterworth in cascata fanno il
        // Linkwitz-Riley.
        m_lowpass[at][1] = m_lowpass[at][0];
        m_highpass[at][1] = m_highpass[at][0];
    }

    // I passa-tutto della compensazione, alle stesse frequenze delle
    // separazioni che le bande basse non attraversano.
    Biquad unused;
    design(kCrossovers[1], unused, unused, &m_allpassBand0[0]);
    design(kCrossovers[2], unused, unused, &m_allpassBand0[1]);
    design(kCrossovers[2], unused, unused, &m_allpassBand1);
}

void MultibandCompressor::applyPunch() noexcept
{
    const double punch = m_punch.load(std::memory_order_relaxed);

    // Un numero solo muove quattro soglie e quattro rapporti. Le soglie
    // scendono insieme — da −6 dB, dove quasi niente le tocca, a −30, dove
    // quasi tutto le supera — e il rapporto sale da 1,5 a 4.
    //
    // Le bande non si trattano uguali. Il corpo (0) e la presenza (3) si
    // comprimono un po' meno: schiacciare il corpo toglie la voce, schiacciare
    // la presenza fa uscire le sibilanti. Quelle di mezzo portano
    // l'intelligibilità e sopportano di più.
    static constexpr double kBias[kBands] = {2.0, 0.0, 0.0, 1.5};

    const double fraction = punch / 10.0;
    const double baseThreshold = -6.0 - 24.0 * fraction;
    const double baseRatio = 1.5 + 2.5 * fraction;

    for (int band = 0; band < kBands; ++band) {
        const auto at = static_cast<std::size_t>(band);
        m_compressors[at].thresholdDb = baseThreshold + kBias[at];
        m_compressors[at].ratio = std::max(1.0, baseRatio - kBias[at] * 0.2);
    }
}

void MultibandCompressor::process(float *audio, std::size_t frames) noexcept
{
    if (!m_configured || frames == 0)
        return;

    if (m_dirty.exchange(false, std::memory_order_acquire))
        applyPunch();

    if (!m_enabled.load(std::memory_order_relaxed)) {
        // Spento non vuol dire fermo: gli inviluppi si lasciano scendere, così
        // riaccendendolo non parte da un guadagno abbassato di dieci decibel
        // fermo lì dall'ultima frase.
        for (auto &value : m_reduction)
            value.store(0.0, std::memory_order_relaxed);
        return;
    }

    std::array<double, kBands> worst{};

    for (std::size_t i = 0; i < frames; ++i) {
        const double in = static_cast<double>(audio[i]);

        // ── Le tre separazioni, in cascata ───────────────────────────────
        double low = in;
        for (Biquad &section : m_lowpass[0])
            low = section.process(low);
        double rest = in;
        for (Biquad &section : m_highpass[0])
            rest = section.process(rest);

        double lowMid = rest;
        for (Biquad &section : m_lowpass[1])
            lowMid = section.process(lowMid);
        double rest2 = rest;
        for (Biquad &section : m_highpass[1])
            rest2 = section.process(rest2);

        double highMid = rest2;
        for (Biquad &section : m_lowpass[2])
            highMid = section.process(highMid);
        double high = rest2;
        for (Biquad &section : m_highpass[2])
            high = section.process(high);

        // ── La compensazione ─────────────────────────────────────────────
        //
        // La banda bassa non ha attraversato le separazioni 1 e 2, quella
        // medio-bassa non ha attraversato la 2: passano dai passa-tutto
        // equivalenti, e la somma torna il segnale di partenza.
        for (Biquad &section : m_allpassBand0)
            low = section.process(low);
        lowMid = m_allpassBand1.process(lowMid);

        // ── I quattro compressori ────────────────────────────────────────
        std::array<double, kBands> bands{low, lowMid, highMid, high};
        double sum = 0.0;
        for (int band = 0; band < kBands; ++band) {
            const auto at = static_cast<std::size_t>(band);
            const double before = bands[at];
            const double after = m_compressors[at].process(before);
            sum += after;
            worst[at] = std::min(worst[at], m_compressors[at].gainDb);
        }

        audio[i] = static_cast<float>(sum);
    }

    for (int band = 0; band < kBands; ++band) {
        const auto at = static_cast<std::size_t>(band);
        m_reduction[at].store(-worst[at], std::memory_order_relaxed);
    }
}

} // namespace dsdr::dsp
