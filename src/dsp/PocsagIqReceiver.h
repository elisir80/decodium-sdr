// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DspTypes.h"
#include "PocsagAudioDecoder.h"
#include <vector>

namespace dsdr::dsp {
// Standalone RX: translated complex IQ -> low-pass/decimation -> FM
// discriminator -> POCSAG audio decoder. Processing and callbacks are
// synchronous. The owner must call it on a worker and reset on input gaps.
class PocsagIqReceiver {
public:
    using Callback = PocsagDecoder::Callback;
    explicit PocsagIqReceiver(Callback callback, void *context = nullptr);
    // Rates from 48 ksample/s to 2 Msample/s; integer decimation produces
    // at least 48 ksample/s and the decoder uses its actual output rate.
    // High rates cost more worker CPU.
    // channelHz is the signal centre relative to input IQ centre, in hertz.
    bool configure(unsigned sampleRate, unsigned baud, double channelHz);
    void reset();
    void process(const Complex *samples, std::size_t count);
private:
    PocsagAudioDecoder audio;
    unsigned rate = 0, decimation = 0, decimPhase = 0;
    double channel = 0, oscillatorPhase = 0, oscillatorStep = 0;
    std::vector<float> taps;
    std::vector<Complex> ring;
    std::size_t cursor = 0;
    Complex previous{};
    bool havePrevious = false;
};
}
