// SPDX-License-Identifier: GPL-3.0-or-later
#include "PocsagIqReceiver.h"
#include <cmath>

namespace dsdr::dsp {
namespace { constexpr unsigned outputRate = 48000; }
PocsagIqReceiver::PocsagIqReceiver(Callback callback, void *context)
    : audio(callback, context) {}
bool PocsagIqReceiver::configure(unsigned sampleRate, unsigned baud, double channelHz) {
    rate = decimation = 0;
    taps.clear(); ring.clear();
    if (sampleRate < outputRate || sampleRate > 2000000 ||
        !std::isfinite(channelHz) || std::abs(channelHz) + 12000 >= sampleRate / 2.0 ||
        !audio.configure(double(sampleRate) / (sampleRate / outputRate), baud)) { audio.reset(); return false; }
    rate = sampleRate; channel = channelHz; decimation = rate / outputRate;
    oscillatorStep = -kTwoPi * channel / rate;
    // 8 kHz cutoff, Hamming-windowed sinc, constant duration at each rate.
    const std::size_t length = 64 * decimation + 1;
    taps.resize(length); ring.resize(length);
    const double cutoff = 8000.0 / rate, middle = (length - 1) / 2.0;
    double total = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const double x = static_cast<double>(i) - middle;
        const double sinc = x == 0 ? 2 * cutoff : std::sin(kTwoPi * cutoff * x) / (kPi * x);
        taps[i] = static_cast<float>(sinc * (0.54 - 0.46 * std::cos(kTwoPi * i / (length - 1))));
        total += taps[i];
    }
    for (auto &tap : taps) tap = static_cast<float>(tap / total);
    reset(); return true;
}
void PocsagIqReceiver::reset() {
    audio.reset(); oscillatorPhase = 0; decimPhase = 0; cursor = 0;
    previous = {}; havePrevious = false;
    for (auto &v : ring) v = {};
}
void PocsagIqReceiver::process(const Complex *samples, std::size_t count) {
    if (!rate || !samples) return;
    for (std::size_t n = 0; n < count; ++n) {
        const auto s = samples[n];
        if (!std::isfinite(s.real()) || !std::isfinite(s.imag())) { reset(); continue; }
        const Complex oscillator(static_cast<float>(std::cos(oscillatorPhase)),
                                 static_cast<float>(std::sin(oscillatorPhase)));
        oscillatorPhase += oscillatorStep;
        if (oscillatorPhase > kPi) oscillatorPhase -= kTwoPi;
        else if (oscillatorPhase < -kPi) oscillatorPhase += kTwoPi;
        ring[cursor] = s * oscillator;
        cursor = (cursor + 1) % ring.size();
        if (++decimPhase != decimation) continue;
        decimPhase = 0;
        // The windowed-sinc filter is symmetric. Pair mirrored samples to
        // halve coefficient multiplications at high input rates.
        Complex filtered{};
        std::size_t newest = cursor ? cursor - 1 : ring.size() - 1;
        std::size_t oldest = cursor;
        const auto middle = taps.size() / 2;
        for (std::size_t i=0;i<middle;++i) {
            filtered += (ring[newest] + ring[oldest]) * taps[i];
            newest = newest ? newest - 1 : ring.size() - 1;
            if (++oldest == ring.size()) oldest = 0;
        }
        filtered += ring[newest] * taps[middle];
        if (havePrevious) {
            const Complex product = filtered * std::conj(previous);
            const float discriminator = std::atan2(product.imag(), product.real());
            audio.process(&discriminator, 1);
        }
        previous = filtered; havePrevious = true;
    }
}
}
