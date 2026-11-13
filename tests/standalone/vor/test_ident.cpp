// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/VorIdentDecoder.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using dsdr::dsp::VorIdentDecoder;
static std::vector<float> sample(bool ident,double dot,double noise) {
    constexpr double pi=3.14159265358979323846,rate=48000;
    const std::string morse[]={"-..","-..-","-..."};
    std::vector<std::pair<double,double>> marks; double t=.5;
    for(const auto &letter:morse) {
        for(char c:letter) { const double len=(c=='-'?3:1)*dot; marks.push_back({t,t+len}); t+=len+dot; }
        t+=2*dot;
    }
    std::vector<float> data; unsigned rng=0x651317u; std::size_t mark=0;
    for(int n=0;n<int((t+1)*rate);++n) {
        const double time=n/rate; while(mark<marks.size() && time>=marks[mark].second) ++mark;
        const bool key=ident && mark<marks.size() && time>=marks[mark].first;
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        const double r=(double(rng&65535)/32767.5-1)*noise;
        data.push_back(float(1+.3*std::cos(2*pi*30*time)+.3*std::cos(2*pi*9960*time+16*std::sin(2*pi*30*time))
                             +(key?.10*std::cos(2*pi*1020*time):0)+r));
    }
    return data;
}
int main() {
    for(double dot:{.1,1.2/7.}) for(double noise:{0.,.03}) {
        auto iq=sample(true,dot,noise); VorIdentDecoder a,b;
        a.process(iq.data(),iq.size());
        for(std::size_t i=0;i<iq.size();i+=137) b.process(iq.data()+i,std::min(std::size_t(137),iq.size()-i));
        std::printf("dot=%.3f noise=%.2f full=%s chunks=%s\n",dot,noise,a.identifier().c_str(),b.identifier().c_str());
        if(a.identifier()!="DXB" || b.identifier()!=a.identifier()) return 1;
        std::vector<float> noKey(48000*25,1.f);
        b.process(noKey.data(),noKey.size());
        if(b.identifier()!="DXB") return 4;
        noKey.resize(48000*6,1.f); b.process(noKey.data(),noKey.size());
        if(!b.identifier().empty()) return 3;
        b.reset(); auto noId=sample(false,dot,noise); b.process(noId.data(),noId.size());
        if(!b.identifier().empty()) return 2;
    }
    return 0;
}
