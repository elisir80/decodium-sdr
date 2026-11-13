// Experimental receive-only DSC bitstream nucleus. GPL-3.0-or-later.
#include "DscRx.h"
#include <algorithm>

namespace dsc_rx {
namespace {
int symbol(const std::vector<uint8_t>& b, std::size_t start, int slot, bool invert) {
    int value = 0, ones = 0, check = 0;
    for (int i = 0; i < 7; ++i) {
        const int v = b[start + slot * 10 + i] ^ invert;
        value |= v << i;
        ones += v;
    }
    for (int i = 7; i < 10; ++i) check = (check << 1) | (b[start + slot * 10 + i] ^ invert);
    return check == 7 - ones ? value : -1;
}
std::optional<Distress> decode(const std::vector<uint8_t>& b, std::size_t start, int length, bool inv) {
    // Recover a bounded number of erased phasing symbols, while rejecting
    // valid symbols that contradict the sequence. Copies and ECC are still
    // checked below before a call is accepted.
    int phaseErasures = 0;
    for (int i = 0; i < 12; ++i) {
        const int expected = i % 2 ? 111 - i / 2 : 125;
        const int actual = symbol(b, start, i, inv);
        if (actual != expected && (actual >= 0 || ++phaseErasures > 4)) return {};
    }
    int values[30]{}; int repaired = 0;
    for (int i = 0; i < length+3; ++i) {
        const int dx = symbol(b, start, 12 + 2*i, inv);
        const int rx = symbol(b, start, 17 + 2*i, inv);
        if (dx < 0 && rx < 0) return {};
        if (dx >= 0 && rx >= 0 && dx != rx) return {};
        if (dx < 0 || rx < 0) ++repaired;
        values[i] = dx >= 0 ? dx : rx;
    }
    const int format=values[0], eos=values[length+1];
    if(values[1]!=format || (length==15 && format!=112 && format!=116) ||
       (length==20 && format!=120 && format!=123 && format!=114 && format!=102) || (length==22 && format!=116) ||
       (length==27 && format!=120 && format!=123 && format!=114 && format!=102) ||
       (eos!=117 && eos!=122 && eos!=127) ||
       (format==112 && eos!=127)) return {};
    int parity=eos ^ format;
    for(int i=2;i<=length;++i) parity^=values[i];
    if(parity!=values[length+2]) return {};
    Distress out; out.bodyLength=length; out.fullBody[0]=format;
    for(int i=1;i<length;++i) out.fullBody[i]=values[i+1];
    std::copy_n(out.fullBody.begin(),15,out.body.begin());
    out.sourceOffset=format==112?1:format==116?2:7;
    if(format!=112) {
        const int category=out.fullBody[format==116?1:6];
        if(length==22 || length==27) {
            const int command=length==22 ? 7 : 12;
            if(category!=112 || (out.fullBody[command]!=110 && out.fullBody[command]!=112)) return {};
            for(int i=command+1;i<=command+5;++i) if(out.fullBody[i]>99) return {};
            if(out.fullBody[command+5]%10!=0) return {};
        } else if(category!=100 && category!=106 && category!=108 && category!=110) return {};
    }
    for(int i=0;i<5;++i) if(out.fullBody[out.sourceOffset+i]>99) return {};
    if(out.fullBody[out.sourceOffset+4]%10!=0) return {};
    if(format==120 || format==123 || format==114 || format==102) {
        for(int i=1;i<=5;++i) if(out.fullBody[i]>99) return {};
        if(format!=102 && out.fullBody[5]%10!=0) return {};
    }
    out.eos=eos; out.repaired=repaired; out.inverted=inv;
    return out;
}
}
void Decoder::reset() { bits_.clear(); }
std::optional<Distress> Decoder::push(bool bit) {
    bits_.push_back(bit);
    for(int length:{15,20,22,27}) {
        const std::size_t n=std::size_t(2*length+22)*10;
        if(bits_.size()<n) continue;
        const auto start=bits_.size()-n;
        auto result=decode(bits_,start,length,false);
        if(!result) result=decode(bits_,start,length,true);
        if(result) { bits_.clear(); return result; }
    }
    if(bits_.size()>760) bits_.erase(bits_.begin());
    return {};
}
}
