// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorAsyncReceiver.h"
#include "core/VorController.h"
#include <QCoreApplication>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace dsdr;
constexpr double pi=3.14159265358979323846, rate=48000;
void check(bool value,const char* label) { if(!value) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); } }
void collect(const dsp::VorReceiver::Result& r,void* p) { *static_cast<dsp::VorReceiver::Result*>(p)=r; }
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);
 for(bool iq:{false,true}) {
  std::vector<float> audio; std::vector<dsp::Complex> samples;
  for(int n=0;n<3*int(rate);++n) { double t=n/rate; double v=1+.3*std::cos(2*pi*30*t-1.2)+.3*std::cos(2*pi*9960*t+16*std::sin(2*pi*30*t)); audio.push_back(v); samples.emplace_back(v*std::cos(500*pi*t),v*std::sin(500*pi*t)); }
  dsp::VorReceiver::Result baseline{}; dsp::VorReceiver direct(collect,&baseline); direct.configure(rate);
  if(iq) direct.processIq(samples.data(),samples.size()); else direct.processAudio(audio.data(),audio.size());
  core::VorAsyncReceiver rx(512,1024);
  struct PollGate {
   core::VorAsyncReceiver* rx; bool armed=false, called=false;
   static void run(void* p) noexcept { auto& g=*static_cast<PollGate*>(p); if(g.armed) { g.armed=false; g.called=true; g.rx->discontinuity(); } }
  } gate{&rx};
  core::VorController controller(&rx,nullptr,PollGate::run,&gate);
  check(!rx.start(),"start before configure"); check(!rx.configure(core::VorAsyncReceiver::Input::Audio,8000),"invalid rate");
  check(rx.configure(iq?core::VorAsyncReceiver::Input::FilteredIq:core::VorAsyncReceiver::Input::Audio,rate),"configure");
  int requests=0; QObject::connect(&controller,&core::VorController::configurationRequested,[&](bool,int){++requests;});
  controller.setInput(iq?1:0); controller.setEnabled(true); check(requests>=1,"controller request");
  check(rx.start(),"start"); controller.setActive(true);
  for(std::size_t p=0;p<audio.size();p+=512) {
   auto n=std::min<std::size_t>(512,audio.size()-p);
   auto r=iq?rx.submitIq(samples.data()+p,n):rx.submitAudio(audio.data()+p,n);
   check(r==core::AsyncIqDecoder::SubmitResult::Accepted,"submit");
  }
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  do { controller.poll(); if(controller.valid()) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); } while(std::chrono::steady_clock::now()<deadline);
  check(baseline.valid && controller.valid(),"valid result"); check(std::abs(std::remainder(controller.radial()-baseline.radialDegrees,360.))<0.001,"direct versus worker");
  // Force a generation change after a result is applied but before poll returns.
  for(std::size_t p=0;p<audio.size();p+=512) {
   auto n=std::min<std::size_t>(512,audio.size()-p);
   check((iq?rx.submitIq(samples.data()+p,n):rx.submitAudio(audio.data()+p,n))==core::AsyncIqDecoder::SubmitResult::Accepted,"race input");
  }
  gate.armed=true;
  deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  do { controller.poll(); if(gate.called) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); } while(std::chrono::steady_clock::now()<deadline);
  check(gate.called && !controller.valid() && controller.identifier().isEmpty(),"generation change during poll clears result");
  auto old=rx.generation(); rx.discontinuity(); controller.poll(); check(!controller.valid() && rx.generation()>old,"gap invalidates");
  // Half a second cannot create a full post-gap measurement.
  for(std::size_t p=0;p<24000;p+=512) { auto n=std::min<std::size_t>(512,24000-p); check((iq?rx.submitIq(samples.data()+p,n):rx.submitAudio(audio.data()+p,n))==core::AsyncIqDecoder::SubmitResult::Accepted,"post gap partial"); }
  std::this_thread::sleep_for(std::chrono::milliseconds(150)); controller.poll(); check(!controller.valid(),"partial segment cannot bridge gap");
  for(std::size_t p=24000;p<audio.size();p+=512) { auto n=std::min<std::size_t>(512,audio.size()-p); check((iq?rx.submitIq(samples.data()+p,n):rx.submitAudio(audio.data()+p,n))==core::AsyncIqDecoder::SubmitResult::Accepted,"post gap recovery"); }
  deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  do { controller.poll(); if(controller.valid()) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); } while(std::chrono::steady_clock::now()<deadline);
  check(controller.valid() && std::abs(std::remainder(controller.radial()-baseline.radialDegrees,360.))<.01,"post gap recovery");
  auto wrong=iq?rx.submitAudio(audio.data(),512):rx.submitIq(samples.data(),512);
  check(wrong==core::AsyncIqDecoder::SubmitResult::Invalid,"wrong source");
  check(rx.submitAudio(nullptr,9999)==core::AsyncIqDecoder::SubmitResult::TooLarge,"oversize");
  rx.stop(); controller.setActive(false); controller.poll(); check(!controller.valid(),"stop clears");
  check(rx.submitIq(samples.data(),1)==core::AsyncIqDecoder::SubmitResult::Stopped,"stopped");
 }
 // Publish another complete result while the controller applies the first.
 // One poll must consume only the first result; the next poll consumes the new one.
 {
  struct Counter { std::atomic<unsigned> blocks{0}; static void run(void* p) noexcept { ++static_cast<Counter*>(p)->blocks; } } counter;
  core::VorAsyncReceiver receiver(512,1024,Counter::run,&counter);
  check(receiver.configure(core::VorAsyncReceiver::Input::Audio,rate) && receiver.start(),"bounded receiver start");
  std::vector<float> audio;
  for(int n=0;n<3*int(rate);++n) { const double t=n/rate; audio.push_back(float(1+.3*std::cos(2*pi*30*t-1.2)+.3*std::cos(2*pi*9960*t+16*std::sin(2*pi*30*t)))); }
  auto submit=[&] {
   for(std::size_t p=0;p<audio.size();p+=1024) {
    auto n=std::min<std::size_t>(1024,audio.size()-p);
    check(receiver.submitAudio(audio.data()+p,n)==core::AsyncIqDecoder::SubmitResult::Accepted,"bounded submit");
   }
   const float marker=0; check(receiver.submitAudio(&marker,1)==core::AsyncIqDecoder::SubmitResult::Accepted,"bounded marker");
  };
  struct Publish {
   core::VorAsyncReceiver* rx; Counter* counter; const std::vector<float>* audio; int calls=0; bool ok=true;
   static void run(void* p) noexcept {
    auto& self=*static_cast<Publish*>(p); if(++self.calls!=1) return;
    for(std::size_t i=0;i<self.audio->size();i+=1024) {
     const auto n=std::min<std::size_t>(1024,self.audio->size()-i);
     if(self.rx->submitAudio(self.audio->data()+i,n)!=core::AsyncIqDecoder::SubmitResult::Accepted) { self.ok=false; return; }
    }
    const float marker=0;
    if(self.rx->submitAudio(&marker,1)!=core::AsyncIqDecoder::SubmitResult::Accepted) { self.ok=false; return; }
    const auto target=self.rx->stats().accepted;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(self.counter->blocks.load()<target && std::chrono::steady_clock::now()<until)
     std::this_thread::sleep_for(std::chrono::milliseconds(1));
    self.ok=self.counter->blocks.load()>=target;
   }
  } publish{&receiver,&counter,&audio};
  core::VorController controller(&receiver,nullptr,Publish::run,&publish);
  controller.setEnabled(true); controller.setActive(true); submit();
  const auto target=receiver.stats().accepted;
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(counter.blocks.load()<target && std::chrono::steady_clock::now()<until)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(counter.blocks.load()>=target,"initial bounded result ready");
  controller.poll(); check(publish.ok && publish.calls==1 && controller.valid(),"one result per poll");
  controller.poll(); check(publish.calls==2 && controller.valid(),"next poll receives pending result");
  receiver.stop();
 }
 // One-slot queue saturates; a dropped block changes generation and forbids stale results.
 core::VorAsyncReceiver limited(1,4096); check(limited.configure(core::VorAsyncReceiver::Input::Audio,rate) && limited.start(),"limited start");
 std::vector<float> block(4096,1); bool full=false; auto gen=limited.generation();
 for(int i=0;i<200000 && !full;++i) full=limited.submitAudio(block.data(),block.size())==core::AsyncIqDecoder::SubmitResult::Full;
 check(full && limited.generation()>gen && limited.stats().droppedBlocks>0,"bounded overflow");
 core::VorAsyncReceiver::Result result; check(!limited.read(result),"no stale after overflow"); limited.stop();
 {
  auto receiver=std::make_unique<core::VorAsyncReceiver>();
  core::VorController detached(receiver.get()); detached.detach(); receiver.reset();
  detached.setEnabled(true); detached.setInput(1); detached.setActive(true); detached.poll();
  check(!detached.enabled() && !detached.active() && !detached.valid(),"safe detach");
  check(detached.status().contains("arresto"),"detached status");
 }
 std::cout<<"direct/worker A/B audio and IQ, controller, gap, overflow, lifecycle passed\n";
}
