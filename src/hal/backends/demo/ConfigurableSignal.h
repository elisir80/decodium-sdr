// SPDX-License-Identifier: GPL-3.0-or-later
// Generatore baseband RX per il backend demo. Nessun accesso a dispositivi TX.
#pragma once

#include "dsp/DspTypes.h"
#include <QVariantMap>
#include <cmath>
#include <cstdint>

namespace dsdr::hal::demo {

// Il seed, la fase e il contatore vengono ripristinati a ogni applicazione.
// Gli offset sono relativi al centro del ricevitore, non frequenze RF.
class ConfigurableSignal {
public:
    static QVariantMap defaults()
    {
        return {{"enabled", false}, {"mode", "tone"}, {"offsetHz", 1000.0},
                {"amplitude", 0.2}, {"modulationHz", 1000.0},
                {"amDepth", 0.5}, {"fmDeviationHz", 2500.0},
                {"noiseRms", 0.0}, {"seed", 1u}, {"impulsePeriod", 0u},
                {"impulseAmplitude", 0.0}, {"driftHz", 0.0}, {"driftPeriodSec", 10.0},
                {"interfererOffsetHz", 3000.0}, {"interfererAmplitude", 0.0},
                {"fadingDepth", 0.0}, {"fadingPeriodSec", 5.0}};
    }

    static bool validate(const QVariantMap &input, QVariantMap &out, QString &error)
    {
        out = defaults();
        for (auto it = input.cbegin(); it != input.cend(); ++it) {
            if (!out.contains(it.key())) { error = QStringLiteral("Unknown parameter: ") + it.key(); return false; }
            out[it.key()] = it.value();
        }
        const QString mode = out.value("mode").toString();
        if (mode != "tone" && mode != "am" && mode != "fm") {
            error = QStringLiteral("Invalid mode"); return false;
        }
        auto number = [&](const char *key, double lo, double hi) {
            bool ok = false;
            const double value = out.value(QLatin1String(key)).toDouble(&ok);
            if (!ok || !std::isfinite(value) || value < lo || value > hi) {
                error = QStringLiteral("Invalid parameter: ") + QLatin1String(key); return false;
            }
            out[QLatin1String(key)] = value;
            return true;
        };
        if (!number("offsetHz", -90000, 90000) || !number("amplitude", 0, 1)
            || !number("modulationHz", 1, 20000) || !number("amDepth", 0, 1)
            || !number("fmDeviationHz", 0, 75000) || !number("noiseRms", 0, 1)
            || !number("seed", 0, 4294967295.0) || !number("impulsePeriod", 0, 100000000)
            || !number("impulseAmplitude", 0, 1) || !number("driftHz", 0, 90000)
            || !number("driftPeriodSec", 0.1, 3600)
            || !number("interfererOffsetHz", -90000, 90000)
            || !number("interfererAmplitude", 0, 1) || !number("fadingDepth", 0, 1)
            || !number("fadingPeriodSec", 0.1, 3600)) return false;
        if (std::floor(out["seed"].toDouble()) != out["seed"].toDouble()
            || std::floor(out["impulsePeriod"].toDouble()) != out["impulsePeriod"].toDouble()) {
            error = QStringLiteral("Seed and impulse period must be integers"); return false;
        }
        if (out["enabled"].metaType().id() != QMetaType::Bool) {
            error = QStringLiteral("Enabled must be boolean"); return false;
        }
        return true;
    }

