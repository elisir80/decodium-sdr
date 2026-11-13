// SPDX-License-Identifier: GPL-3.0-or-later
// Optional OnAir comparison on demodulated audio; output cannot open a device.
#include "dect2/vor/vor_rx.h"
#include "dect2/audio/audioout.h"
#include <cstdio>
#include <vector>
namespace dect2 {
struct AudioOut::Impl {};
AudioOut::AudioOut()=default;
AudioOut::~AudioOut()=default;
bool AudioOut::start(int,int) { return false; }
void AudioOut::setStartThreshold(int) {}
void AudioOut::setVolume(float) {}
void AudioOut::setMuted(bool) {}
int AudioOut::write(const float*,int) { return 0; }
void AudioOut::flush() {}
int AudioOut::bufferedFrames() const { return 0; }
bool ilsIsLocalizer(double) { return false; } // VOR-only probe
}
int main(int argc,char**argv) {
    if(argc!=2) return 2;
    FILE* file=std::fopen(argv[1],"rb"); if(!file) return 2;
    dect2::VorReceiver receiver; receiver.setSilent(true); receiver.setMuted(true);
    receiver.configure(48000); receiver.setFunction(1);
    float input[1024]; size_t count; std::vector<dect2::cf32> iq;
    while((count=std::fread(input,sizeof(float),1024,file))) {
        iq.resize(count);
        for(size_t i=0;i<count;++i) iq[i]={.5f+input[i]*.4f,0};
        receiver.feed(iq.data(),count);
    }
    const bool ok=!std::ferror(file); std::fclose(file);
    dect2::VorTelemetry result{}; receiver.telemetry(result,0);
    std::printf("valid=%d radial=%.2f raw=%.2f state=%d\n",result.vorValid,result.radialDeg,result.radialRawDeg,result.state);
    return ok ? 0 : 2;
}
