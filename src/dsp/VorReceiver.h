// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VorIdentDecoder.h"
#include <array>
#include <complex>
#include <cstddef>

namespace dsdr::dsp {
// Experimental CVOR only. Caller owns this object on one worker thread.
// Callback runs synchronously; invalid windows are reported with valid=false.
class VorReceiver {
public:
    struct Result { bool valid; double radialDegrees; double variableAmplitude; double referenceAmplitude; double subcarrierAmplitude; std::array<char,7> identifier{}; };
    using Callback = void (*)(const Result&, void*);
    explicit VorReceiver(Callback callback, void* context = nullptr);
    // Composite AM audio or complex IQ at 48 kHz. IQ must already be channel-filtered and centred.
    bool configure(unsigned sampleRate);
    void reset();
    void processAudio(const float* samples, std::size_t count);
    void processIq(const std::complex<float>* samples, std::size_t count);
private:
    void sample(double x);
    Callback callback_; void* context_; unsigned rate_ = 0;
    double phase9960_ = 0, phase30_ = 0;
    std::complex<double> lp1_{}, lp2_{}, prev_{}, var_{}, ref_{};
    double sub_ = 0, energy_ = 0; std::size_t count_ = 0, warmup_ = 0;
    bool havePrev_ = false;
    VorIdentDecoder ident_;
};
}
