// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/Demodulator.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

void DcBlocker::configure(double sampleRate, double cornerHz)
{
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;
    m_a = static_cast<float>(std::exp(-kTwoPi * cornerHz / sampleRate));
    reset();
}

void DcBlocker::reset() noexcept
{
    m_prevIn = 0.0f;
    m_prevOut = 0.0f;
}

float DcBlocker::process(float x) noexcept
{
    const float y = x - m_prevIn + m_a * m_prevOut;
    m_prevIn = x;
    m_prevOut = y;
    return y;
}

void Demodulator::configure(double sampleRate)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_dcBlocker.configure(m_sampleRate);

    m_fmScale = static_cast<float>(m_sampleRate / (kTwoPi * m_fmDeviation));

    // PLL SAM: banda di loop ~50 Hz, smorzamento critico (ζ = 0.707).
    const double loopBandwidth = 50.0 / m_sampleRate;
    m_samAlpha = 2.0 * 0.707 * loopBandwidth;
    m_samBeta = loopBandwidth * loopBandwidth;

    reset();
}

void Demodulator::setMode(DemodMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    reset();
}

void Demodulator::setFmDeviation(double hz)
{
    m_fmDeviation = std::max(100.0, hz);
    m_fmScale = static_cast<float>(m_sampleRate / (kTwoPi * m_fmDeviation));
}

void Demodulator::setAmCarrierAgc(bool enabled)
{
    if (m_amCarrierAgc == enabled)
        return;
    m_amCarrierAgc = enabled;
    m_amCarrierLevel = 0.0f;
}

void Demodulator::reset() noexcept
{
    m_dcBlocker.reset();
    m_previous = Complex(0.0f, 0.0f);
    m_amCarrierLevel = 0.0f;
    m_samPhase = 0.0;
    m_samFrequency = 0.0;
}

std::size_t Demodulator::process(const Complex *in, std::size_t n, float *out) noexcept
{
    switch (m_mode) {
    case DemodMode::Usb:
    case DemodMode::Lsb:
    case DemodMode::Cw:
    case DemodMode::Cwr:
    case DemodMode::DigU:
    case DemodMode::DigL:
    case DemodMode::Dsb:
        return demodSsb(in, n, out);
    case DemodMode::Am:
        return demodAm(in, n, out);
    case DemodMode::Sam:
        return demodSam(in, n, out);
    case DemodMode::Fm:
    case DemodMode::Nfm:
        return demodFm(in, n, out);
    case DemodMode::Iq:
        return demodIq(in, n, out);
    }
    return demodSsb(in, n, out);
}

std::size_t Demodulator::demodSsb(const Complex *in, std::size_t n, float *out) noexcept
{
    // Il passa-banda complesso ha già isolato la banda laterale: la parte
    // reale è il segnale audio, la parte immaginaria è la sua trasformata di
    // Hilbert e si scarta. Fattore 2 perché metà energia sta nell'immaginaria.
    for (std::size_t i = 0; i < n; ++i)
        out[i] = 2.0f * in[i].real();
    return n;
}

std::size_t Demodulator::demodAm(const Complex *in, std::size_t n, float *out) noexcept
{
    for (std::size_t i = 0; i < n; ++i) {
        float envelope = std::sqrt(magnitudeSquared(in[i]));
        if (m_amCarrierAgc) {
            // La portante varia lentamente con fading e gain RF; la
            // modulazione audio è molto più rapida e resta conservata.
            m_amCarrierLevel += (envelope - m_amCarrierLevel) * 0.002f;
            const float gain = std::clamp(0.5f
                                              / std::max(m_amCarrierLevel, 1e-3f),
                                          0.05f, 20.0f);
            envelope *= gain;
        }
        out[i] = m_dcBlocker.process(envelope);
    }
    return n;
}

std::size_t Demodulator::demodSam(const Complex *in, std::size_t n, float *out) noexcept
{
    // AM sincrona: un PLL insegue la portante residua, poi si demodula
    // coerentemente sulla fase agganciata. Rispetto all'AM a inviluppo
    // sopravvive molto meglio al fading selettivo.
    for (std::size_t i = 0; i < n; ++i) {
        const Complex reference(static_cast<float>(std::cos(m_samPhase)),
                                static_cast<float>(-std::sin(m_samPhase)));
        const Complex rotated = in[i] * reference;

        const double error = std::atan2(static_cast<double>(rotated.imag()),
                                        static_cast<double>(rotated.real()));

        m_samFrequency += m_samBeta * error;
        m_samFrequency = std::clamp(m_samFrequency, -0.05, 0.05);
        m_samPhase += m_samFrequency + m_samAlpha * error;
        if (m_samPhase > kPi)
            m_samPhase -= kTwoPi;
        else if (m_samPhase < -kPi)
            m_samPhase += kTwoPi;

        out[i] = m_dcBlocker.process(rotated.real());
    }
    return n;
}

std::size_t Demodulator::demodFm(const Complex *in, std::size_t n, float *out) noexcept
{
    // Discriminatore a prodotto coniugato: la fase del prodotto x[n]·conj(x[n-1])
    // è l'incremento di fase, cioè la frequenza istantanea.
    for (std::size_t i = 0; i < n; ++i) {
        const Complex product = in[i] * std::conj(m_previous);
        m_previous = in[i];
        const float phase = std::atan2(product.imag(), product.real());
        out[i] = phase * m_fmScale;
    }
    return n;
}

std::size_t Demodulator::demodIq(const Complex *in, std::size_t n, float *out) noexcept
{
    // Passthrough monofonico: serve solo al monitor d'ascolto quando il canale
    // è instradato come IQ verso DECODIUM 4.
    for (std::size_t i = 0; i < n; ++i)
        out[i] = in[i].real();
    return n;
}

float Demodulator::samLockErrorHz() const noexcept
{
    return static_cast<float>(m_samFrequency * m_sampleRate / kTwoPi);
}

} // namespace dsdr::dsp
