// SPDX-License-Identifier: GPL-3.0-or-later
// The same executable source is linked once to each implementation.
#include "dsp/vdl2/OnAirVdl2Abi.h"
#include <array>
#include <cstdint>
#include <cstdio>
int main() {
    uint64_t hash=1469598103934665603ull;
    auto add=[&](uint64_t n){for(int k=0;k<8;++k) {hash^=(n>>(k*8))&255;hash*=1099511628211ull;}};
    if(onair_vdl2_version()!=2) return 1;
    size_t sizes[3]{}; onair_vdl2_struct_sizes(sizes);
    if(sizes[0]!=sizeof(OnairVdl2Parsed) || sizes[1]!=sizeof(OnairVdl2Layout) || sizes[2]!=sizeof(OnairVdl2Addr)) return 2;
    for(uint32_t len=1;len<=8191;len+=7) {
        const auto word=onair_vdl2_header_word(len); uint32_t decoded=0; bool corrected=false;
        if(!onair_vdl2_header_decode(word,&decoded,&corrected) || decoded!=len || corrected) return 3;
        add(word);
        for(unsigned bit=0;bit<22;++bit) {
            decoded=0; corrected=false;
            const bool ok=onair_vdl2_header_decode(word^(1u<<bit),&decoded,&corrected);
            if(!ok || decoded!=len || !corrected) return 4;
            add(decoded); add(corrected);
        }
        OnairVdl2Layout layout{}; const bool ok=onair_vdl2_layout(len,&layout);
        add(ok); add(uint32_t(layout.data_oct)); add(uint32_t(layout.fec_oct)); add(uint32_t(layout.blocks));
    }
    for(unsigned seed=0;seed<32;++seed) {
        std::array<uint8_t,255> data{};
        for(unsigned i=0;i<249;++i) data[i]=uint8_t(i*37+seed*19);
        onair_vdl2_rs_encode(data.data(),data.data()+249);
        for(unsigned errors=0;errors<=4;++errors) {
            auto damaged=data; for(unsigned i=0;i<errors;++i) damaged[i*47]^=uint8_t(i+1);
            const int fixed=onair_vdl2_rs_decode(damaged.data(),6); add(uint32_t(fixed));
            if(errors<=3 && (fixed!=int(errors) || damaged!=data)) return 5;
            if(errors==4 && fixed>=0) return 6;
            for(auto byte:damaged) add(byte);
        }
        add(onair_vdl2_fcs(data.data(),249));
    }
    std::printf("ABI v2 header/FEC/FCS digest %016llx\n",static_cast<unsigned long long>(hash));
    return 0;
}
