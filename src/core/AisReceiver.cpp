// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AisReceiver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace dsdr::core {
static_assert(std::atomic<double>::is_always_lock_free,
              "AIS source parameters require lock-free atomics on the producer");
namespace {
std::size_t checkedCapacity(std::size_t n) {
    if (!n || n > 4096) throw std::invalid_argument("invalid AIS message capacity");
    return n;
}
}
AisReceiver::AisReceiver(std::size_t slots,std::size_t maxFrames,std::size_t capacity,WorkerHook hook,void *context)
    : m_hook(hook),m_hookContext(context),m_messages(checkedCapacity(capacity)),
      m_decoder(receive,this),m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {}
AisReceiver::~AisReceiver() { stop(); }
void AisReceiver::start() {
    if (m_running.load()) return;
    discontinuity(); m_async.start(); m_running.store(true);
}
void AisReceiver::stop() noexcept {
    if (!m_running.exchange(false)) return;
    discontinuity(); m_async.stop(false);
}
void AisReceiver::discontinuity() noexcept { m_generation.fetch_add(1); }
void AisReceiver::setEnabled(bool v) noexcept {
    if (m_enabled.load()==v) return;
    m_updating.store(true); discontinuity(); m_enabled.store(v); discontinuity(); m_updating.store(false);
}
void AisReceiver::setSource(bool present,double rate,double offset,bool inBand) noexcept {
    if (m_present.load()==present && m_rate.load()==rate && m_offset.load()==offset && m_inBand.load()==inBand) return;
    m_updating.store(true); discontinuity(); m_present.store(false);
    m_rate.store(rate); m_offset.store(offset); m_inBand.store(inBand); m_present.store(present);
    discontinuity(); m_updating.store(false);
}
AisReceiver::Status AisReceiver::status() const noexcept {
    if (!m_enabled.load()) return Status::Disabled;
    if (!m_running.load()) return Status::Stopped;
    if (m_updating.load() || !m_present.load()) return Status::NoSource;
    const double rate=m_rate.load(), offset=m_offset.load();
    if (!std::isfinite(rate)||rate<48000||rate>384000) return Status::WrongRate;
    if (!m_inBand.load() || !std::isfinite(offset)||std::abs(offset)>rate/2-15000) return Status::WrongOffset;
    return Status::Active;
}
AsyncIqDecoder::SubmitResult AisReceiver::submit(const dsp::Complex *s,std::size_t n) noexcept {
    return submitForGeneration(s,n,m_generation.load());
}
AsyncIqDecoder::SubmitResult AisReceiver::submitForGeneration(const dsp::Complex *s,std::size_t n,std::uint64_t generation) noexcept {
    using R=AsyncIqDecoder::SubmitResult;
    if (generation!=m_generation.load() || status()!=Status::Active || generation!=m_generation.load()) return R::Stopped;
    const auto result=m_async.submit(s,n,generation);
    if (result!=R::Accepted) discontinuity();
    return result;
}
AisReceiver::ReadResult AisReceiver::readOne(Message &message) noexcept {
    if(!m_messages.read(&message,1)) { message={}; return ReadResult::Empty; }
    if(message.generation!=m_generation.load() || status()!=Status::Active) {
        message={}; return ReadResult::Stale;
    }
    return ReadResult::Message;
}
bool AisReceiver::read(Message &message) noexcept {
    for (;;) {
        const auto result=readOne(message);
        if(result==ReadResult::Message) return true;
        if(result==ReadResult::Empty) return false;
    }
}
void AisReceiver::resetWorker(void *ctx) noexcept {
    auto &s=*static_cast<AisReceiver *>(ctx); s.m_decoder.reset(); s.m_workerGeneration=0;
}
void AisReceiver::processWorker(void *ctx,const dsp::Complex *samples,std::size_t n,std::uint64_t generation,
                                std::chrono::steady_clock::time_point) noexcept {
    auto &s=*static_cast<AisReceiver *>(ctx);
    if(s.m_hook) s.m_hook(s.m_hookContext);
    if(generation!=s.m_generation.load() || s.status()!=Status::Active) return;
    try {
        if(s.m_workerGeneration!=generation) {
            if(!s.m_decoder.configure(s.m_rate.load(),s.m_offset.load())) return;
            s.m_workerGeneration=generation;
        }
        s.m_decoder.process(samples,n);
    } catch(...) { s.m_decoder.reset(); s.m_workerGeneration=0; ++s.m_errors; s.discontinuity(); }
}
void AisReceiver::receive(void *ctx,const dsp::AisDecoder::Message &decoded) {
    auto &s=*static_cast<AisReceiver *>(ctx);
    if(s.m_workerGeneration!=s.m_generation.load() || s.status()!=Status::Active) return;
    Message m;
    if(decoded.payload.size()>m.payload.size()) { ++s.m_dropped; return; }
    m.size=decoded.payload.size(); std::copy(decoded.payload.begin(),decoded.payload.end(),m.payload.begin());
    m.type=decoded.type; m.mmsi=decoded.mmsi; m.hasPosition=decoded.hasPosition;
    m.latitude=decoded.latitude; m.longitude=decoded.longitude; m.generation=s.m_workerGeneration;
    if(!s.m_messages.write(&m,1)) ++s.m_dropped;
}
}
