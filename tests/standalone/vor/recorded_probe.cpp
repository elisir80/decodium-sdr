// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic only: demodulated mono 48-kHz float audio; no audio device.
#include "dsp/VorReceiver.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
struct Stats { unsigned total=0,valid=0; double x=0,y=0; };
int main(int argc,char**argv) {
    if(argc!=2 && argc!=4) return 2;
    FILE* file=std::fopen(argv[1],"rb"); if(!file) return 2;
    Stats stats;
    dsdr::dsp::VorReceiver decoder([](const auto& r,void* ctx) {
        auto& s=*static_cast<Stats*>(ctx); ++s.total;
        if(r.valid) { constexpr double pi=3.14159265358979323846; ++s.valid; s.x+=std::cos(r.radialDegrees*pi/180); s.y+=std::sin(r.radialDegrees*pi/180); }
    },&stats);
    if(!decoder.configure(48000)) return 2;
    float block[2048]; size_t n;
    while((n=std::fread(block,sizeof(float),2048,file))) decoder.processAudio(block,n);
    const bool ok=!std::ferror(file); std::fclose(file);
    constexpr double pi=3.14159265358979323846;
    double angle=std::atan2(stats.y,stats.x)*180/pi; if(angle<0) angle+=360;
    std::printf("valid=%u windows=%u circular_mean=%.2f degrees\n",stats.valid,stats.total,angle);
    if(argc==4 && (!stats.valid || std::abs(std::remainder(angle-std::atof(argv[2]),360.0))>std::atof(argv[3]))) return 1;
    return ok && stats.total ? 0 : 1;
}
