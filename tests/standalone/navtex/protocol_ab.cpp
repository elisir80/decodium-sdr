// SPDX-License-Identifier: GPL-3.0-or-later
// Same synthetic bitstream to OnAir and the adapted protocol module.
#include "dsp/navtex/NavtexProtocol.h"
#include "dect2/marine/marine_navtex.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>
int main() {
    const std::string text="ZCZC EA01\nTEST MESSAGE 123\nNNNN\n";
    std::vector<int> tokens(40,-1);
    for(auto c:dect2::marine::navtexEncode(text)) tokens.push_back(c);
    tokens.insert(tokens.end(),40,-1);
    const auto bits=dect2::marine::codesToBits(dect2::marine::sitorBSlots(tokens));
    for(bool invert:{false,true}) for(bool damaged:{false,true}) {
        dsdr::navtex::NavtexDecoder a(invert); dect2::marine::NavtexDecoder b(invert);
        std::vector<dsdr::navtex::NavtexMessage> outA; std::vector<dect2::NavtexMessage> outB;
        a.setCallback([&](const auto& m){outA.push_back(m);}); b.setCallback([&](const auto& m){outB.push_back(m);});
        for(size_t i=0;i<bits.size();++i) {
            bool value=bool(bits[i]); if(damaged && i==bits.size()/2) value=!value;
            if(invert) value=!value;
            a.pushBit(value?1.f:-1.f); b.pushBit(value?1.f:-1.f);
        }
        a.flush(); b.flush();
        bool same=outA.size()==outB.size() && !outA.empty();
        for(size_t i=0;same && i<outA.size();++i)
            same=outA[i].text==outB[i].text && outA[i].header==outB[i].header && outA[i].complete==outB[i].complete;
        std::printf("inverted=%d damaged=%d messages=%zu match=%d\n",invert,damaged,outA.size(),same);
        if(!same) return 1;
    }
    // Two independent broadcasts with a fresh phasing sequence. Vary the
    // first message's length to exercise both DX/RX parities after NNNN.
    for(bool invert:{false,true}) for(int extra=0;extra<2;++extra) {
        std::vector<int> stream(40,-1);
        const std::string first="ZCZC EA01\nFIRST"+std::string(extra,'X')+"\nNNNN\n";
        for(auto c:dect2::marine::navtexEncode(first)) stream.push_back(c);
        stream.insert(stream.end(),40,-1);
        auto sequence=dect2::marine::codesToBits(dect2::marine::sitorBSlots(stream));
        std::vector<int> second(40,-1);
        for(auto c:dect2::marine::navtexEncode("ZCZC EA02\nSECOND\nNNNN\n")) second.push_back(c);
        second.insert(second.end(),40,-1);
        auto next=dect2::marine::codesToBits(dect2::marine::sitorBSlots(second));
        sequence.insert(sequence.end(),next.begin(),next.end());
        dsdr::navtex::NavtexDecoder rx(invert); std::vector<dsdr::navtex::NavtexMessage> messages;
        rx.setCallback([&](auto const&m){if(m.complete)messages.push_back(m);});
        for(auto bit:sequence) rx.pushBit((bool(bit)!=invert)?1.f:-1.f);
        rx.flush();
        if(messages.size()!=2 || messages[0].header!="EA01" || messages[1].header!="EA02" ||
           messages[1].text.find("SECOND")==std::string::npos) return 1;
        std::printf("successive messages inverted=%d extra=%d recovered=2\n",invert,extra);
    }
    return 0;
}
