// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/SpeechProcessor.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {
namespace {

/// Rapporto di compressione fisso 4:1. Esporlo all'operatore vorrebbe dire
/// chiedergli di regolare due manopole che interagiscono; con il rapporto
/// fermo, «quanti dB di compressione» è una domanda a cui si può rispondere.
constexpr float kSlope = 0.75f;   // 1 - 1/4

constexpr double kAttackMs = 2.0;
constexpr double kReleaseMs = 120.0;

/// Sopra questa soglia il limitatore comincia a piegare invece di tagliare.
constexpr float kKnee = 0.9f;

float softClip(float x, bool &limited) noexcept
{
    const float a = std::abs(x);
    if (a <= kKnee)
        return x;
    limited = true;
    // Tagliare di netto genererebbe armoniche su tutta la banda — lo splatter
    // che si sente due canali più in là. La tangente iperbolica piega la
    // caratteristica restando continua anche nella derivata: quel che esce
    // dalla banda è molto meno, e a costo di una sola transcendentale sui
    // pochi campioni che superano il ginocchio.
    const float over = (a - kKnee) / (1.0f - kKnee);
    const float shaped = kKnee + (1.0f - kKnee) * std::tanh(over);
    return x < 0.0f ? -shaped : shaped;
}

float coefficientFor(double milliseconds, double sampleRate) noexcept
{
    const double samples = std::max(1.0, milliseconds * 0.001 * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

} // namespace

bool SpeechProcessor::configure(double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    m_highPass.configure(sampleRate, m_highPassHz, 100.0);
    recomputeCoefficients();
    reset();
    return true;
}

void SpeechProcessor::setMicGainDb(double db)
{
    m_micGainDb = std::clamp(db, -20.0, 40.0);
    recomputeCoefficients();
}

void SpeechProcessor::setCompressionDb(double db)
{
    m_compressionDb = std::clamp(db, 0.0, 20.0);
    recomputeCoefficients();
}

void SpeechProcessor::setHighPassHz(double hz)
{
    m_highPassHz = std::clamp(hz, 20.0, 1000.0);
    m_highPass.configure(m_sampleRate, m_highPassHz, 100.0);
}

void SpeechProcessor::recomputeCoefficients()
{
    m_linearGain = static_cast<float>(std::pow(10.0, m_micGainDb / 20.0));

    // La soglia si ricava dalla compressione chiesta: un segnale a fondo scala
    // deve subire esattamente quella riduzione. Il conto è l'inverso della
    // caratteristica del compressore, non un numero scelto a occhio.
    if (m_compressionDb <= 0.0) {
        m_threshold = 1.0f;
        m_slope = 0.0f;
    } else {
        const double thresholdDb = -m_compressionDb / kSlope;
        m_threshold = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
        m_slope = kSlope;
    }

    m_attack = coefficientFor(kAttackMs, m_sampleRate);
    m_release = coefficientFor(kReleaseMs, m_sampleRate);
}

void SpeechProcessor::reset() noexcept
{
    m_highPass.reset();
    m_envelope = 0.0f;
    m_lastCompressionDb = 0.0f;
    m_lastInputPeak = 0.0f;
    m_lastLimited = false;
}

void SpeechProcessor::process(float *audio, std::size_t n) noexcept
{
    if (n == 0)
        return;

    // Il guadagno di recupero riporta il picco dove stava: senza, alzare la
    // compressione **abbasserebbe** il segnale, e il comando sembrerebbe
    // funzionare al contrario.
    const float makeup = static_cast<float>(std::pow(10.0, m_compressionDb / 20.0));

    float inputPeak = 0.0f;
    float maxReduction = 1.0f;
    bool limited = false;

    for (std::size_t i = 0; i < n; ++i) {
        const float raw = audio[i];
        inputPeak = std::max(inputPeak, std::abs(raw));

        float x = m_highPass.process(raw) * m_linearGain;

        const float rectified = std::abs(x);
        const float coefficient = rectified > m_envelope ? m_attack : m_release;
        m_envelope = coefficient * m_envelope + (1.0f - coefficient) * rectified;

        float gain = 1.0f;
        if (m_slope > 0.0f && m_envelope > m_threshold) {
            const float overDb = 20.0f * std::log10(m_envelope / m_threshold);
            gain = std::pow(10.0f, -m_slope * overDb / 20.0f);
            maxReduction = std::min(maxReduction, gain);
        }

        audio[i] = softClip(x * gain * makeup, limited);
    }

    m_lastInputPeak = inputPeak;
    m_lastCompressionDb = maxReduction < 1.0f ? -20.0f * std::log10(maxReduction) : 0.0f;
    m_lastLimited = limited;
}

} // namespace dsdr::dsp
