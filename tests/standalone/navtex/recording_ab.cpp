// SPDX-License-Identifier: GPL-3.0-or-later
// Optional external 24 kHz float NAVTEX recording; see priority-ten.md.
#include "dsp/navtex/NavtexAudioDecoder.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc,char**argv){
 if(argc!=2 && argc!=3)return 2;FILE*f=std::fopen(argv[1],"rb");if(!f)return 2;
 std::vector<float> audio;float block[4096];size_t n;
 while((n=std::fread(block,sizeof(float),4096,f)))audio.insert(audio.end(),block,block+n);
 bool error=std::ferror(f);std::fclose(f);if(error||audio.empty())return 2;
 const bool second=argc==3 && std::string(argv[2])=="eight-minute";
 if(argc==3 && !second)return 2;
 const std::vector<std::string> expected=second ? std::vector<std::string>{"PA19","PA40","PA42","PA43","PA49","PA54","PA56","PA57","PJ77"} : std::vector<std::string>{"SA97","SA77","SA56","SA51","SE89"};
 for(size_t chunk:{size_t(137),size_t(4096)}) {
  std::vector<std::string> messages; bool bodyOk=!second;
  dsdr::navtex::NavtexAudioDecoder decoder([&](auto const&m){messages.push_back(m.header);if(m.header=="PA56" && m.text.find("NETHERLANDS COASTGUARD")!=std::string::npos && m.text.find("FOGHORN INOPERATIVE")!=std::string::npos)bodyOk=true;});
  for(size_t i=0;i<audio.size();i+=chunk)decoder.process(audio.data()+i,std::min(chunk,audio.size()-i));
  if(messages!=expected || !bodyOk){std::fprintf(stderr,"chunk=%zu decoded=%zu expected=%zu body=%d\n",chunk,messages.size(),expected.size(),bodyOk);return 1;}
  decoder.reset();messages.clear();std::vector<float> silence(24000*3,0);decoder.process(silence.data(),silence.size());if(!messages.empty())return 1;
  std::printf("chunk=%zu %zu reference messages; silence rejected\n",chunk,expected.size());
 }
}
