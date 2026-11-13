// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/navtex/NavtexAudioDecoder.h"
#include "dect2/marine/marine_navtex.h"
#include "dect2/marine/marine_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
int main() {
    constexpr double pi=3.14159265358979323846;
    std::vector<int> tokens(40,-1);
    for(auto c:dect2::marine::navtexEncode("ZCZC EA01\nTEST MESSAGE 123\nNNNN\n")) tokens.push_back(c);
    tokens.insert(tokens.end(),40,-1);
    auto bits=dect2::marine::codesToBits(dect2::marine::sitorBSlots(tokens));
    for(int offset:{-40,-20,0,20,40}) for(bool inverted:{false,true}) {
        std::vector<float> audio(937,0); double phase=.73;
        for(size_t i=0;i<bits.size()*240;++i) {
            const bool bit=bool(bits[i/240])!=inverted;
            phase+=2*pi*(1000+offset+(bit?85:-85))/24000;
            audio.push_back(float(.65*std::sin(phase)));
        }
        audio.insert(audio.end(),2400,0);
        std::vector<dsdr::navtex::NavtexMessage> a;
        std::vector<dect2::NavtexMessage> b;
        dsdr::navtex::NavtexAudioDecoder ours([&](const auto& m){a.push_back(m);});
        dect2::marine::FskSlicer slicer; slicer.setCentreHz(1000);
        dect2::marine::NavtexDecoder r0(false),r1(true);
        auto collect=[&](const auto& m){if(m.complete) b.push_back(m);}; r0.setCallback(collect); r1.setCallback(collect);
        for(size_t i=0;i<audio.size();i+=137) {
            const auto n=std::min(size_t(137),audio.size()-i);
            ours.process(audio.data()+i,n);
            std::vector<dect2::cf32> block(n);
            for(size_t j=0;j<n;++j) block[j]={audio[i+j],0};
            slicer.process(block.data(),block.size(),[&](float v){r0.pushBit(v);r1.pushBit(v);});
        }
        bool ok=a.size()==1 && b.size()==1 && a[0].header==b[0].header && a[0].text==b[0].text;
        std::printf("audio offset=%d inverted=%d ours=%zu OnAir=%zu match=%d\n",offset,inverted,a.size(),b.size(),ok);
        if(!ok) return 1;
        a.clear(); ours.reset();
        std::vector<float> quiet(24000*3,0.f); ours.process(quiet.data(),quiet.size());
        if(!a.empty()) return 1;
        ours.process(audio.data(),audio.size()/2); ours.reset();
        ours.process(audio.data()+audio.size()/2,audio.size()-audio.size()/2);
        if(!a.empty()) return 1;
        ours.reset(); ours.process(audio.data(),audio.size());
        if(a.size()!=b.size() || (!a.empty() && a[0].header!=b[0].header)) return 1;
    }
    struct Case { float noise; float interferer; bool signal; };
    for(const Case c: {Case{0.15f,0.f,true},Case{0.35f,0.f,true},Case{0.60f,0.f,true},
                       Case{0.15f,0.30f,true},Case{0.35f,0.30f,true},Case{0.35f,0.30f,false}})
    for(const std::uint32_t seed : {0x5a17c3u, 0x129d5bu, 0x84e3a1u, 0xc37f21u}) {
        std::vector<float> audio(937,0); double phase=.73; std::uint32_t random=seed;
        for(size_t i=0;i<bits.size()*240;++i) {
            const bool bit=bool(bits[i/240]); phase+=2*pi*(1000+(bit?85:-85))/24000;
            random ^= random<<13; random ^= random>>17; random ^= random<<5;
            const float noise=(float(random & 0xffffu)/32767.5f-1.f)*c.noise;
            audio.push_back((c.signal ? float(.65*std::sin(phase)) : 0.f) + noise
                            + c.interferer*float(std::sin(2*pi*1500*i/24000)));
        }
        audio.insert(audio.end(),2400,0);
        std::vector<dsdr::navtex::NavtexMessage> a; std::vector<dect2::NavtexMessage> b;
        dsdr::navtex::NavtexAudioDecoder ours([&](const auto& m){a.push_back(m);});
        dect2::marine::FskSlicer slicer; slicer.setCentreHz(1000);
        dect2::marine::NavtexDecoder r0(false),r1(true);
        auto collect=[&](const auto& m){if(m.complete) b.push_back(m);}; r0.setCallback(collect); r1.setCallback(collect);
        for(size_t i=0;i<audio.size();i+=137) {
            const auto n=std::min(size_t(137),audio.size()-i); ours.process(audio.data()+i,n);
            std::vector<dect2::cf32> block(n);
            for(size_t j=0;j<n;++j) block[j]={audio[i+j],0};
            slicer.process(block.data(),block.size(),[&](float v){r0.pushBit(v);r1.pushBit(v);});
        }
        const bool same=a.size()==b.size() && (a.empty() || (a.size()==1 && a[0].header==b[0].header && a[0].text==b[0].text));
        std::printf("seed=%08x noise=%.2f interferer=%.2f signal=%d ours=%zu OnAir=%zu match=%d\n",seed,c.noise,c.interferer,c.signal,a.size(),b.size(),same);
        if(!same || (c.signal && (a.size()!=1 || a[0].header!="EA01" || a[0].text.find("TEST MESSAGE 123")==std::string::npos))
           || (!c.signal && !a.empty())) return 1;
    }
    return 0;
}
