// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/demo/SyntheticBand.h"

#include <algorithm>
#include <cmath>

namespace dsdr::hal::demo {

using dsp::Complex;
using dsp::kTwoPi;

namespace {

float dbToLinear(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

/// Inviluppo sillabico: modula l'ampiezza a 2–4 Hz così la voce sintetica ha
/// pause e picchi, e l'AGC viene esercitato come con una voce vera.
double syllableEnvelope(double phase)
{
    const double s = 0.5 + 0.5 * std::sin(phase);
    return 0.15 + 0.85 * s * s;
}

} // namespace

std::vector<StationSpec> SyntheticBand::hfBandPlan()
{
    // Un pomeriggio sui 40 metri: CW in basso, digitale a 7.074, fonia in alto.
    return {
        {7'005'000, SignalKind::Cw, -42.0, QStringLiteral("CQ CQ DE IU8LMC IU8LMC K   "), 24.0, 11.0, 12.0, 600.0},
        {7'012'500, SignalKind::Cw, -55.0, QStringLiteral("TEST DE 9H1SR 9H1SR K   "), 28.0, 7.0, 18.0, 600.0},
        {7'023'000, SignalKind::Cw, -63.0, QStringLiteral("QRL? DE DL1ABC K   "), 18.0, 23.0, 9.0, 600.0},
        {7'030'000, SignalKind::Cw, -70.0, QStringLiteral("VVV DE OH2BEACON   "), 15.0, 0.0, 0.0, 600.0},
        {7'040'000, SignalKind::Carrier, -75.0, QString(), 0.0, 0.0, 0.0, 0.0},
        {7'074'000, SignalKind::Digital, -50.0, QString(), 0.0, 0.0, 0.0, 1240.0},
        {7'100'000, SignalKind::Am, -48.0, QString(), 0.0, 17.0, 10.0, 800.0},
        {7'145'000, SignalKind::SsbLower, -40.0, QString(), 0.0, 13.0, 14.0, 520.0},
        {7'182'000, SignalKind::SsbLower, -58.0, QString(), 0.0, 29.0, 20.0, 640.0},
    };
}

std::vector<StationSpec> SyntheticBand::vhfBandPlan()
{
    // Due metri: ripetitori FM, un beacon e una SSB in gamma bassa.
    return {
        {144'300'000, SignalKind::SsbUpper, -52.0, QString(), 0.0, 19.0, 8.0, 560.0},
        {144'400'000, SignalKind::Carrier, -68.0, QString(), 0.0, 0.0, 0.0, 0.0},
        {144'800'000, SignalKind::Cw, -60.0, QStringLiteral("VVV DE IQ8XX JN70 BEACON   "), 20.0, 0.0, 0.0, 600.0},
        {145'625'000, SignalKind::Fm, -38.0, QString(), 0.0, 31.0, 6.0, 700.0},
        {145'750'000, SignalKind::Fm, -46.0, QString(), 0.0, 0.0, 0.0, 900.0},
    };
}

void SyntheticBand::configure(double sampleRate,
                              qint64 centerHz,
                              const std::vector<StationSpec> &stations)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 192000.0;
    m_centerHz = centerHz;

    m_stations.clear();
    m_stations.reserve(stations.size());
    for (const StationSpec &spec : stations) {
        Station station;
        station.spec = spec;
        station.keyer.configure(m_sampleRate, spec.wpm);
        if (!spec.text.isEmpty())
            station.keyer.setText(spec.text);
        m_stations.push_back(std::move(station));
    }