    void configure(double sampleRate, const QVariantMap &config)
    {
        m_rate = sampleRate;
        const QString mode = config.value("mode").toString();
        m_mode = mode == QLatin1String("am") ? Mode::Am : mode == QLatin1String("fm") ? Mode::Fm : Mode::Tone;
        m_offset = config.value("offsetHz").toDouble();
        m_amplitude = config.value("amplitude").toDouble();
        m_modulation = config.value("modulationHz").toDouble();
        m_depth = config.value("amDepth").toDouble();
        m_deviation = config.value("fmDeviationHz").toDouble();
        m_noise = config.value("noiseRms").toDouble();
        m_rng = static_cast<std::uint32_t>(config.value("seed").toDouble());
        m_period = static_cast<std::uint64_t>(config.value("impulsePeriod").toDouble());
        m_impulse = config.value("impulseAmplitude").toDouble();
        m_drift = config.value("driftHz", 0.0).toDouble();
        m_driftStep = dsp::kTwoPi / (m_rate * config.value("driftPeriodSec", 10.0).toDouble());
        m_interfererAmplitude = config.value("interfererAmplitude", 0.0).toDouble();
        m_interfererStep = dsp::kTwoPi * config.value("interfererOffsetHz", 3000.0).toDouble() / m_rate;
        m_fadingDepth = config.value("fadingDepth", 0.0).toDouble();
        m_fadingStep = dsp::kTwoPi / (m_rate * config.value("fadingPeriodSec", 5.0).toDouble());
        m_driftPhase = m_interfererPhase = m_fadingPhase = 0.0;
        m_index = 0;
        m_carrierPhase = 0.0;
        m_modPhase = 0.0;
    }

    void generate(dsp::Complex *out, std::size_t count) noexcept
    {
        for (std::size_t i = 0; i < count; ++i, ++m_index) {
            const double mod = m_mode == Mode::Tone ? 0.0 : std::sin(m_modPhase);
            const double fade = m_fadingDepth > 0.0
                ? 1.0 - m_fadingDepth * 0.5 * (1.0 - std::cos(m_fadingPhase)) : 1.0;
            const double level = m_amplitude * fade * (m_mode == Mode::Am ? 1.0 + m_depth * mod : 1.0);
            float re = static_cast<float>(level * std::cos(m_carrierPhase));
            float im = static_cast<float>(level * std::sin(m_carrierPhase));
            if (m_interfererAmplitude > 0.0) {
                re += static_cast<float>(m_interfererAmplitude * std::cos(m_interfererPhase));
                im += static_cast<float>(m_interfererAmplitude * std::sin(m_interfererPhase));
                m_interfererPhase = std::remainder(m_interfererPhase + m_interfererStep, dsp::kTwoPi);
            }
            if (m_noise > 0) { re += noise(); im += noise(); }
            if (m_period && m_index % m_period == 0) re += static_cast<float>(m_impulse);
            out[i] = {re, im};
            const double frequency = m_offset + (m_mode == Mode::Fm ? m_deviation * mod : 0.0)
                + (m_drift > 0.0 ? m_drift * std::sin(m_driftPhase) : 0.0);
            if (m_drift > 0.0)
                m_driftPhase = std::remainder(m_driftPhase + m_driftStep, dsp::kTwoPi);
            if (m_fadingDepth > 0.0)
                m_fadingPhase = std::remainder(m_fadingPhase + m_fadingStep, dsp::kTwoPi);
            m_carrierPhase = std::remainder(m_carrierPhase + dsp::kTwoPi * frequency / m_rate, dsp::kTwoPi);
            m_modPhase = std::remainder(m_modPhase + dsp::kTwoPi * m_modulation / m_rate, dsp::kTwoPi);
        }
    }
private:
    float noise() noexcept
    {
        // LCG specificato, somma di quattro uniformi: media zero e RMS unitario.
        double sum = 0;
        for (int i = 0; i < 4; ++i) {
            m_rng = m_rng * 1664525u + 1013904223u;
            sum += static_cast<double>(m_rng >> 8) / 16777216.0;
        }
        return static_cast<float>((sum - 2.0) * 1.7320508075688772 * m_noise);
    }
    enum class Mode { Tone, Am, Fm };
    Mode m_mode = Mode::Tone;
    double m_rate = 192000, m_offset = 0, m_amplitude = 0, m_modulation = 0;
    double m_depth = 0, m_deviation = 0, m_noise = 0, m_impulse = 0;
    double m_carrierPhase = 0, m_modPhase = 0;
    double m_drift = 0, m_driftStep = 0, m_driftPhase = 0;
    double m_interfererAmplitude = 0, m_interfererStep = 0, m_interfererPhase = 0;
    double m_fadingDepth = 0, m_fadingStep = 0, m_fadingPhase = 0;
    std::uint32_t m_rng = 0;
    std::uint64_t m_period = 0, m_index = 0;
};

} // namespace dsdr::hal::demo
