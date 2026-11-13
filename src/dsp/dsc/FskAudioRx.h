// Experimental MF/HF DSC audio front end, GPL-3.0-or-later.
#pragma once
#include "DscRx.h"
#include <array>
#include <cstddef>
#include <functional>

namespace dsc_rx {
// Single-thread confined; caller supplies serialized blocks on a worker.
// 24 kHz mono real audio, 100 baud, 170 Hz shift, known 1700 Hz centre.
class FskAudioRx {
public:
    using Callback = std::function<void(const Distress&)>;
    explicit FskAudioRx(Callback callback);
    void process(const float* samples, std::size_t count);
    void reset(); // mandatory after a missing block, retune, or stream restart
private:
    struct IQ { double i = 0, q = 0; };
    Callback callback_;
    Decoder decoder_;
    std::array<IQ,120> high_{}, low_{};
    IQ highSum_{}, lowSum_{};
    std::size_t index_ = 0, count_ = 0;
    double oscillatorHigh_ = 0, oscillatorLow_ = 0;
    double clock_ = 0, smoothed_ = 0;
    int lastSign_ = 0;
};
}
