// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone test: build separately without changing the shared CMake files.
#include "core/AdsbCpr.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <chrono>
#include <iostream>
using namespace dsdr::core;
using Frame = dsdr::dsp::AdsbDecoder::Frame;
using namespace std::chrono_literals;
void fixCrc(Frame &f) {
    f[11]=f[12]=f[13]=0; unsigned crc=0;
    for (int i=0; i<11; ++i) for (int bit=7; bit>=0; --bit) {
        auto byte=f[i];
        bool top=((crc>>23)^(byte>>bit))&1; crc=(crc<<1)&0xffffff; if (top) crc^=0xfff409;
    }
    f[11]=crc>>16; f[12]=crc>>8; f[13]=crc;
}
int main() {
    const Frame even{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
    const Frame odd {0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
    auto t = AdsbCpr::Clock::time_point{} + 100s;
    AdsbCpr c;
    assert(!c.update(odd,t));
    auto p = c.update(even,t+2s);
    assert(p && std::abs(p->latitude-52.257202)<0.00001 && std::abs(p->longitude-3.919373)<0.00001);
    c.clear(); assert(!c.update(even,t)); p=c.update(odd,t+2s);
    assert(p && std::abs(p->latitude-52.265780)<0.00001 && std::abs(p->longitude-3.938913)<0.00001);
    c.clear(); assert(!c.update(even,t)); assert(!c.update(odd,t+11s));
    c.clear(); assert(!c.update(even,t)); assert(c.update(odd,t+10s));
    c.clear(); auto bad=odd; bad[8]^=1; assert(!c.update(even,t)); assert(!c.update(bad,t+1s));
    c.clear(); assert(!c.update(even,t)); assert(!c.update(odd,t-1s));
    c.clear(); assert(!c.update(even,t)); auto other=odd; other[1]^=1; fixCrc(other); assert(dsdr::dsp::AdsbDecoder::validFrame(other)); assert(!c.update(other,t+1s));
    c.clear(); auto surface=odd; surface[4]=(5<<3)|(surface[4]&7); fixCrc(surface); assert(!c.update(even,t)); assert(!c.update(surface,t+1s));
    c.clear(); auto mismatch=odd; mismatch[6]=(mismatch[6]&0xfc); mismatch[7]=0; mismatch[8]&=1; fixCrc(mismatch); assert(!c.update(even,t)); assert(!c.update(mismatch,t+1s));
    std::cout << "CPR A/B, ordering, expiry and invalid frames passed\n";
}
