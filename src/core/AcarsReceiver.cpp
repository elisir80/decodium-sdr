// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AcarsReceiver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace dsdr::core {
static_assert(std::atomic<double>::is_always_lock_free, "lock-free source parameters required");
namespace { std::size_t capacity(std::size_t n) { if (!n || n>4096) throw std::invalid_argument("invalid ACARS queue capacity"); return n; } }
AcarsReceiver::AcarsReceiver(std::size_t slots,std::size_t maxFrames,std::size_t messages,WorkerHook hook,void *context)
    : m_hook(hook),m_hookContext(context),m_messages(capacity(messages)), m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {}
AcarsReceiver::~AcarsReceiver() { stop(); }
void AcarsReceiver::start() { if(m_running.load()) return; discontinuity(); m_async.start(); m_running.store(true); }
void AcarsReceiver::stop() noexcept { if(!m_running.exchange(false)) return; discontinuity(); m_async.stop(false); }
void AcarsReceiver::discontinuity() noexcept { m_generation.fetch_add(1); }
void AcarsReceiver::setEnabled(bool v) noexcept {
    if(m_enabled.load()==v) return; m_updating.store(true); discontinuity(); m_enabled.store(v); discontinuity(); m_updating.store(false);
}
void AcarsReceiver::setSource(bool p,double rate,double offset) noexcept {
    if(m_present.load()==p && m_rate.load()==rate && m_offset.load()==offset) return;
    m_updating.store(true); discontinuity(); m_present.store(false); m_rate.store(rate); m_offset.store(offset); m_present.store(p); discontinuity(); m_updating.store(false);
}
AcarsReceiver::Status AcarsReceiver::status() const noexcept {
    if(!m_enabled.load()) return Status::Disabled;
    if(!m_running.load()) return Status::Stopped;
    if(m_updating.load() || !m_present.load()) return Status::NoSource;
    const double r=m_rate.load(), o=m_offset.load();
    if(!std::isfinite(r) || r<1000000 || r>4000000) return Status::WrongRate;
    if(!std::isfinite(o) || std::abs(o)>r/2-10000) return Status::WrongOffset;
    return Status::Active;
}
AcarsReceiver::SubmitResult AcarsReceiver::submitForGeneration(const dsp::Complex *p,std::size_t n,std::uint64_t g) noexcept {
    if(g!=generation() || status()!=Status::Active || g!=generation()) return SubmitResult::Stopped;
    auto r=m_async.submit(p,n,g); if(r!=SubmitResult::Accepted) discontinuity(); return r;
}
AcarsReceiver::ReadResult AcarsReceiver::readOne(Message &m) noexcept {
    if(!m_messages.read(&m,1)) { m={}; return ReadResult::Empty; }
    if(m.generation!=generation() || status()!=Status::Active) { m={}; return ReadResult::Stale; }
    return ReadResult::Message;
}
bool AcarsReceiver::read(Message &m) noexcept {
    for(;;) { const auto result=readOne(m); if(result==ReadResult::Message) return true; if(result==ReadResult::Empty) return false; }
}
void AcarsReceiver::resetWorker(void *p) noexcept {
    auto &s=*static_cast<AcarsReceiver*>(p);
    // configure() on the next tagged block initializes and resets all state.
    s.m_workerGeneration=0;
}
void AcarsReceiver::processWorker(void *p,const dsp::Complex *samples,std::size_t n,std::uint64_t g,std::chrono::steady_clock::time_point) noexcept {
    auto &s=*static_cast<AcarsReceiver*>(p);
    if(s.m_hook) s.m_hook(s.m_hookContext);
    if(g!=s.generation() || s.status()!=Status::Active) return;
    try {
        for(std::size_t i=0;i<n;++i) if(!std::isfinite(samples[i].real()) || !std::isfinite(samples[i].imag())) { s.m_workerGeneration=0; ++s.m_errors; s.discontinuity(); return; }
        if(s.m_workerGeneration!=g) { s.m_decoder.configure(s.m_rate.load(),s.m_offset.load()); s.m_workerGeneration=g; }
        s.m_decoder.process(samples,n); s.m_blocks.clear(); s.m_decoder.takeBlocks(s.m_blocks);
        for(const auto &b:s.m_blocks) {
            if(!b.ok || g!=s.generation() || s.status()!=Status::Active) continue;
            Message m; m.size=static_cast<std::size_t>(b.len); m.levelDb=b.levelDb; m.generation=g;
            std::copy_n(b.txt,m.size,m.bytes.begin());
            if(!s.m_messages.write(&m,1)) ++s.m_dropped;
        }
    } catch(...) { s.m_workerGeneration=0; ++s.m_errors; s.discontinuity(); }
}
}
