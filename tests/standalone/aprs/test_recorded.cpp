// SPDX-License-Identifier: GPL-3.0-or-later
// Optional external APRS recording: opens the file without playing audio.
#include "dsp/AprsAfskDecoder.h"
#include <sndfile.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif
int main(int argc,char**argv){
 if(argc!=2)return 2;SF_INFO info{};auto*f=sf_open(argv[1],SFM_READ,&info);
 if(!f||info.channels!=1||info.samplerate!=44100||info.frames<=0)return 2;
 std::vector<float>x(info.frames);auto n=sf_readf_float(f,x.data(),info.frames);sf_close(f);if(n!=info.frames)return 2;
 // AX.25 UI frame, excluding FCS. Source/destination/payload checked against
 // the independent aprs-rx output for the hash-documented fixture.
 const std::vector<unsigned char> expected={0x82,0xa0,0xa4,0xa6,0x40,0x40,0xe0,0xa6,0x6a,0x6e,0x98,0x9c,0x40,0x61,0x03,0xf0,0x3d,0x34,0x36,0x30,0x33,0x2e,0x36,0x33,0x4e,0x2f,0x30,0x31,0x34,0x33,0x31,0x2e,0x32,0x36,0x45,0x2d,0x4f,0x70,0x2e,0x20,0x41,0x6e,0x64,0x72,0x65,0x6a};
 for(size_t chunk:{size_t(137),size_t(4096)}){
  std::vector<dsdr::dsp::AprsAfskDecoder::Frame> out;
  dsdr::dsp::AprsAfskDecoder d([](void*p,const auto& frame){static_cast<std::vector<dsdr::dsp::AprsAfskDecoder::Frame>*>(p)->push_back(frame);},&out);
  assert(d.configure(info.samplerate));for(size_t i=0;i<x.size();i+=chunk)d.process(x.data()+i,std::min(chunk,x.size()-i));
  assert(out.size()==1 && out[0]==expected);d.reset();out.clear();std::vector<float> silence(info.samplerate*2,0);d.process(silence.data(),silence.size());assert(out.empty());
  printf("chunk=%zu frame matches reference; silence rejected\n",chunk);
 }
}
