// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "dsp/AisIqFrontend.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <vector>
using Frontend=dsdr::dsp::AisIqFrontend;
struct Capture {
    std::vector<Frontend::Sample> data; unsigned gaps=0;
    static void append(void *p,const Frontend::Sample *samples,std::size_t count) noexcept {
        auto &self=*static_cast<Capture *>(p);
        if(!samples) { ++self.gaps; return; }
        self.data.insert(self.data.end(),samples,samples+count);
    }
};
int main() {
    constexpr double pi=3.14159265358979323846;
    for(unsigned rate:{192000u,1536000u,2048000u}) {
        double energies[2]{};
        for(int which=0;which<2;++which) {
            Capture c; Frontend f(Capture::append,&c); assert(f.configure(rate,25000));
            assert(f.outputRate()>=48000 && f.outputRate()<96000);
            std::vector<Frontend::Sample> samples(rate/25); const double hz=which==0 ? 25000.0 : -25000.0;
            for(std::size_t i=0;i<samples.size();++i) {
                const double phase=2*pi*hz*i/rate; samples[i]={float(std::cos(phase)),float(std::sin(phase))};
            }
            const auto started=std::chrono::steady_clock::now();
            for(std::size_t i=0;i<samples.size();) { auto n=std::min<std::size_t>(317,samples.size()-i); f.process(samples.data()+i,n); i+=n; }
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            if(which==0) std::printf("rate=%u processing %.1f ms of IQ in %.1f ms\n",rate,1000.0*samples.size()/rate,1000*elapsed);
            assert(c.data.size()>1200);
            for(std::size_t i=800;i<c.data.size();++i) energies[which]+=std::norm(c.data[i]);
            energies[which]/=(c.data.size()-800);
            auto nan=Frontend::Sample(std::numeric_limits<float>::quiet_NaN(),0);
            f.process(&nan,1); assert(c.gaps==1);
        }
        std::printf("rate=%u desired=%.6g adjacent=%.6g rejection=%.1f dB\n",rate,energies[0],energies[1],10*std::log10(energies[0]/energies[1]));
        assert(energies[0]>0.90 && energies[1]<energies[0]*0.0001);
    }
    Capture c; Frontend f(Capture::append,&c);
    assert(!f.configure(32000,0)); assert(!f.configure(2500000,0)); assert(!f.configure(192000,90000));
    assert(!f.configured());
}
