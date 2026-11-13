// SPDX-License-Identifier: GPL-3.0-or-later
#include "PocsagAudioDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsdr::dsp {
PocsagAudioDecoder::PocsagAudioDecoder(Callback callback, void *context)
    : decoder(callback, context) {}
bool PocsagAudioDecoder::configure(double sampleRate, unsigned baud) {
    if (!std::isfinite(sampleRate) || (baud != 512 && baud != 1200 && baud != 2400)
        || sampleRate < 8.0 * baud || sampleRate > 384000) {
        rate = period = 0; reset(); return false;
    }
    rate = sampleRate; period = rate / baud; reset(); return true;
}
void PocsagAudioDecoder::reset() {
    decoder.reset(); phase = dc = envelope = sum = 0;
    haveLevel = level = false;
}
void PocsagAudioDecoder::process(const float *samples, std::size_t count) {
    if (!samples || !rate) return;
    for (std::size_t i = 0; i < count; ++i) {
        const double sample = samples[i];
        if (!std::isfinite(sample)) { reset(); continue; }
        // Slow offset estimator; preamble is balanced. Hysteresis rejects small
        // crossings caused by noise. PLL corrects phase only, not baud rate.
        dc += (sample - dc) / (rate * 0.12);
        const double x = sample - dc;
        envelope += (std::abs(x) - envelope) / (rate * 0.01);
        const double threshold = envelope * 0.35;
        bool next = level;
        if (x > threshold) next = true;
        else if (x < -threshold) next = false;
        if (haveLevel && next != level) {
            double error = phase;
            if (error > period / 2) error -= period;
            // A bounded phase correction limits false edges in noise.
            phase -= std::clamp(error * 0.25, -period * 0.12, period * 0.12);
            if (phase < 0) phase += period;
            if (phase >= period) phase -= period;
        }
        if (std::abs(x) > threshold) { haveLevel = true; level = next; }
        sum += x; phase += 1;
        if (phase >= period) {
            phase -= period;
            const std::uint8_t bit = sum >= 0;
            decoder.process(&bit, 1);
            sum = 0;
        }
    }
}
}
