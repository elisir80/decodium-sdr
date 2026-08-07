// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

namespace dsdr::dsp {

double besselI0(double x)
{
    // Serie di potenze: I0(x) = Σ ((x/2)^k / k!)^2. Converge rapidamente per
    // gli x che interessano un beta di Kaiser (≤ 20).
    double sum = 1.0;
    double term = 1.0;
    const double halfSquared = (x * 0.5) * (x * 0.5);
    for (int k = 1; k < 64; ++k) {
        term *= halfSquared / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-16)
            break;
    }
    return sum;
}

double kaiserBeta(double stopbandDb)
{
    if (stopbandDb > 50.0)
        return 0.1102 * (stopbandDb - 8.7);
    if (stopbandDb >= 21.0)
        return 0.5842 * std::pow(stopbandDb - 21.0, 0.4) + 0.07886 * (stopbandDb - 21.0);
    return 0.0;
}

int estimateTaps(double transitionHz, double sampleRate, double stopbandDb)
{
    if (transitionHz <= 0.0 || sampleRate <= 0.0)
        return 63;
    const double normalized = transitionHz / sampleRate;
    double taps = (stopbandDb - 8.0) / (2.285 * kTwoPi * normalized);
    int n = static_cast<int>(std::ceil(taps));
    n = std::clamp(n, 15, static_cast<int>(kMaxFirTaps) - 1);
    if ((n & 1) == 0)
        ++n; // simmetria dispari: ritardo di gruppo intero
    return n;
}

std::vector<float> designLowpass(double cutoffHz, double sampleRate, int numTaps, double beta)
{
    if (numTaps < 3)
        numTaps = 3;
    if ((numTaps & 1) == 0)
        ++numTaps;

    std::vector<float> taps(static_cast<std::size_t>(numTaps));
    const double fc = std::clamp(cutoffHz / sampleRate, 1e-6, 0.5 - 1e-6);
    const double center = (numTaps - 1) * 0.5;
    const double i0beta = besselI0(beta);

    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n) {
        const double t = n - center;
        // sinc ideale
        const double sinc = (std::abs(t) < 1e-9) ? (2.0 * fc)
                                                 : std::sin(kTwoPi * fc * t) / (kPi * t);
        // finestra di Kaiser
        const double ratio = t / center;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / i0beta;
        const double h = sinc * w;
        taps[static_cast<std::size_t>(n)] = static_cast<float>(h);
        sum += h;
    }

    // Normalizzazione a guadagno DC unitario.
    if (std::abs(sum) > 1e-12) {
        const float inv = static_cast<float>(1.0 / sum);
        for (float &v : taps)
            v *= inv;
    }
    return taps;
}

void designBandpassInto(std::vector<Complex> &out,
                        double loHz,
                        double hiHz,
                        double sampleRate,
                        int numTaps,
                        double beta)
{
    if (loHz > hiHz)
        std::swap(loHz, hiHz);

    const double halfWidth = std::max(1.0, (hiHz - loHz) * 0.5);
    const double center = (hiHz + loHz) * 0.5;

    const std::vector<float> proto = designLowpass(halfWidth, sampleRate, numTaps, beta);
    const std::size_t n = proto.size();
    out.resize(n);

    const double centerIndex = (static_cast<double>(n) - 1.0) * 0.5;
    const double omega = kTwoPi * center / sampleRate;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) - centerIndex;
        const double phase = omega * t;
        out[i] = Complex(static_cast<float>(proto[i] * std::cos(phase)),
                         static_cast<float>(proto[i] * std::sin(phase)));
    }
}

std::vector<Complex> designBandpass(double loHz,
                                    double hiHz,
                                    double sampleRate,
                                    int numTaps,
                                    double beta)
{
    std::vector<Complex> out;
    out.reserve(kMaxFirTaps);
    designBandpassInto(out, loHz, hiHz, sampleRate, numTaps, beta);
    return out;
}

} // namespace dsdr::dsp
