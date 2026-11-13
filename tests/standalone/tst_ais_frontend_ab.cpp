// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AisIqFrontend.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using Sample=std::complex<float>;
constexpr double pi=3.14159265358979323846;
void capture(void *p,const Sample *data,std::size_t n) noexcept {
    if(data) static_cast<std::vector<Sample>*>(p)->insert(static_cast<std::vector<Sample>*>(p)->end(),data,data+n);
}
int main() {
    for(unsigned rate:{192000u,200000u,960000u,2400000u}) {
        constexpr double offset=25000.; const unsigned factor=rate/48000;
        const std::size_t length=32*factor+1; std::vector<float> coeff(length);
        std::vector<Sample> ring(length), actual, expected; double total=0;
        for(std::size_t i=0;i<length;++i) {
            const double x=double(i)-double(length-1)/2;
            const double h=x==0?23000./rate:std::sin(2*pi*11500*x/rate)/(pi*x);
            coeff[i]=float(h*(0.54-0.46*std::cos(2*pi*i/(length-1)))); total+=coeff[i];
        }
        for(auto& c:coeff)c=float(c/total);
        const double angle=-2*pi*offset/rate;
        std::complex<double> oscillator{1,0}, step{std::cos(angle),std::sin(angle)};
        std::size_t cursor=0; unsigned phase=0;
        std::vector<Sample> input(rate/10);
        for(std::size_t i=0;i<input.size();++i) {
            const double t=double(i)/rate;
            input[i]={float(.6*std::cos(2*pi*28000*t)+.1*std::sin(2*pi*60000*t)),float(.6*std::sin(2*pi*28000*t))};
            ring[cursor]=input[i]*Sample(float(oscillator.real()),float(oscillator.imag()));
            cursor=(cursor+1)%length; oscillator*=step;
            const double power=std::norm(oscillator);
            if(power<0.999999 || power>1.000001) oscillator/=std::sqrt(power);
            if(++phase<factor)continue;
            phase=0; Sample sum{}; auto at=cursor;
            for(float c:coeff) { at=at?at-1:length-1; sum+=ring[at]*c; }
            expected.push_back(sum);
        }
        dsdr::dsp::AisIqFrontend frontend(capture,&actual);
        if(!frontend.configure(rate,offset)) return 1;
        for(std::size_t i=0;i<input.size();) {
            const auto n=std::min<std::size_t>(317,input.size()-i);
            frontend.process(input.data()+i,n); i+=n;
        }
        if(actual.size()!=expected.size()) return 2;
        for(std::size_t i=0;i<actual.size();++i)
            if(std::abs(actual[i]-expected[i])>0.00002f) { std::fprintf(stderr,"rate %u sample %zu differs\n",rate,i); return 3; }
    }
    std::puts("AIS symmetric FIR versus original convolution: four rates passed");
}
