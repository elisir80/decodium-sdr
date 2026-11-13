// SPDX-License-Identifier: GPL-3.0-or-later
#include "VorReceiver.h"
#include <cmath>
#include <algorithm>

namespace dsdr::dsp {
namespace { constexpr double pi = 3.14159265358979323846; }
VorReceiver::VorReceiver(Callback cb, void* ctx): callback_(cb), context_(ctx) {}
bool VorReceiver::configure(unsigned rate) {
    rate_ = rate == 48000 ? rate : 0; reset(); return rate_ != 0;
}
void VorReceiver::reset() {
    phase9960_=phase30_=0; lp1_=lp2_=prev_=var_=ref_={};
    sub_=energy_=0; count_=warmup_=0; havePrev_=false;
    ident_.reset();
}
void VorReceiver::processAudio(const float* samples, std::size_t n) {
    if (!rate_ || !samples) return;
    for (std::size_t i=0;i<n;++i) sample(std::isfinite(samples[i]) ? samples[i] : 0);
}
void VorReceiver::processIq(const std::complex<float>* samples, std::size_t n) {
    if (!rate_ || !samples) return;
    for (std::size_t i=0;i<n;++i) {
        const auto s=samples[i];
        sample(std::isfinite(s.real()) && std::isfinite(s.imag()) ? std::abs(s) : 0);
    }
}
void VorReceiver::sample(double x) {
    const float identSample=float(x); ident_.process(&identSample,1);
    // Two complex one-pole filters retain the FM sidebands after downconversion.
    constexpr double alpha = 0.19;
    const std::complex<double> mix(std::cos(phase9960_),-std::sin(phase9960_));
    lp1_ += alpha*(x*mix-lp1_); lp2_ += alpha*(lp1_-lp2_);
    double discriminator=0;
    if (havePrev_ && std::norm(lp2_) > 1e-10 && std::norm(prev_) > 1e-10)
        discriminator=std::arg(lp2_*std::conj(prev_))*rate_/(2*pi);
    prev_=lp2_; havePrev_=true;
    const std::complex<double> osc(std::cos(phase30_),-std::sin(phase30_));
    phase9960_+=2*pi*9960/rate_; if (phase9960_>=2*pi) phase9960_-=2*pi;
    phase30_+=2*pi*30/rate_; if (phase30_>=2*pi) phase30_-=2*pi;
    if (warmup_ < rate_/4) { ++warmup_; return; }
    var_ += x*osc; ref_ += discriminator*osc; sub_ += 2*std::abs(lp2_); energy_ += x*x; ++count_;
    if (count_ == rate_) {
        const double va=2*std::abs(var_)/count_, re=2*std::abs(ref_)/count_, sc=sub_/count_;
        // Relative amplitudes retain the same decision at different input
        // gains. Require a floor and both branches; this is not integrity monitoring.
        const double rms=std::sqrt(energy_/count_);
        const bool valid=rms>1e-4 && va>rms*0.04 && re>100 && sc>rms*0.08;
        // The low-pass group delay also delays the 30 Hz FM modulation.
        const double w=2*pi*30/rate_;
        const double delayPhase=2*std::atan2((1-alpha)*std::sin(w),1-(1-alpha)*std::cos(w));
        double angle=std::arg(ref_)-std::arg(var_)+delayPhase;
        angle=std::fmod(angle*180/pi+720,360);
        if (callback_) {
            Result result{valid,angle,va,re,sc};
            const auto &id=ident_.identifier();
            std::copy_n(id.begin(),std::min(id.size(),result.identifier.size()-1),result.identifier.begin());
            callback_(result,context_);
        }
        count_=0; var_=ref_={}; sub_=energy_=0;
    }
}
}
