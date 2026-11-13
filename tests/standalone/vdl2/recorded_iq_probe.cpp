// SPDX-License-Identifier: GPL-3.0-or-later
// External 50 ksample/s little-endian IQ, supplied separately; never prints payloads.
#include "dsp/vdl2/Vdl2WindowStream.h"
#include "dsp/vdl2/Vdl2Frame.h"
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>
using Sample=std::complex<float>;
using Frame=std::vector<std::uint8_t>;
std::vector<Frame> decode(const std::vector<Sample>& iq,std::size_t chunk) {
    std::vector<Frame> frames;
    dsdr::vdl2::WindowStream stream(50000,0.000001,[&](const auto& burst) {
        for(const auto& raw:burst.burst.frames)
            if(dsdr::vdl2::parse(raw.data(),raw.size())) frames.push_back(raw);
    });
    for(std::size_t i=0;i<iq.size();i+=chunk) stream.feed(iq.data()+i,std::min(chunk,iq.size()-i));
    return frames;
}
int main(int argc,char**argv) {
    if(argc!=2 && argc!=3) return 2;
    std::ifstream file(argv[1],std::ios::binary); if(!file) return 2;
    const std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(file),{}};
    if(raw.size()!=1200000) return 2;
    std::vector<Sample> iq; iq.reserve(raw.size()/4);
    auto signed16=[](unsigned lo,unsigned hi) { const unsigned n=lo|(hi<<8); return n<32768?int(n):int(n)-65536; };
    for(std::size_t i=0;i<raw.size();i+=4)
        iq.emplace_back(signed16(raw[i],raw[i+1])/32768.f,signed16(raw[i+2],raw[i+3])/32768.f);
    const auto a=decode(iq,1024),b=decode(iq,4096);
    if(argc==3) {
        FILE* out=std::fopen(argv[2],"wx"); if(!out) return 2;
        for(const auto& frame:a) {
            const unsigned length=unsigned(frame.size());
            const unsigned char header[4]={static_cast<unsigned char>(length),static_cast<unsigned char>(length>>8),static_cast<unsigned char>(length>>16),static_cast<unsigned char>(length>>24)};
            if(std::fwrite(header,1,4,out)!=4 || std::fwrite(frame.data(),1,frame.size(),out)!=frame.size()) { std::fclose(out); return 2; }
        }
        if(std::fclose(out)!=0) return 2;
    }
    std::vector<Sample> silence(iq.size()); const auto negative=decode(silence,1024);
    std::mt19937 rng(91823); std::normal_distribution<float> noise(0.f,0.05f);
    std::vector<Sample> randomNoise(iq.size());
    for(auto& s:randomNoise) s={noise(rng),noise(rng)};
    const auto randomResult=decode(randomNoise,1024);
    for(auto& sample:iq) sample=std::conj(sample);
    const auto reversed=decode(iq,1024);
    std::printf("valid A=%zu B=%zu silence=%zu noise=%zu inverted=%zu\n",a.size(),b.size(),negative.size(),randomResult.size(),reversed.size());
    return a.size()==4 && a==b && negative.empty() && randomResult.empty() && reversed.empty() ? 0 : 1;
}
