// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/AisIqGapFence.h"
#include "dsp/SpscRing.h"
#include "ais_synthetic.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
using dsdr::core::AisReceiver;
using dsdr::core::AisIqGapFence;
int main() {
    AisReceiver receiver; receiver.start(); receiver.setSource(true,48000,0); receiver.setEnabled(true);
    AisIqGapFence fence(receiver);
    dsdr::dsp::SpscRing<float> ring(65536);
    const auto iq=ais_test::transmit(ais_test::position(),48000,0,0,2,false);
    std::vector<float> floats; floats.reserve(iq.size()*2);
    for(const auto z:iq) { floats.push_back(z.real()); floats.push_back(z.imag()); }
    const auto half=(floats.size()/4)*2;
    assert(ring.write(floats.data(),half)==half);
    const auto before=fence.snapshot();
    const auto initial=ring.totalRead();
    std::vector<float> buffer(floats.size());
    assert(ring.read(buffer.data(),half)==half);
    fence.notifyLoss(ring.totalWritten());
    assert(!fence.accepts(before,initial,ring.totalRead()));
    // A read straddling the conservative write watermark is dropped whole.
    assert(ring.write(floats.data()+half,floats.size()-half)==floats.size()-half);
    const auto fencePoint=ring.totalWritten(); fence.notifyLoss(fencePoint);
    assert(ring.write(floats.data(),floats.size())==floats.size());
    const auto ticket=fence.snapshot(); const auto start=ring.totalRead();
    const auto firstChunk=ring.read(buffer.data(),floats.size());
    assert(firstChunk==floats.size());
    assert(start<fencePoint && ring.totalRead()>fencePoint);
    assert(!fence.accepts(ticket,start,ring.totalRead()));
    // Drain to a later boundary, then a whole new frame must decode.
    while(ring.available()) ring.discard(ring.available());
    assert(ring.write(floats.data(),floats.size())==floats.size());
    const auto fresh=fence.snapshot(); const auto first=ring.totalRead();
    assert(ring.read(buffer.data(),floats.size())==floats.size()); const auto last=ring.totalRead();
    assert(fence.accepts(fresh,first,last));
    std::vector<dsdr::dsp::Complex> samples(floats.size()/2);
    for(std::size_t i=0;i<samples.size();++i) samples[i]={buffer[2*i],buffer[2*i+1]};
    assert(receiver.submitForGeneration(samples.data(),samples.size(),fresh.generation)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted);
    AisReceiver::Message message; const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!receiver.read(message)) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    assert(message.mmsi==123456789);
    // Deterministic interleaving: a notification on another thread arrives
    // after snapshot but before producer validation.
    auto old=fence.snapshot(); std::atomic<bool> go{false},done{false};
    const auto watermark=ring.totalWritten();
    std::thread notification([&]{ while(!go.load()) std::this_thread::yield(); fence.notifyLoss(watermark); done=true; });
    go=true; while(!done.load()) std::this_thread::yield();
    assert(!fence.accepts(old,last,last+2)); notification.join();
    fence.notifyLoss(ring.totalWritten()+1); assert(!fence.snapshot().valid);
    ring.reset(65536); fence.resetQuiescent(); assert(fence.snapshot().valid);
    assert(ring.totalRead()==0 && ring.totalWritten()==0);
    receiver.stop(); std::puts("AIS IQ loss fence, float boundaries and epochs: OK");
}
