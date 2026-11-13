// Experimental MF/HF DSC audio front end, GPL-3.0-or-later.
#include "FskAudioRx.h"
#include <cmath>
#include <utility>
namespace dsc_rx {
namespace { constexpr double pi = 3.14159265358979323846; }
FskAudioRx::FskAudioRx(Callback cb) : callback_(std::move(cb)) {}
void FskAudioRx::reset() {
    decoder_.reset(); high_ = {}; low_ = {}; highSum_ = {}; lowSum_ = {};
    index_ = count_ = 0; oscillatorHigh_ = oscillatorLow_ = clock_ = smoothed_ = 0; lastSign_ = 0;
}
void FskAudioRx::process(const float* samples, std::size_t count) {
    for (std::size_t n=0; n<count; ++n) {
        const double x = std::isfinite(samples[n]) ? samples[n] : 0.0;
        IQ hi{x * std::cos(oscillatorHigh_), x * std::sin(oscillatorHigh_)};
        IQ lo{x * std::cos(oscillatorLow_), x * std::sin(oscillatorLow_)};
        oscillatorHigh_ += 2*pi*1785/24000; oscillatorLow_ += 2*pi*1615/24000;
        if (oscillatorHigh_ >= 2*pi) oscillatorHigh_ -= 2*pi;
        if (oscillatorLow_ >= 2*pi) oscillatorLow_ -= 2*pi;
        highSum_.i += hi.i - high_[index_].i; highSum_.q += hi.q - high_[index_].q;
        lowSum_.i += lo.i - low_[index_].i; lowSum_.q += lo.q - low_[index_].q;
        high_[index_] = hi; low_[index_] = lo; index_ = (index_ + 1) % high_.size();
        if (count_ < high_.size()) ++count_;
        if (count_ < high_.size()) continue;
        const double eh = highSum_.i*highSum_.i + highSum_.q*highSum_.q;
        const double el = lowSum_.i*lowSum_.i + lowSum_.q*lowSum_.q;
        double discr = (el - eh) / (el + eh + 1e-12);
        smoothed_ += 0.12*(discr - smoothed_);
        int sign = smoothed_ > .10 ? 1 : smoothed_ < -.10 ? -1 : lastSign_;
        // The correlator and smoothing delay the observed transition. Correct
        // the clock gently to avoid following short noisy discriminator flips.
        if (lastSign_ && sign != lastSign_) {
            double error = clock_ - .30;
            if (error > .5) error -= 1;
            if (error < -.5) error += 1;
            clock_ -= .10*error;
            if (clock_ < 0) clock_ += 1;
            if (clock_ >= 1) clock_ -= 1;
        }
        lastSign_ = sign;
        clock_ += 1.0/240;
        if (clock_ >= 1) {
            clock_ -= 1;
            if (auto call = decoder_.push(smoothed_ > 0)) if (callback_) callback_(*call);
        }
    }
}
}