    setCenterFrequency(centerHz);
    reset();
}

void SyntheticBand::setCenterFrequency(qint64 centerHz)
{
    m_centerHz = centerHz;
    m_noiseAmplitude = dbToLinear(m_noiseFloorDb);
    for (Station &station : m_stations)
        prepareStation(station);
}

void SyntheticBand::prepareStation(Station &station)
{
    station.offsetHz = static_cast<double>(station.spec.frequencyHz - m_centerHz);
    const double nyquist = m_sampleRate * 0.5;

    // Fuori banda = invisibile. Con un margine del 2% per non far comparire
    // stazioni proprio sul bordo, dove il filtro anti-alias le deformerebbe.
    station.inBand = std::abs(station.offsetHz) < nyquist * 0.98;
    station.amplitude = dbToLinear(station.spec.amplitudeDb);

    const double w = kTwoPi * station.offsetHz / m_sampleRate;
    station.step = Complex(static_cast<float>(std::cos(w)), static_cast<float>(std::sin(w)));
    station.phasor = Complex(1.0f, 0.0f);
}

void SyntheticBand::reset()
{
    m_noiseAmplitude = dbToLinear(m_noiseFloorDb);
    for (Station &station : m_stations) {
        station.phasor = Complex(1.0f, 0.0f);
        station.normalizeCounter = 0;
        station.keyer.reset();
        station.modPhase[0] = 0.0;
        station.modPhase[1] = 1.1;
        station.modPhase[2] = 2.3;
        station.syllablePhase = 0.0;
        station.qsbPhase = 0.0;
        station.digitalClock = 0.0;
        station.digitalTone = 0;
    }
}

Complex SyntheticBand::modulationSampleImpl(Station &station) noexcept
{
    const StationSpec &spec = station.spec;
    const double dt = 1.0 / m_sampleRate;

    // Fading lento comune a tutte le modalità.
    float qsb = 1.0f;
    if (spec.qsbPeriodSeconds > 0.0 && spec.qsbDepthDb > 0.0) {
        station.qsbPhase += kTwoPi * dt / spec.qsbPeriodSeconds;
        if (station.qsbPhase > kTwoPi)
            station.qsbPhase -= kTwoPi;
        const double depth = std::pow(10.0, -spec.qsbDepthDb / 20.0);
        const double t = 0.5 + 0.5 * std::sin(station.qsbPhase);
        qsb = static_cast<float>(depth + (1.0 - depth) * t);
    }

    const float amp = station.amplitude * qsb;

    switch (spec.kind) {
    case SignalKind::Cw:
        return Complex(amp * station.keyer.nextEnvelope(), 0.0f);

    case SignalKind::Carrier:
        return Complex(amp, 0.0f);

    case SignalKind::Am: {
        station.syllablePhase += kTwoPi * 3.0 * dt;
        station.modPhase[0] += kTwoPi * spec.toneHz * dt;
        const double envelope = syllableEnvelope(station.syllablePhase);
        const double audio = std::sin(station.modPhase[0]) * envelope;
        return Complex(static_cast<float>(amp * (1.0 + 0.7 * audio)), 0.0f);
    }

    case SignalKind::SsbUpper:
    case SignalKind::SsbLower: {
        // Tre formanti con inviluppi sillabici sfalsati: il risultato occupa
        // 300–2700 Hz come una voce, e — essendo costruito come segnale
        // analitico — è davvero a banda laterale unica.
        station.syllablePhase += kTwoPi * 2.6 * dt;
        const double envelope = syllableEnvelope(station.syllablePhase);

        static constexpr double kFormantRatio[3] = {1.0, 1.7, 2.9};
        static constexpr double kFormantLevel[3] = {1.0, 0.55, 0.25};

        double re = 0.0;
        double im = 0.0;
        for (int k = 0; k < 3; ++k) {
            station.modPhase[k] += kTwoPi * spec.toneHz * kFormantRatio[k] * dt;
            if (station.modPhase[k] > kTwoPi)
                station.modPhase[k] -= kTwoPi;
            re += kFormantLevel[k] * std::cos(station.modPhase[k]);
            im += kFormantLevel[k] * std::sin(station.modPhase[k]);
        }
        if (spec.kind == SignalKind::SsbLower)
            im = -im; // coniugato = banda laterale inferiore

        const double scale = amp * envelope * 0.5;
        return Complex(static_cast<float>(re * scale), static_cast<float>(im * scale));
    }

    case SignalKind::Fm: {
        station.syllablePhase += kTwoPi * 2.2 * dt;
        station.modPhase[0] += kTwoPi * spec.toneHz * dt;
        const double envelope = syllableEnvelope(station.syllablePhase);
        const double deviation = 2500.0 * envelope;
        // Integrazione della modulante: la fase è l'integrale della frequenza.
        station.modPhase[1] += kTwoPi * deviation * std::sin(station.modPhase[0]) * dt;
        if (station.modPhase[1] > kTwoPi)
            station.modPhase[1] -= kTwoPi;
        else if (station.modPhase[1] < -kTwoPi)
            station.modPhase[1] += kTwoPi;
        return Complex(static_cast<float>(amp * std::cos(station.modPhase[1])),
                       static_cast<float>(amp * std::sin(station.modPhase[1])));
    }

    case SignalKind::Digital: {
        // Salto di 8 toni a 6.25 Hz di passo, 0.16 s per simbolo, con la
        // cadenza a 15 s tipica di FT8: serve a validare il ponte IQ verso
        // DECODIUM 4 senza dover generare un vero messaggio codificato.
        station.digitalClock += dt;
        if (station.digitalClock >= 15.0)
            station.digitalClock -= 15.0;

        const bool transmitting = station.digitalClock < 12.64;
        if (!transmitting)
            return Complex(0.0f, 0.0f);

        const int symbol = static_cast<int>(station.digitalClock / 0.16) % 8;
        const double toneOffset = spec.toneHz + symbol * 6.25;
        station.modPhase[0] += kTwoPi * toneOffset * dt;
        if (station.modPhase[0] > kTwoPi)
            station.modPhase[0] -= kTwoPi;
        return Complex(static_cast<float>(amp * std::cos(station.modPhase[0])),
                       static_cast<float>(amp * std::sin(station.modPhase[0])));
    }
    }

    return Complex(0.0f, 0.0f);
}

void SyntheticBand::generate(Complex *out, std::size_t n) noexcept
{
    // 1) Rumore di fondo: gaussiano complesso, il "respiro" della banda.
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = Complex(m_noiseAmplitude * m_gauss(m_rng),
                         m_noiseAmplitude * m_gauss(m_rng));
    }

    // 2) Stazioni: ciascuna genera la propria banda base e viene traslata
    //    alla sua frequenza dal proprio fasore.
    for (Station &station : m_stations) {
        if (!station.inBand)
            continue;

        for (std::size_t i = 0; i < n; ++i) {
            const Complex baseband = modulationSampleImpl(station);
            out[i] += baseband * station.phasor;

            station.phasor *= station.step;
            if (++station.normalizeCounter >= 2048) {
                station.normalizeCounter = 0;
                const float mag = std::sqrt(dsp::magnitudeSquared(station.phasor));
                station.phasor = (mag > 1e-6f) ? station.phasor / mag : Complex(1.0f, 0.0f);
            }
        }
    }
}

} // namespace dsdr::hal::demo
