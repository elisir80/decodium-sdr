// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/dsc/DscRx.h"
#include "dect2/marine/marine_dsc.h"
#include <cstdio>
#include <string>
#include <vector>
#include <random>
using namespace dect2::marine;
bool phasingRecovery(const std::vector<int>& body) {
    auto bits=dscFrameBits(body,127,200);
    auto decode=[](const auto& input) {
        dsc_rx::Decoder local; DscDecoder ref(false,false); int ours=0,theirs=0;
        ref.setCallback([&](const auto& c){if(c.eccOk) ++theirs;});
        for(auto b:input) { if(local.push(b)) ++ours; ref.pushBit(b?1.f:-1.f); }
        return std::pair<int,int>{ours,theirs};
    };
    for(int i=0;i<4;++i) bits[200+10*i]^=1;
    if(decode(bits)!=std::pair<int,int>{1,1}) return false;
    auto inverted=bits; for(auto& bit:inverted) bit^=1;
    dsc_rx::Decoder inverseDecoder; int inverseCalls=0;
    for(auto bit:inverted) if(inverseDecoder.push(bit)) ++inverseCalls;
    if(inverseCalls!=1) return false;
    auto corruptPayload=bits;
    const size_t payloadDx=200+10*(12+2*3), payloadRx=200+10*(17+2*3);
    const auto replacement=dscSymbolBits(98);
    for(int i=0;i<10;++i) corruptPayload[payloadDx+i]=corruptPayload[payloadRx+i]=(replacement>>(9-i))&1;
    if(decode(corruptPayload).first!=0) return false;
    bits[240]^=1;
    if(decode(bits).first!=0) return false;
    bits=dscFrameBits(body,127,200);
    const auto encoded=dscSymbolBits(124);
    for(int i=0;i<10;++i) bits[200+i]=(encoded>>(9-i))&1;
    if(decode(bits).first!=0) return false;
    std::mt19937 rng(781); std::vector<uint8_t> noise(300000);
    for(auto& b:noise) b=rng()&1;
    return decode(noise).first==0;
}
bool check(const std::vector<int>& body,int eos,const char* from) {
    const auto bits=dscFrameBits(body,eos,200);
    dsc_rx::Decoder local; DscDecoder ref(false,false);
    std::vector<dsc_rx::Distress> ours; std::vector<dect2::DscCall> theirs;
    ref.setCallback([&](const auto& c){if(c.eccOk) theirs.push_back(c);});
    for(auto bit:bits) { if(auto v=local.push(bit)) ours.push_back(*v); ref.pushBit(bit?1.f:-1.f); }
    if(ours.size()!=1 || theirs.size()!=1 || ours[0].bodyLength!=int(body.size())) return false;
    for(size_t i=0;i<body.size();++i) if(ours[0].fullBody[i]!=body[i]) return false;
    return ours[0].eos==eos && theirs[0].format==body[0] && theirs[0].fromMmsi==from;
}
bool rejectsCorrupt(const std::vector<int>& body) {
    auto slots=dscFrameSymbols(body,127);
    const int index=3; const int replacement=(slots[12+2*index]+1)%100;
    slots[12+2*index]=replacement; slots[17+2*index]=replacement;
    std::vector<unsigned char> bits;
    for(int i=0;i<200;++i) bits.push_back(i%2==0);
    for(int s:slots) { const auto encoded=dscSymbolBits(s); for(int i=9;i>=0;--i) bits.push_back((encoded>>i)&1); }
    dsc_rx::Decoder local; DscDecoder ref(false,false); int ours=0,theirs=0;
    ref.setCallback([&](const auto& c){if(c.eccOk) ++theirs;});
    for(auto bit:bits) { if(local.push(bit)) ++ours; ref.pushBit(bit?1.f:-1.f); }
    return ours==0 && theirs==0;
}
bool interruptedThenRecovers(const std::vector<int>& body) {
    const auto bits=dscFrameBits(body,127,200);
    dsc_rx::Decoder local; DscDecoder ref(false,false); int ours=0,theirs=0;
    ref.setCallback([&](const auto& c){if(c.eccOk) ++theirs;});
    for(std::size_t i=0;i<bits.size()/2;++i) {
        if(local.push(bits[i])) ++ours;
        ref.pushBit(bits[i]?1.f:-1.f);
    }
    if(ours || theirs) return false;
    local.reset(); ref.reset();
    for(auto bit:bits) { if(local.push(bit)) ++ours; ref.pushBit(bit?1.f:-1.f); }
    return ours==1 && theirs==1;
}
int main() {
    const auto all=dscBuildAllShips("232123456",108,100,126,82910,82910);
    const auto ind=dscBuildIndividual("123456789",100,"232123456",100,126,82910,82910);
    auto group=ind; group[0]=114;
    auto automatic=ind; automatic[0]=123;
    auto area=ind; area[0]=102; area[5]=91; // area is not an MMSI and need not end in zero
    const auto ack=dscBuildDistressAck("232123456","123456789",103,35.0,14.0,12,34,109);
    auto relay=ack; relay[7]=112;
    auto addressedAck=ack; addressedAck[0]=120;
    addressedAck.insert(addressedAck.begin()+1,ind.begin()+1,ind.begin()+6);
    auto addressedRelay=addressedAck; addressedRelay[12]=112;
    auto areaRelay=addressedRelay; areaRelay[0]=102; areaRelay[5]=91;
    bool ok=true;
    ok=phasingRecovery(ind) && ok;
    for(int eos:{117,122,127}) ok=check(all,eos,"232123456") && check(ind,eos,"232123456") &&
        check(group,eos,"232123456") && check(automatic,eos,"232123456") && check(area,eos,"232123456") && ok;
    ok=check(ack,122,"232123456") && check(relay,127,"232123456") && ok;
    ok=check(addressedAck,122,"232123456") && check(addressedRelay,127,"232123456") &&
       check(areaRelay,127,"232123456") && ok;
    ok=rejectsCorrupt(all) && rejectsCorrupt(ind) && rejectsCorrupt(group) && rejectsCorrupt(area) && rejectsCorrupt(ack) && rejectsCorrupt(relay) && ok;
    ok=rejectsCorrupt(addressedAck) && rejectsCorrupt(addressedRelay) && rejectsCorrupt(areaRelay) && ok;
    ok=interruptedThenRecovers(addressedAck) && interruptedThenRecovers(addressedRelay) &&
       interruptedThenRecovers(areaRelay) && ok;
    std::printf("all-ships, individual, group, area, distress acknowledgement and relay: %s\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
