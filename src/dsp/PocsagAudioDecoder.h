// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PocsagDecoder.h"
#include <cstddef>

namespace dsdr::dsp {
// Input is DC-coupled, frequency-discriminator audio (two NRZ levels), not IQ
// or a pair of audio-frequency tones. Call on a worker; callback is synchronous.
class PocsagAudioDecoder {
public:
    using Callback = PocsagDecoder::Callback;
    explicit PocsagAudioDecoder(Callback callback, void *context = nullptr);
    // Sample rate >= 8 samples/symbol; supported rates 512, 1200, 2400.
    bool configure(double sampleRate, unsigned baud);
    void reset();
    void process(const float *samples, std::size_t count);
private:
    PocsagDecoder decoder;
    double rate = 0, period = 0, phase = 0, dc = 0, envelope = 0, sum = 0;
    bool haveLevel = false, level = false;
};
}
