// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorEngineTap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace dsdr;
constexpr double pi=3.14159265358979323846;
void check(bool b,const char* s) { if(!b) {std::cerr<<"FAIL "<<s<<'\n';std::exit(1);} }
std::vector<dsp::Complex> signal(double rate,double radial,double offset,bool vor) {
 std::vector<dsp::Complex> x; x.reserve(std::size_t(rate*3));
 for(int i=0;i<int(rate*3);++i) { double t=i/rate;
  double a=vor?1+.3*std::cos(2*pi*30*t-radial*pi/180)+.3*std::cos(2*pi*9960*t+16*std::sin(2*pi*30*t)):1.;
  x.emplace_back(float(a*std::cos(2*pi*offset*t)),float(a*std::sin(2*pi*offset*t)));
 } return x;
}
void feed(core::VorEngineTap& tap,const std::vector<dsp::Complex>& v,std::size_t size) {
 for(std::size_t i=0;i<v.size();) {auto n=std::min(size,v.size()-i);tap.process(v.data()+i,n);i+=n;}
}
bool wait(core::VorAsyncReceiver& r,core::VorAsyncReceiver::Result& out) {
 auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 do { if(r.read(out)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); } while(std::chrono::steady_clock::now()<limit); return false;
}
int main() {
 for(unsigned rate:{96000u,144000u,192000u,200000u,240000u,250000u,288000u,336000u,384000u,480000u,500000u,768000u,960000u}) for(double az:{0.,90.,270.}) {
  core::VorAsyncReceiver rx(1024,512); check(rx.configure(core::VorAsyncReceiver::Input::FilteredIq,48000) && rx.start(),"rx start");
  core::VorEngineTap tap(rx); check(tap.configure(rate,7000),"tap configure");
  auto input=signal(rate,az,7000,true); const auto original=input;
  // A: branch not called; B: same input with branch enabled.
  core::VorAsyncReceiver::Result result;
  check(tap.submitted()==0 && !rx.read(result),"disabled branch");
  const auto processingStart=std::chrono::steady_clock::now();
  feed(tap,input,317);
  const auto processingTime=std::chrono::steady_clock::now()-processingStart;
  if(rate==960000 && az==90.) {
   const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(processingTime).count();
   std::cout<<"VOR 960k: "<<ms<<" ms for 3000 ms synthetic IQ\n";
  }
  check(input==original,"source unchanged");
  check(wait(rx,result) && result.value.valid,"valid");
  if(std::abs(std::remainder(result.value.radialDegrees-az,360.))>3) { std::cerr<<rate<<' '<<az<<' '<<result.value.radialDegrees<<'\n'; check(false,"angle"); }
  if((rate==96000 || rate==200000 || rate==500000) && az==0.) {
   const auto stale=rx.generation(); rx.invalidateResults();
   const auto before=rx.stats().accepted;
   tap.processForGeneration(input.data(),input.size(),stale);
   check(rx.stats().accepted==before,"stale source was submitted");
   feed(tap,input,317); check(wait(rx,result) && result.value.valid,"recovery after invalidation");
  }
  if(rate==200000 && az==0.) {
   tap.reset();
   tap.process(input.data(),input.size()/2);
   const auto generation=rx.generation();
   const dsp::Complex corrupt{std::numeric_limits<float>::quiet_NaN(),0};
   tap.process(&corrupt,1);
   check(rx.generation()!=generation && !rx.read(result),"non-finite IQ invalidates partial result");
   feed(tap,input,317);
   check(wait(rx,result) && result.value.valid &&
         std::abs(std::remainder(result.value.radialDegrees-az,360.))<3.,"recovery after non-finite IQ");
  }
  tap.reset(); check(!rx.read(result),"discontinuity");
  auto carrier=signal(rate,az,7000,false); feed(tap,carrier,1021); check(wait(rx,result) && !result.value.valid,"carrier negative");
  if((rate==192000 || rate==200000 || rate==500000) && az==90.) {
   // Same VOR waveform, shifted outside the selected channel: it must not
   // produce a valid radial. A selected-channel waveform must recover.
   tap.reset();
   auto adjacent=signal(rate,az,37000,true);
   feed(tap,adjacent,317);
   check(wait(rx,result) && !result.value.valid,"adjacent-channel rejection");
   tap.reset(); feed(tap,input,317);
   check(wait(rx,result) && result.value.valid &&
         std::abs(std::remainder(result.value.radialDegrees-az,360.))<3.,"selected channel recovery");
  }
  rx.stop();
 }
 core::VorAsyncReceiver rx; core::VorEngineTap tap(rx);
 check(!tap.configure(48000,0) && !tap.configure(2048001,0) && !tap.configure(192000,90000),"invalid config");
 std::cout<<"VOR raw IQ front end 39 rate/angle cases, carrier and discontinuity passed\n";
 return 0;
}
