// SPDX-License-Identifier: GPL-3.0-or-later
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "Vdl2Acquisition.h"
#include <algorithm>
#include <cmath>
namespace dsdr::vdl2 {
std::optional<Acquisition> findPreamble(const std::complex<float>* x,std::size_t n,double rate,double floor) {
    constexpr double pi=3.14159265358979323846, baud=10500;
    constexpr int phase[16]={0,3,-3,1,1,2,0,4,-3,4,-2,3,1,-2,-3,0};
    if(!x || n>2000000 || !std::isfinite(rate) || rate<baud*4 || rate>4000000 ||
       !std::isfinite(floor) || floor<0) return {};
    const double T=rate/baud;
    const auto span=std::size_t(std::ceil(15*T));
    if(n<=span+1) return {};
    std::complex<double> correction[15];
    for(int q=0;q<15;++q) correction[q]=std::polar(1.0,-(phase[q+1]-phase[q])*pi/4);
    Acquisition best;
    for(std::size_t t=0;t<n-span-1;++t) {
        std::complex<double> s[16],d{};
        double energy=0;
        bool valid=true;
        for(int q=0;q<16;++q) {
            const double pos=t+q*T;
            const auto idx=std::size_t(pos);
            const float frac=float(pos-idx);
            const auto z=x[idx]*(1-frac)+x[idx+1]*frac;
            if(!std::isfinite(z.real()) || !std::isfinite(z.imag())) { valid=false; break; }
            s[q]={z.real(),z.imag()}; const auto e=std::norm(s[q]);
            energy+=e;
        }
        energy/=16;
        if(!valid || energy<=floor || energy<=1e-20) continue;
        // Normalise by differential-pair magnitudes. A weak pair gate rejects
        // matches on burst edges while tolerating fades inside the preamble.
        // Motivation: off-air diagnostics and the differential metric in
        // airframesio/xng's VDL2 receiver (MIT/Apache-2.0).
        double norm=0, minimum=1e100;
        for(int q=0;q<15;++q) {
            const auto v=s[q+1]*std::conj(s[q]);
            d+=v*correction[q]; norm+=std::abs(v); minimum=std::min(minimum,std::abs(v));
        }
        if(norm<1e-20 || minimum<0.01*norm/15) continue;
        const double score=std::abs(d)/norm;
        if(score>best.correlation) best={t,std::arg(d)*baud/(2*pi),score};
    }
    return best.correlation>=0.75 ? std::optional<Acquisition>(best) : std::nullopt;
}
}
