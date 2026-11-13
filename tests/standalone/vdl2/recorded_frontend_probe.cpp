// SPDX-License-Identifier: GPL-3.0-or-later
// Optional recorded-IQ test through the product frontend and window decoder.
#include "dsp/vdl2/Vdl2Frontend.h"
#include "dsp/vdl2/Vdl2WindowStream.h"
#include "dsp/vdl2/Vdl2Frame.h"
#include <complex>
#include <cstdio>
#include <set>
#include <vector>
static bool independentFcs(const std::vector<std::uint8_t>& frame) {
    if(frame.size()<2) return false;
    unsigned crc=0xffff;
    for(std::size_t i=0;i+2<frame.size();++i) {
        crc^=frame[i];
        for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0x8408:0);
    }
    crc^=0xffff;
    return crc==(unsigned(frame[frame.size()-2])|(unsigned(frame.back())<<8));
}
int main(int argc,char**argv) {
    if(argc!=2 && argc!=3) return 2;
    int frames=0;
    std::set<std::vector<std::uint8_t>> unique;
    dsdr::vdl2::WindowStream stream(96000,1e-6,[&](const auto& burst) {
        for(const auto& frame:burst.burst.frames) if(dsdr::vdl2::parse(frame.data(),frame.size())) { ++frames; unique.insert(frame); }
    });
    dsdr::vdl2::Frontend frontend([](void* context,const auto* samples,std::size_t count) noexcept {
        if(samples) static_cast<dsdr::vdl2::WindowStream*>(context)->feed(samples,count);
    },&stream);
    if(!frontend.configure(192000,0)) return 2;
    FILE* file=std::fopen(argv[1],"rb"); if(!file) return 2;
    float input[2048]; std::size_t count;
    while((count=std::fread(input,sizeof(float),2048,file))) {
        if(count%2) {std::fclose(file);return 2;}
        std::complex<float> iq[1024];
        for(std::size_t i=0;i<count/2;++i) iq[i]={input[2*i],input[2*i+1]};
        frontend.process(iq,count/2);
    }
    const bool ok=!std::ferror(file); std::fclose(file);
    if(argc==3) {
        FILE* out=std::fopen(argv[2],"wx"); if(!out) return 2;
        for(const auto& frame:unique) {
            const unsigned n=unsigned(frame.size());
            const unsigned char header[4]={static_cast<unsigned char>(n),static_cast<unsigned char>(n>>8),static_cast<unsigned char>(n>>16),static_cast<unsigned char>(n>>24)};
            if(std::fwrite(header,1,4,out)!=4 || std::fwrite(frame.data(),1,n,out)!=n) {std::fclose(out);return 2;}
        }
        if(std::fclose(out)!=0) return 2;
    }
    std::printf("frontend valid frames=%d unique=%zu\n",frames,unique.size());
    if(!ok || frames!=5 || unique.size()!=5) return 1;
    for(auto frame:unique) {
        if(!independentFcs(frame)) return 1;
        frame[0]^=1;
        if(independentFcs(frame)) return 1;
    }
    return 0;
}
