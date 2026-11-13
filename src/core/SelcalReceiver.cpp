// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SelcalReceiver.h"
#include <cmath>
#include <stdexcept>
namespace dsdr::core {
namespace {
constexpr double pi=3.14159265358979323846;
std::size_t validCapacity(std::size_t n) { if(!n || n>4096) throw std::invalid_argument("invalid SELCAL queue capacity"); return n; }
}
SelcalReceiver::SelcalReceiver(std::size_t slots,std::size_t maxFrames,std::size_t capacity,WorkerHook hook,void *context)
    : m_hook(hook),m_hookContext(context),m_messages(validCapacity(capacity)),m_decoder(receive,this),m_async(slots,maxFrames,{this,resetWorker,nullptr,processWorker}) {
    // Anti-alias filter before 48 -> 8 kHz decimation; 2.5 kHz cutoff.
    double sum=0;
    for(std::size_t i=0;i<m_coeff.size();++i) {
        const double k=double(i)-48, cutoff=2500.0/48000.0;
        const double sinc=k==0 ? 2*cutoff : std::sin(2*pi*cutoff*k)/(pi*k);
        const double window=0.42-0.5*std::cos(2*pi*i/96)+0.08*std::cos(4*pi*i/96);
        m_coeff[i]=float(sinc*window); sum+=m_coeff[i];
    }
    for(auto &c:m_coeff) c=float(c/sum);
}
SelcalReceiver::~SelcalReceiver() { stop(); }
void SelcalReceiver::start() { discontinuity(); m_async.start(); }
void SelcalReceiver::stop() noexcept { discontinuity(); m_async.stop(false); }
void SelcalReceiver::setEnabled(bool value) noexcept {
    if(m_enabled.load()==value) return;
    discontinuity(); m_enabled.store(value); discontinuity();
}
void SelcalReceiver::selectChannel(int channel) noexcept {
    if(m_channel.load()==channel) return;
    discontinuity(); m_channel.store(channel); discontinuity();
}
AsyncIqDecoder::SubmitResult SelcalReceiver::submitForGeneration(const dsp::Complex *samples,std::size_t count,std::uint64_t generation) noexcept {
    using R=AsyncIqDecoder::SubmitResult;
    if(!enabled() || generation!=m_generation.load()) return R::Stopped;
    const auto result=m_async.submit(samples,count,generation);
    if(result!=R::Accepted) discontinuity();
    return result;
}
SelcalReceiver::ReadResult SelcalReceiver::readOne(Message &message) noexcept {
    if(!m_messages.read(&message,1)) { message={}; return ReadResult::Empty; }
    if(!enabled() || message.generation!=m_generation.load()) { message={}; return ReadResult::Stale; }
    return ReadResult::Message;
}
void SelcalReceiver::resetFilter() noexcept { m_ring.fill(0); m_pos=m_phase=0; m_decoder.reset(); }
void SelcalReceiver::resetWorker(void *ctx) noexcept { auto &s=*static_cast<SelcalReceiver *>(ctx); s.resetFilter(); s.m_workerGeneration=0; }
void SelcalReceiver::processWorker(void *ctx,const dsp::Complex *samples,std::size_t count,std::uint64_t generation,std::chrono::steady_clock::time_point) noexcept {
    auto &s=*static_cast<SelcalReceiver *>(ctx);
    if(s.m_hook) s.m_hook(s.m_hookContext);
    if(!s.enabled() || generation!=s.m_generation.load()) return;
    if(s.m_workerGeneration!=generation) { s.resetFilter(); s.m_workerGeneration=generation; }
    for(std::size_t i=0;i<count;++i) {
        const float value=samples[i].real();
        if(!std::isfinite(value)) { s.discontinuity(); s.resetFilter(); return; }
        s.m_ring[s.m_pos]=value; s.m_pos=(s.m_pos+1)%s.m_ring.size();
        if(++s.m_phase!=6) continue;
        s.m_phase=0; float out=0; std::size_t p=s.m_pos;
        for(float coeff:s.m_coeff) { p=p?p-1:s.m_ring.size()-1; out+=s.m_ring[p]*coeff; }
        s.m_decoder.process(&out,1);
    }
}
void SelcalReceiver::receive(void *ctx,const std::array<char,5> &code) {
    auto &s=*static_cast<SelcalReceiver *>(ctx);
    if(!s.enabled() || s.m_workerGeneration!=s.m_generation.load()) return;
    Message message{code,s.m_workerGeneration}; if(!s.m_messages.write(&message,1)) ++s.m_dropped;
}
}
