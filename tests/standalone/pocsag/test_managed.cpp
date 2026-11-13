// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "async_fixture.h"
#include "core/PocsagReceiver.h"
#include <atomic>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;
using Receiver=dsdr::core::PocsagReceiver;
using Result=dsdr::core::AsyncIqDecoder::SubmitResult;
struct Gate { std::atomic<bool> entered{false}, release{false}; };
static void hook(void *p) noexcept {
    auto &g=*static_cast<Gate *>(p); g.entered=true;
    while (!g.release.load()) std::this_thread::yield();
}
template<class F> static void waitFor(F f) {
    const auto end=std::chrono::steady_clock::now()+10s;
    while (!f()) { assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}
static void feed(Receiver &rx,const std::vector<dsdr::dsp::Complex> &iq) {
    for (std::size_t pos=0;pos<iq.size();) {
        const auto n=std::min<std::size_t>(1024,iq.size()-pos);
        assert(rx.submit(iq.data()+pos,n)==Result::Accepted); pos+=n;
    }
}
int main() {
    Gate gate; Receiver rx(1024,1024,16,hook,&gate);
    assert(rx.startManaged());
    assert(rx.managedStatus()==Receiver::ManagedStatus::Disabled);
    assert(!rx.configureManaged(44100,1200,0));
    rx.setManagedEnabled(true);
    assert(rx.managedStatus()==Receiver::ManagedStatus::InvalidConfiguration);
    assert(rx.configureManaged(2000000,1200,100000));
    assert(rx.managedStatus()==Receiver::ManagedStatus::Configuring);
    assert(!rx.configureManaged(2000001,1200,0));
    assert(rx.configureManaged(96000,1200,2000));
    assert(rx.managedStatus()==Receiver::ManagedStatus::Configuring);
    const auto first=generate(96000,1200,2600,0.01,1);
    feed(rx,first); waitFor([&]{return gate.entered.load();});
    const auto old=rx.generation();
    assert(rx.configureManaged(48000,2400,-2000));
    assert(rx.submitForGeneration(first.data(),1,old)==Result::Stopped);
    const auto second=generate(48000,2400,-1400,0.01,2);
    feed(rx,second);
    gate.release=true;
    waitFor([&]{return rx.managedStatus()==Receiver::ManagedStatus::Active;});
    Receiver::Message msg;
    waitFor([&]{return rx.read(msg);});
    assert(msg.address==1234563 && std::string(msg.text.data(),msg.size)=="HELLO PAGER");
    assert(!rx.read(msg));
    rx.setManagedEnabled(false);
    assert(rx.managedStatus()==Receiver::ManagedStatus::Disabled);
    assert(rx.submit(second.data(),1)==Result::Stopped);
    rx.setManagedEnabled(true);
    assert(rx.managedStatus()==Receiver::ManagedStatus::Configuring);
    feed(rx,second);
    waitFor([&]{return rx.read(msg);});
    rx.stop(false);
    assert(rx.managedStatus()==Receiver::ManagedStatus::Stopped);
}
