// SPDX-License-Identifier: GPL-3.0-or-later
// Optional AIS discriminator-audio comparison with libaisdemod and libsndfile.
// The input is externally provided; this program does not play audio or print
// payloads. See docs/decoders/priority-ten.md for provenance and limitations.
#include "dsp/AisDecoder.h"
#include "aisdemod.h"
#include <sndfile.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <set>
#include <vector>
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif
using Frames=std::multiset<std::vector<unsigned char>>;
int main(int argc,char**argv){
 if(argc!=2)return 2;SF_INFO info{};auto*f=sf_open(argv[1],SFM_READ,&info);
 if(!f||info.channels!=1||info.samplerate!=48000||info.frames<=0)return 2;
 std::vector<float>x(info.frames);auto read=sf_readf_float(f,x.data(),info.frames);sf_close(f);if(read!=info.frames)return 2;
 Frames ours,reference;dsdr::dsp::AisDecoder decoder([](void*p,auto const&m){static_cast<Frames*>(p)->insert(m.payload);},&ours);
 assert(decoder.configure(96000));double offset=0;char label='A';auto*ref=ais_rx_create_ex(96000,&offset,&label,1);assert(ref);
 ais_rx_set_callback(ref,[](const ais_message_t*m,void*p){assert(m->payload_bits%8==0);static_cast<Frames*>(p)->insert(std::vector<unsigned char>(m->payload,m->payload+m->payload_bits/8));},&reference);
 std::vector<std::complex<float>> iq;std::vector<float> interleaved;iq.reserve(4096);interleaved.reserve(8192);double phase=0;
 auto process=[&](){if(iq.empty())return;decoder.process(iq.data(),iq.size());ais_rx_process_cf32(ref,interleaved.data(),iq.size());iq.clear();interleaved.clear();};
 for(float sample:x)for(int k=0;k<2;++k){phase+=2*3.141592653589793*2400*sample/96000;std::complex<float> z{float(std::cos(phase)),float(std::sin(phase))};iq.push_back(z);interleaved.push_back(z.real());interleaved.push_back(z.imag());if(iq.size()==4096)process();}
 process();ais_rx_destroy(ref);
 assert(reference.size()==26);const auto total=reference.size();
 for(const auto&v:ours){auto i=reference.find(v);if(i!=reference.end())reference.erase(i);}
 assert(reference.empty());
 std::printf("reference frames matched=%zu; additional CRC-valid frames=%zu (not independently verified)\n",total,ours.size()-total);
}
