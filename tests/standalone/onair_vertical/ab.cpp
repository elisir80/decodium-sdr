// SPDX-License-Identifier: GPL-3.0-or-later
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "dect2/adsb/adsb_demod.h"
#include "dsp/AdsbDecoder.h"
#include "core/AsyncIqDecoder.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
using Frame = dsdr::dsp::AdsbDecoder::Frame;
int main() {
    const Frame expected{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
    std::vector<dsdr::dsp::Complex> iq(24000, {0,0});
    for (int rep=0; rep<8; ++rep) {
        const int start=2000+rep*2500;
        for (int i : {0,2,7,9}) iq[start+i]={0.8f,0};
        for (int b=0;b<112;++b) iq[start+16+2*b+((expected[b/8]>>(7-b%8))&1 ? 0 : 1)]={0.8f,0};
    }
    std::vector<Frame> a,b;
    dsdr::dsp::AdsbDecoder d([](void *p,const Frame &f){static_cast<std::vector<Frame>*>(p)->push_back(f);},&a);
    dect2::AdsbDemod o; o.configure(2000000);
    o.setSink([&](dect2::AdsbRaw &raw,bool) { Frame f{}; if(raw.bits!=112) return false; std::copy(raw.bytes,raw.bytes+14,f.begin()); if (!dsdr::dsp::AdsbDecoder::validFrame(f)) return false; b.push_back(f); return true; });
    dsdr::core::AsyncIqDecoder bridge(256, 137, {&o,
        [](void *p) noexcept { static_cast<dect2::AdsbDemod*>(p)->reset(); },
        [](void *p, const dsdr::dsp::Complex *x, size_t n) noexcept { static_cast<dect2::AdsbDemod*>(p)->feed(x,n); }});
    bridge.start();
    for (size_t i=0;i<iq.size();i+=137) { auto n=std::min(size_t(137),iq.size()-i); d.process(iq.data()+i,n); if (bridge.submit(iq.data()+i,n)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 2; }
    bridge.stop(true);
    const auto stats=bridge.stats();
    std::printf("IQ blocks=%llu dropped=%llu\n",(unsigned long long)stats.accepted,(unsigned long long)stats.droppedBlocks);
    std::printf("Decodium=%zu OnAir=%zu OnAir_candidates=%llu preambles=%llu\n",a.size(),b.size(),(unsigned long long)o.candidates(),(unsigned long long)o.preambles());
    return a.size()==8 && b.size()==8 && a==b && std::all_of(a.begin(),a.end(),[&](const Frame &f){return f==expected;}) ? 0 : 1;
}
