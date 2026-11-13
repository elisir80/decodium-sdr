// SPDX-License-Identifier: GPL-3.0-or-later
// Uses the same independently generated Bell 202 audio as the DSP A/B test.
#define main aprs_dsp_test_main
#include "../standalone/tst_aprs_afsk.cpp"
#undef main
#include "core/AprsReceiver.h"
#include "core/AprsController.h"
#include <QCoreApplication>
#include <QVariantMap>
#include <atomic>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;
using dsdr::core::AprsReceiver;
using R = dsdr::core::AsyncIqDecoder::SubmitResult;
struct Gate {
    std::atomic<bool> block{false}, entered{false};
    std::atomic<unsigned> calls{0};
    static void hook(void *p) noexcept {
        auto &g=*static_cast<Gate *>(p);
        ++g.calls;
        if (!g.block.load()) return;
        g.entered.store(true);
        while (g.block.load()) std::this_thread::yield();
    }
};
template<typename F> void waitFor(F f) {
    auto end=std::chrono::steady_clock::now()+5s;
    while (!f()) { assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}
std::vector<dsdr::dsp::Complex> stereo(const std::vector<float>& samples) {
    std::vector<dsdr::dsp::Complex> result; result.reserve(samples.size());
    for (float x : samples) result.emplace_back(x*1.5f, x*0.5f); return result;
}
void submit(AprsReceiver &r, int id, const std::vector<dsdr::dsp::Complex> &v) {
    assert(r.submit(id,v.data(),v.size())==R::Accepted);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto good=frame(); auto invalid=good; invalid.back()^=4;
    const auto sound=waveform(good,48000,0.6,7), bad=waveform(invalid,48000,0.6,7);
    auto input=stereo(sound), badInput=stereo(bad);
    std::vector<Frame> direct;
    Decoder directDecoder(capture,&direct); assert(directDecoder.configure(48000));
    directDecoder.process(sound.data(),sound.size()); assert(direct.size()==1);
    Gate gate; AprsReceiver rx(16,100000,2,Gate::hook,&gate);
    struct PollGate {
        AprsReceiver* rx; bool armed=false,called=false;
        static void hook(void* p) noexcept { auto& g=*static_cast<PollGate*>(p); if(g.armed) {g.armed=false; g.called=true; g.rx->discontinuity();} }
    } pollGate{&rx};
    dsdr::core::AprsController controller(&rx,nullptr,PollGate::hook,&pollGate);
    controller.setChannels({QVariantMap{{"id", 1},{"label","NFM 1"}}, QVariantMap{{"id", 2},{"label","NFM 2"}}});
    controller.setEnabled(true); assert(rx.status()==AprsReceiver::Status::NoChannel);
    controller.selectChannel(99); assert(rx.selectedChannel()==-1);
    controller.selectChannel(1); assert(rx.status()==AprsReceiver::Status::NoSource);
    rx.setChannelState(1,true,false,48000); assert(rx.status()==AprsReceiver::Status::NotNfm);
    rx.setChannelState(1,true,true,4000); assert(rx.status()==AprsReceiver::Status::WrongRate);
    rx.setChannelState(1,true,true,48000); assert(rx.status()==AprsReceiver::Status::Active);
    rx.start();
    submit(rx,1,badInput);
    submit(rx,1,input);
    AprsReceiver::Message message;
    waitFor([&]{return rx.read(message);});
    Frame async(message.bytes.begin(),message.bytes.begin()+message.size);
    assert(async==direct[0]); assert(!rx.read(message));
    submit(rx,1,input); waitFor([&]{controller.poll();return controller.frameCount()==1;});
    assert(controller.source()=="IU8LMC-7"); assert(controller.destination()=="APRS");
    assert(controller.information().startsWith("!3554"));
    pollGate.armed=true; submit(rx,1,input);
    waitFor([&]{controller.poll();return pollGate.called;});
    assert(controller.frameCount()==1 && controller.source().isEmpty() && controller.information().isEmpty());
    submit(rx,1,input); waitFor([&]{controller.poll();return controller.frameCount()==2;});
    assert(controller.source()=="IU8LMC-7");
    // Hold the worker on the first old block and queue another. Switch channel,
    // frequency (explicit discontinuity), or disable before releasing it.
    for (int scenario=0; scenario<3; ++scenario) {
        gate.entered=false; gate.block=true;
        submit(rx,1,input); waitFor([&]{return gate.entered.load();}); submit(rx,1,input);
        if (scenario==0) {
            controller.selectChannel(2); rx.setChannelState(2,true,true,48000);
            assert(controller.source().isEmpty() && controller.destination().isEmpty());
            assert(controller.information().isEmpty() && controller.lastFrame().isEmpty());
        }
        if (scenario==1) rx.discontinuity();
        if (scenario==2) { controller.setEnabled(false); controller.setEnabled(true); }
        gate.block=false;
        const int newId=scenario==0 ? 2 : 1;
        submit(rx,newId,input);
        waitFor([&]{return rx.read(message);});
        assert(message.generation==rx.generation());
        std::this_thread::sleep_for(30ms); assert(!rx.read(message));
        if (scenario==0) { controller.selectChannel(1); rx.setChannelState(1,true,true,48000); }
    }
    // An already decoded queued result must also be discarded after retuning.
    for (int scenario=0; scenario<3; ++scenario) {
        auto before = gate.calls.load();
        submit(rx,1,input);
        dsdr::dsp::Complex barrier(0,0); assert(rx.submit(1,&barrier,1)==R::Accepted);
        waitFor([&]{return gate.calls.load() >= before + 2;});
        if (scenario==0) { controller.selectChannel(2); rx.setChannelState(2,true,true,48000); }
        if (scenario==1) rx.discontinuity();
        if (scenario==2) { controller.setEnabled(false); controller.setEnabled(true); }
        // UI selection/enable changes poll immediately; a raw discontinuity
        // leaves the stale item for the next bounded poll.
        assert(rx.readOne(message)==(scenario==1 ? AprsReceiver::ReadResult::Stale
                                                 : AprsReceiver::ReadResult::Empty));
        assert(rx.readOne(message)==AprsReceiver::ReadResult::Empty);
        if (scenario==0) { controller.selectChannel(1); rx.setChannelState(1,true,true,48000); }
    }
    // The output queue is bounded, reports overflow, and retains FIFO.
    for (int i=0;i<5;++i) submit(rx,1,input);
    waitFor([&]{return rx.droppedFrames()>=3;});
    int count=0; while (rx.read(message)) ++count; assert(count==2);
    // Samples read before a retune cannot be submitted under its new generation.
    const auto staleGeneration = rx.generation();
    rx.discontinuity();
    assert(rx.submitForGeneration(1,input.data(),input.size(),staleGeneration)==R::Stopped);
    assert(!rx.read(message));
    // Input loss invalidates the partially decoded packet; a fresh packet works.
    const auto half=input.size()/2;
    assert(rx.submit(1,input.data(),half)==R::Accepted);
    assert(rx.submit(1,nullptr,1)==R::Invalid);
    assert(rx.submit(1,input.data()+half,input.size()-half)==R::Accepted);
    std::this_thread::sleep_for(100ms); assert(!rx.read(message));
    submit(rx,1,input); waitFor([&]{return rx.read(message);});
    controller.setChannels({QVariantMap{{"id",2},{"label","NFM 2"}}});
    assert(rx.selectedChannel()==-1);
    rx.stop();
    std::puts("APRS async A/B, worker backlog, stale frames and controller: OK");
}
