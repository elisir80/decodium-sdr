// SPDX-License-Identifier: GPL-3.0-or-later
#define main vor_low_rate_fixture_main
#include "test.cpp"
#undef main
int main() {
 for(unsigned rate:{960001u,1000000u,1300000u,1536000u,2000000u,2048000u}) {
  core::VorAsyncReceiver rx(2048,512);
  check(rx.configure(core::VorAsyncReceiver::Input::FilteredIq,48000) && rx.start(),"receiver start");
  core::VorEngineTap tap(rx); check(tap.configure(rate,7000),"high rate configure");
  auto good=signal(rate,90,7000,true);
  const auto start=std::chrono::steady_clock::now(); feed(tap,good,8192);
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  core::VorAsyncReceiver::Result result;
  check(wait(rx,result) && result.value.valid && std::abs(std::remainder(result.value.radialDegrees-90.,360.))<3.,"selected signal");
  const double baseline=result.value.radialDegrees;
  check(rx.stats().droppedBlocks==0,"worker overflow");
  std::cout<<"VOR "<<rate<<" Hz: "<<elapsed<<" ms for 3000 ms synthetic IQ\n";
  if(rate==1300000 || rate==2048000) {
   for(std::size_t size:{std::size_t(317),good.size()}) {
    tap.reset(); feed(tap,good,size);
    check(wait(rx,result) && result.value.valid &&
          std::abs(std::remainder(result.value.radialDegrees-baseline,360.))<0.01,
          "irregular versus whole-block radial");
   }
  }
  tap.reset(); auto adjacent=signal(rate,90,37000,true); feed(tap,adjacent,8192);
  check(wait(rx,result) && !result.value.valid,"adjacent signal rejected");
  tap.reset(); feed(tap,good,8192);
  check(wait(rx,result) && result.value.valid,"recovery after adjacent signal");
  const auto stale=rx.generation(); rx.invalidateResults();
  const auto before=rx.stats().accepted;
  tap.processForGeneration(good.data(),good.size(),stale);
  check(rx.stats().accepted==before && !rx.read(result),"stale generation rejected");
  feed(tap,good,8192); check(wait(rx,result) && result.value.valid,"recovery after generation change");
  tap.reset(); tap.process(good.data(),good.size()/2);
  const auto generation=rx.generation();
  const dsp::Complex corrupt{std::numeric_limits<float>::quiet_NaN(),0};
  tap.process(&corrupt,1);
  check(rx.generation()!=generation && !rx.read(result),"non-finite input invalidates partial result");
  feed(tap,good,8192); check(wait(rx,result) && result.value.valid,"recovery after non-finite input");
  rx.stop();
 }
}
