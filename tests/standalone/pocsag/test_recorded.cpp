// SPDX-License-Identifier: GPL-3.0-or-later
// Optional external discriminator-audio recording; no audio is played.
#include "dsp/PocsagAudioDecoder.h"
#include <sndfile.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif
#include <cassert>
using Decoder=dsdr::dsp::PocsagAudioDecoder;
using Message=dsdr::dsp::PocsagDecoder::Message;
static std::vector<Message> decode(const std::vector<float>& data,double sampleRate,unsigned baud,std::size_t chunk) {
 std::vector<Message> out;Decoder d([](void*p,const Message&m){static_cast<std::vector<Message>*>(p)->push_back(m);},&out);
 assert(d.configure(sampleRate,baud));
 for(size_t i=0;i<data.size();i+=chunk)d.process(data.data()+i,std::min(chunk,data.size()-i));
 std::vector<float> silence(static_cast<size_t>(sampleRate),0);d.process(silence.data(),silence.size());return out;
}
int main(int argc,char**argv){
 if(argc!=2)return 2;SF_INFO info{};auto*f=sf_open(argv[1],SFM_READ,&info);
 if(!f || info.channels!=1 || info.samplerate!=48000 || info.frames<=0)return 2;
 std::vector<float> x(static_cast<size_t>(info.frames));auto n=sf_readf_float(f,x.data(),info.frames);sf_close(f);if(n!=info.frames)return 2;
 const std::string expected="This is a test of the pager. It should span a couple frames.";
 for(auto chunk:{size_t(137),size_t(4096)}) {
  auto correct=decode(x,48000,1200,chunk);
  assert(correct.size()==1 && correct[0].address==1100004 && correct[0].alpha.find(expected)!=std::string::npos);
  assert(decode(x,48000,512,chunk).empty());assert(decode(x,48000,2400,chunk).empty());
  assert(decode(std::vector<float>(96000,0),48000,1200,chunk).empty());
  std::printf("chunk=%zu reference message matched; wrong baud and silence rejected\n",chunk);
 }
}
