// SPDX-License-Identifier: GPL-3.0-or-later
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "dect2/adsb/adsb_demod.h"
#include "dsp/AdsbDecoder.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>
using Frame = dsdr::dsp::AdsbDecoder::Frame;
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 2;
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), {});
    if (raw.size() != 713736) return 2;
    std::vector<dsdr::dsp::Complex> iq(raw.size()/2 + 2000);
    for (size_t i=0; i<raw.size()/2; ++i)
        iq[i] = {((int)raw[2*i]-127.5f)/127.5f, ((int)raw[2*i+1]-127.5f)/127.5f};
    std::vector<Frame> a,b;
    dsdr::dsp::AdsbDecoder d([](void *p,const Frame &f){static_cast<std::vector<Frame>*>(p)->push_back(f);},&a);
    dect2::AdsbDemod o; o.configure(2000000);
    o.setSink([&](dect2::AdsbRaw &raw,bool) { Frame f{}; if(raw.bits!=112) return false; std::copy(raw.bytes,raw.bytes+14,f.begin()); if (!dsdr::dsp::AdsbDecoder::validFrame(f)) return false; b.push_back(f); return true; });
    for (size_t i=0;i<iq.size();i+=137) { auto n=std::min(size_t(137),iq.size()-i); d.process(iq.data()+i,n); o.feed(iq.data()+i,n); }
    if(argc==3) {
        // The optional artifact contains potentially identifying frames: keep
        // it local, never print the contents, and do not overwrite files.
        std::FILE* out=std::fopen(argv[2],"wx"); if(!out) return 2;
        for(const auto& f:a) if(std::fwrite(f.data(),1,f.size(),out)!=f.size()) { std::fclose(out); return 2; }
        if(std::fclose(out)!=0) return 2;
    }
    const std::set<Frame> da(a.begin(),a.end()), ob(b.begin(),b.end());
    size_t shared=0; for(const auto &f:da) shared += ob.count(f);
    std::printf("real IQ long CRC-valid: Decodium=%zu (%zu unique), OnAir=%zu (%zu unique), shared=%zu\n", a.size(),da.size(),b.size(),ob.size(),shared);
    // Dataset-specific regression threshold, not a claim of equal performance.
    // The reference fixture is documented to contain one known aircraft.
    const auto target=[](const Frame &f){ return f[1]==0x4d && f[2]==0x20 && f[3]==0x23; };
    std::printf("different ICAO: Decodium=%zu OnAir=%zu\n", a.size()-size_t(std::count_if(a.begin(),a.end(),target)), b.size()-size_t(std::count_if(b.begin(),b.end(),target)));
    return a.size() >= 130 && b.size() >= 100 &&
           std::all_of(a.begin(),a.end(),target) && std::all_of(b.begin(),b.end(),target) ? 0 : 1;
}
